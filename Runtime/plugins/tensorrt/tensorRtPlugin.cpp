#include "pluginSupport.hpp"
#include "memory/gpuBufferPool.hpp"

#include <NvInfer.h>
#include <NvInferPlugin.h>
#include <cuda_runtime_api.h>

#include <atomic>
#include <charconv>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace visionRuntime::plugins {
namespace {

void checkCuda(cudaError_t status) {
	if (status != cudaSuccess) {
		throw Failure(VISION_RUNTIME_BACKEND_STATUS_BACKEND_ERROR, cudaGetErrorString(status));
	}
}

class Logger final : public nvinfer1::ILogger {
public:
	void log(Severity severity, const char* message) noexcept override {
		if (severity <= Severity::kERROR) {
			try {
				std::scoped_lock lock(mutex_);
				message_ = message;
			} catch (...) {
			}
		}
	}
	std::string message() const {
		std::scoped_lock lock(mutex_);
		return message_;
	}
private:
	mutable std::mutex mutex_;
	std::string message_;
};

struct Stream {
	cudaStream_t value = nullptr;
	~Stream() {
		if (value != nullptr) {
			cudaStreamDestroy(value);
		}
	}
};

// One inference worker: an execution context plus its own CUDA stream and
// device buffer pools. Workers are independent, so `streams > 1` lets
// concurrent infer() calls overlap H2D/compute/D2H on different streams.
struct Worker {
	std::unique_ptr<nvinfer1::IExecutionContext> context;
	Stream stream;
	std::optional<memory::GpuBufferPool> inputPool;
	std::optional<memory::GpuBufferPool> outputPool;
	std::mutex mutex;
};

struct Backend {
	~Backend() { static_cast<void>(cudaSetDevice(device)); }
	Logger logger;
	std::unique_ptr<nvinfer1::IRuntime> runtime;
	std::unique_ptr<nvinfer1::ICudaEngine> engine;
	std::vector<std::unique_ptr<Worker>> workers;
	std::atomic<std::size_t> nextWorker{0};
	std::shared_ptr<memory::GpuAllocator> allocator;
	std::string inputName;
	std::string outputName;
	int device = 0;
	int profile = 0;
};

void checkTensorRt(bool succeeded, const Backend& backend, const char* operation) {
	if (!succeeded) {
		throw Failure(VISION_RUNTIME_BACKEND_STATUS_BACKEND_ERROR,
			std::string(operation) + ": " + backend.logger.message());
	}
}

VisionRuntimeBackendStatus create(const VisionRuntimeBackendCreateOptions* options,
	void** handle, VisionRuntimeErrorBuffer error) noexcept {
	*handle = nullptr;
	return boundary(error, [&] {
		const auto document = parseOptions(*options);
		require(text(options->artifactKind) == "engine", VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED,
			"TensorRT requires an engine artifact");
		const auto path = artifactPath(*options);
		require(std::filesystem::is_regular_file(path), VISION_RUNTIME_BACKEND_STATUS_NOT_FOUND,
			"TensorRT engine was not found");
		auto backend = std::make_unique<Backend>();
		const auto device = text(options->device);
		require(!device.empty(), VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT,
			"TensorRT requires a CUDA device index");
		const auto parsed = std::from_chars(device.data(), device.data() + device.size(), backend->device);
		require(parsed.ec == std::errc{} && parsed.ptr == device.data() + device.size() && backend->device >= 0,
			VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT, "invalid CUDA device index");
		backend->profile = document.value("optimizationProfile", 0);
		require(backend->profile >= 0, VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT,
			"optimizationProfile must not be negative");
		backend->inputName = text(options->inputName);
		backend->outputName = text(options->outputName);
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		require(static_cast<bool>(file), VISION_RUNTIME_BACKEND_STATUS_NOT_FOUND, "engine could not be opened");
		const auto size = file.tellg();
		require(size > 0, VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT, "engine is empty");
		std::vector<char> serialized(static_cast<std::size_t>(size));
		file.seekg(0);
		require(static_cast<bool>(file.read(serialized.data(), size)),
			VISION_RUNTIME_BACKEND_STATUS_BACKEND_ERROR, "engine could not be read");
		checkCuda(cudaSetDevice(backend->device));
		backend->allocator = std::make_shared<memory::GpuAllocator>(backend->device);
		backend->runtime.reset(nvinfer1::createInferRuntime(backend->logger));
		checkTensorRt(static_cast<bool>(backend->runtime), *backend, "runtime creation failed");
		checkTensorRt(initLibNvInferPlugins(&backend->logger, ""), *backend, "plugin initialization failed");
		backend->engine.reset(backend->runtime->deserializeCudaEngine(serialized.data(), serialized.size()));
		checkTensorRt(static_cast<bool>(backend->engine), *backend, "engine deserialization failed");
		auto& engine = *backend->engine;
		require(engine.getNbIOTensors() == 2, VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED,
			"TensorRT requires exactly one input and one output");
		require(engine.getTensorIOMode(backend->inputName.c_str()) == nvinfer1::TensorIOMode::kINPUT &&
			engine.getTensorIOMode(backend->outputName.c_str()) == nvinfer1::TensorIOMode::kOUTPUT,
			VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT, "tensor names do not match the engine");
		require(backend->profile < engine.getNbOptimizationProfiles(),
			VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT, "optimization profile is out of range");
		const auto maxBatch = document.value("maxBatchSize", 0);
		require(maxBatch >= 0, VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT,
			"maxBatchSize must not be negative");
		const auto dynamicBatch = document.value("dynamicBatch", false);
		const auto profileMin = engine.getProfileShape(
			backend->inputName.c_str(), backend->profile, nvinfer1::OptProfileSelector::kMIN);
		const auto profileMax = engine.getProfileShape(
			backend->inputName.c_str(), backend->profile, nvinfer1::OptProfileSelector::kMAX);
		// The batch dimension is the first dimension of the (NCHW) input.
		require(profileMin.nbDims >= 1, VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED,
			"TensorRT input profile must have at least one dimension");
		const auto profileMinBatch = profileMin.d[0];
		const auto profileMaxBatch = profileMax.d[0];
		if (dynamicBatch) {
			require(profileMinBatch >= 1 && profileMaxBatch > profileMinBatch,
				VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT,
				"dynamicBatch requires an engine profile with min batch >= 1 and "
				"max batch > min batch (rebuild the engine with min/opt/max shapes)");
		}
		if (maxBatch > 0) {
			// Dump the full optimization profile for diagnostics.
			std::string diag;
			for (const auto sel : {nvinfer1::OptProfileSelector::kMIN,
					nvinfer1::OptProfileSelector::kOPT, nvinfer1::OptProfileSelector::kMAX}) {
				const auto s = engine.getProfileShape(
					backend->inputName.c_str(), backend->profile, sel);
				diag += "[";
				for (int32_t d = 0; d < s.nbDims; ++d) {
					diag += std::to_string(s.d[d]);
					diag += ",";
				}
				diag += "] ";
			}
			if (profileMaxBatch < maxBatch) {
				throw Failure(VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT,
					"engine optimization profile max batch (" +
					std::to_string(profileMaxBatch) + ") < maxBatchSize " +
					std::to_string(maxBatch) + " | profile=" + diag);
			}
		}
		const auto streams = document.value("streams", 1);
		require(streams >= 1, VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT,
			"streams must be at least 1");
		for (const auto& name : {backend->inputName, backend->outputName}) {
			require(engine.getTensorDataType(name.c_str()) == nvinfer1::DataType::kFLOAT &&
				engine.getTensorLocation(name.c_str()) == nvinfer1::TensorLocation::kDEVICE &&
				engine.getTensorFormat(name.c_str(), backend->profile) == nvinfer1::TensorFormat::kLINEAR &&
				engine.getTensorVectorizedDim(name.c_str(), backend->profile) == -1 &&
				!engine.isShapeInferenceIO(name.c_str()), VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED,
				"TensorRT requires linear device Float32 execution tensors");
		}
		backend->workers.reserve(static_cast<std::size_t>(streams));
		for (int index = 0; index < streams; ++index) {
			auto worker = std::make_unique<Worker>();
			worker->context.reset(engine.createExecutionContext());
			checkTensorRt(static_cast<bool>(worker->context), *backend,
				"context creation failed");
			checkCuda(cudaStreamCreateWithFlags(&worker->stream.value, cudaStreamNonBlocking));
			checkTensorRt(worker->context->setOptimizationProfileAsync(
				backend->profile, worker->stream.value),
				*backend, "profile selection failed");
			checkCuda(cudaStreamSynchronize(worker->stream.value));
			backend->workers.push_back(std::move(worker));
		}
		*handle = backend.release();
	});
}

void destroy(void* handle) noexcept { delete static_cast<Backend*>(handle); }

core::TensorBuffer acquire(std::optional<memory::GpuBufferPool>& pool,
	std::size_t bytes, const std::shared_ptr<memory::GpuAllocator>& allocator) {
	if (!pool || pool->bufferCapacity() < bytes) {
		auto created = memory::GpuBufferPool::create(1, bytes, allocator);
		if (!created) {
			throw Failure(VISION_RUNTIME_BACKEND_STATUS_BACKEND_ERROR, created.status().toString());
		}
		pool = std::move(created).value();
	}
	auto buffer = pool->acquire();
	if (!buffer) {
		throw Failure(VISION_RUNTIME_BACKEND_STATUS_BACKEND_ERROR, buffer.status().toString());
	}
	return std::move(buffer).value();
}

VisionRuntimeBackendStatus infer(void* handle, const VisionRuntimeTensorView* inputs,
	std::size_t count, VisionRuntimeOutputAllocator outputs,
	VisionRuntimeErrorBuffer error) noexcept {
	return boundary(error, [&] {
		auto& backend = *static_cast<Backend*>(handle);
		const auto& input = singleInput(inputs, count, backend.inputName);
		require(input.rank <= nvinfer1::Dims::MAX_DIMS, VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT,
			"TensorRT input rank exceeds supported dimensions");
		nvinfer1::Dims dimensions{};
		dimensions.nbDims = static_cast<int32_t>(input.rank);
		for (std::size_t index = 0; index < input.rank; ++index) {
			dimensions.d[index] = input.dimensions[index];
		}
		// Pick a worker: round-robin over the pool, taking the first free one;
		// if all are busy, block on the round-robin slot. With streams == 1
		// this degenerates to the original single-context serialization.
		const auto workerCount = backend.workers.size();
		const auto start = backend.nextWorker.fetch_add(1,
			std::memory_order_relaxed) % workerCount;
		Worker* worker = nullptr;
		std::unique_lock<std::mutex> workerLock;
		for (std::size_t attempt = 0; attempt < workerCount; ++attempt) {
			auto& candidate = *backend.workers[(start + attempt) % workerCount];
			auto tryLock = std::unique_lock<std::mutex>(candidate.mutex, std::try_to_lock);
			if (tryLock.owns_lock()) {
				worker = &candidate;
				workerLock = std::move(tryLock);
				break;
			}
		}
		if (worker == nullptr) {
			worker = backend.workers[start].get();
			workerLock = std::unique_lock<std::mutex>(worker->mutex);
		}
		checkCuda(cudaSetDevice(backend.device));
		require(worker->context->setInputShape(backend.inputName.c_str(), dimensions),
			VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT, "input shape is outside the selected profile");
		const auto shape = worker->context->getTensorShape(backend.outputName.c_str());
		require(shape.nbDims >= 0, VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED, "output rank remains dynamic");
		const auto rank = static_cast<std::size_t>(shape.nbDims);
		const auto bytes = tensorBytes(shape.d, rank);
		void* hostOutput = allocateOutput(outputs, backend.outputName, shape.d, rank, bytes);
		auto deviceInput = acquire(worker->inputPool, input.byteSize, backend.allocator);
		auto deviceOutput = acquire(worker->outputPool, bytes, backend.allocator);
		checkTensorRt(worker->context->setTensorAddress(backend.inputName.c_str(), deviceInput.data()) &&
			worker->context->setTensorAddress(backend.outputName.c_str(), deviceOutput.data()),
			backend, "tensor address binding failed");
		try {
			checkCuda(cudaMemcpyAsync(deviceInput.data(), input.data, input.byteSize,
				cudaMemcpyHostToDevice, worker->stream.value));
			checkTensorRt(worker->context->enqueueV3(worker->stream.value), backend, "enqueueV3 failed");
			checkCuda(cudaMemcpyAsync(hostOutput, deviceOutput.data(), bytes,
				cudaMemcpyDeviceToHost, worker->stream.value));
		} catch (...) {
			checkCuda(cudaStreamSynchronize(worker->stream.value));
			throw;
		}
		checkCuda(cudaStreamSynchronize(worker->stream.value));
	});
}

const VisionRuntimeBackendApi api{
	sizeof(VisionRuntimeBackendApi), VISION_RUNTIME_BACKEND_ABI_MAJOR,
	VISION_RUNTIME_BACKEND_ABI_MINOR, {"tensorrt", 8}, create, destroy, infer,
};

} // namespace
} // namespace visionRuntime::plugins

extern "C" VISION_RUNTIME_BACKEND_EXPORT const VisionRuntimeBackendApi*
visionRuntimeBackendQuery(void) {
	return &visionRuntime::plugins::api;
}