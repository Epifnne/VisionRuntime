#include "backends/tensorRtBackend.hpp"

#include "core/dataType.hpp"
#include "memory/cpuBufferPool.hpp"
#include "memory/gpuAllocator.hpp"
#include "memory/gpuBufferPool.hpp"

#include <NvInfer.h>
#include <NvInferPlugin.h>
#include <cuda_runtime_api.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace visionRuntime::backends {
namespace {

[[nodiscard]] core::Status error(core::StatusCode code, std::string message) {
	return core::Status::error(code, std::move(message));
}

[[nodiscard]] core::Status cudaError(const char* operation, cudaError_t status) {
	std::ostringstream message;
	message << operation << " failed: " << cudaGetErrorString(status);
	return error(core::StatusCode::BackendError, message.str());
}

class TensorRtLogger final : public nvinfer1::ILogger {
public:
	void log(Severity severity, const char* message) noexcept override {
		if (severity <= Severity::kERROR && message != nullptr) {
			try {
				std::scoped_lock lock(mutex_);
				lastError_ = message;
			} catch (...) {
			}
		}
	}

	[[nodiscard]] std::string lastError() const {
		std::scoped_lock lock(mutex_);
		return lastError_;
	}

private:
	mutable std::mutex mutex_;
	std::string lastError_;
};

class CudaStream {
public:
	CudaStream() = default;
	~CudaStream() {
		if (stream_ != nullptr) {
			cudaStreamDestroy(stream_);
		}
	}

	CudaStream(const CudaStream&) = delete;
	CudaStream& operator=(const CudaStream&) = delete;

	[[nodiscard]] cudaError_t create() noexcept {
		return cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking);
	}

	[[nodiscard]] cudaStream_t get() const noexcept { return stream_; }

private:
	cudaStream_t stream_ = nullptr;
};

[[nodiscard]] core::Result<nvinfer1::Dims> toTensorRtDimensions(
	const core::TensorShape& shape) {
	if (!shape.isConcrete() || shape.rank() > static_cast<std::size_t>(nvinfer1::Dims::MAX_DIMS)) {
		return core::Result<nvinfer1::Dims>::failure(error(
			core::StatusCode::InvalidArgument,
			"TensorRT input shape must be concrete and within the supported rank"));
	}

	nvinfer1::Dims dimensions{};
	dimensions.nbDims = static_cast<std::int32_t>(shape.rank());
	for (std::size_t index = 0; index < shape.rank(); ++index) {
		const auto dimension = shape.dimensions()[index];
		if (dimension > std::numeric_limits<std::int32_t>::max()) {
			return core::Result<nvinfer1::Dims>::failure(error(
				core::StatusCode::InvalidArgument,
				"TensorRT input dimension exceeds int32 range"));
		}
		dimensions.d[index] = static_cast<std::int32_t>(dimension);
	}
	return core::Result<nvinfer1::Dims>::success(dimensions);
}

[[nodiscard]] core::Result<core::TensorShape> fromTensorRtDimensions(
	const nvinfer1::Dims& dimensions) {
	if (dimensions.nbDims < 0) {
		return core::Result<core::TensorShape>::failure(error(
			core::StatusCode::BackendError,
			"TensorRT output rank is unresolved"));
	}
	std::vector<std::int64_t> shape;
	shape.reserve(static_cast<std::size_t>(dimensions.nbDims));
	for (std::int32_t index = 0; index < dimensions.nbDims; ++index) {
		if (dimensions.d[index] <= 0) {
			return core::Result<core::TensorShape>::failure(error(
				core::StatusCode::BackendError,
				"TensorRT output shape remains dynamic after input shape resolution"));
		}
		shape.push_back(dimensions.d[index]);
	}
	return core::Result<core::TensorShape>::success(core::TensorShape(std::move(shape)));
}

[[nodiscard]] bool isSupportedInput(const core::Tensor& tensor) noexcept {
	return tensor.dataType() == core::DataType::Float32 &&
		tensor.isContiguous() && tensor.bytes().size() == tensor.byteSize();
}

} // namespace

class TensorRtBackend::Impl {
public:
	~Impl() { static_cast<void>(cudaSetDevice(deviceIndex)); }

	TensorRtLogger logger;
	std::unique_ptr<nvinfer1::IRuntime> runtime;
	std::unique_ptr<nvinfer1::ICudaEngine> engine;
	std::unique_ptr<nvinfer1::IExecutionContext> context;
	CudaStream stream;
	std::shared_ptr<memory::GpuAllocator> gpuAllocator;
	std::optional<memory::GpuBufferPool> inputPool;
	std::optional<memory::GpuBufferPool> outputPool;
	std::optional<memory::CpuBufferPool> hostOutputPool;
	std::mutex inferMutex;
	int deviceIndex = 0;
};

core::Result<std::unique_ptr<TensorRtBackend>> TensorRtBackend::create(
	TensorRtBackendOptions options) {
	if (options.enginePath.empty() || !std::filesystem::is_regular_file(options.enginePath)) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::NotFound, "TensorRT engine file was not found"));
	}
	if (options.inputName.empty() || options.outputName.empty()) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::InvalidArgument,
			"TensorRT tensor map names must not be empty"));
	}
	if (options.deviceIndex < 0) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::InvalidArgument,
			"TensorRT CUDA device index must not be negative"));
	}
	if (options.outputBufferCount == 0) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::InvalidArgument,
			"TensorRT output buffer count must be greater than zero"));
	}

	std::ifstream engineFile(options.enginePath, std::ios::binary | std::ios::ate);
	if (!engineFile) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::NotFound, "TensorRT engine file could not be opened"));
	}
	const auto fileSize = engineFile.tellg();
	if (fileSize <= 0) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::InvalidArgument, "TensorRT engine file is empty"));
	}
	std::vector<char> serializedEngine(static_cast<std::size_t>(fileSize));
	engineFile.seekg(0, std::ios::beg);
	if (!engineFile.read(serializedEngine.data(), fileSize)) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::BackendError, "TensorRT engine file could not be read"));
	}

	auto impl = std::make_unique<Impl>();
	impl->deviceIndex = options.deviceIndex;
	impl->gpuAllocator = std::make_shared<memory::GpuAllocator>(options.deviceIndex);
	auto cudaStatus = cudaSetDevice(options.deviceIndex);
	if (cudaStatus != cudaSuccess) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(
			cudaError("CUDA device selection", cudaStatus));
	}
	impl->runtime.reset(nvinfer1::createInferRuntime(impl->logger));
	if (!impl->runtime) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::BackendError, "TensorRT runtime creation failed"));
	}
	if (!initLibNvInferPlugins(&impl->logger, "")) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::BackendError,
			"TensorRT plugin registry initialization failed"));
	}
	impl->engine.reset(impl->runtime->deserializeCudaEngine(
		serializedEngine.data(), serializedEngine.size()));
	if (!impl->engine) {
		std::string message = "TensorRT engine deserialization failed";
		const auto lastError = impl->logger.lastError();
		if (!lastError.empty()) {
			message += ": " + lastError;
		}
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(
			error(core::StatusCode::BackendError, std::move(message)));
	}
	if (impl->engine->getNbIOTensors() != 2) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::Unsupported,
			"initial TensorRT backend requires exactly one input and one output"));
	}
	if (impl->engine->getTensorIOMode(options.inputName.c_str()) !=
			nvinfer1::TensorIOMode::kINPUT ||
		impl->engine->getTensorIOMode(options.outputName.c_str()) !=
			nvinfer1::TensorIOMode::kOUTPUT) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::InvalidArgument,
			"TensorRT input or output tensor name does not match the engine"));
	}
	if (impl->engine->getTensorDataType(options.inputName.c_str()) !=
			nvinfer1::DataType::kFLOAT ||
		impl->engine->getTensorDataType(options.outputName.c_str()) !=
			nvinfer1::DataType::kFLOAT) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::Unsupported,
			"initial TensorRT backend requires Float32 input and output"));
	}
	if (options.optimizationProfile >=
		static_cast<std::size_t>(impl->engine->getNbOptimizationProfiles())) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::InvalidArgument,
			"TensorRT optimization profile index is out of range"));
	}
	const auto profileIndex = static_cast<std::int32_t>(options.optimizationProfile);
	if (impl->engine->isShapeInferenceIO(options.inputName.c_str()) ||
		impl->engine->isShapeInferenceIO(options.outputName.c_str()) ||
		impl->engine->getTensorLocation(options.inputName.c_str()) !=
			nvinfer1::TensorLocation::kDEVICE ||
		impl->engine->getTensorLocation(options.outputName.c_str()) !=
			nvinfer1::TensorLocation::kDEVICE ||
		impl->engine->getTensorFormat(options.inputName.c_str(), profileIndex) !=
			nvinfer1::TensorFormat::kLINEAR ||
		impl->engine->getTensorFormat(options.outputName.c_str(), profileIndex) !=
			nvinfer1::TensorFormat::kLINEAR ||
		impl->engine->getTensorVectorizedDim(
			options.inputName.c_str(), profileIndex) != -1 ||
		impl->engine->getTensorVectorizedDim(
			options.outputName.c_str(), profileIndex) != -1) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::Unsupported,
			"initial TensorRT backend requires linear device execution tensors"));
	}

	impl->context.reset(impl->engine->createExecutionContext());
	if (!impl->context) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::BackendError,
			"TensorRT execution context creation failed"));
	}
	cudaStatus = impl->stream.create();
	if (cudaStatus != cudaSuccess) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(
			cudaError("CUDA stream creation", cudaStatus));
	}
	if (!impl->context->setOptimizationProfileAsync(
			profileIndex, impl->stream.get())) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(error(
			core::StatusCode::BackendError,
			"TensorRT optimization profile selection failed"));
	}
	cudaStatus = cudaStreamSynchronize(impl->stream.get());
	if (cudaStatus != cudaSuccess) {
		return core::Result<std::unique_ptr<TensorRtBackend>>::failure(
			cudaError("CUDA profile synchronization", cudaStatus));
	}

	return core::Result<std::unique_ptr<TensorRtBackend>>::success(
		std::unique_ptr<TensorRtBackend>(
			new TensorRtBackend(std::move(options), std::move(impl))));
}

TensorRtBackend::TensorRtBackend(
	TensorRtBackendOptions options, std::unique_ptr<Impl> impl)
	: options_(std::move(options)), impl_(std::move(impl)) {}

TensorRtBackend::~TensorRtBackend() = default;

core::Result<preprocess::TensorMap> TensorRtBackend::infer(
	const preprocess::TensorMap& inputs) {
	const auto iterator = inputs.find(options_.inputName);
	if (iterator == inputs.end()) {
		return core::Result<preprocess::TensorMap>::failure(error(
			core::StatusCode::InvalidArgument,
			"TensorRT input tensor was not found: " + options_.inputName));
	}
	const auto& input = iterator->second;
	if (!isSupportedInput(input)) {
		return core::Result<preprocess::TensorMap>::failure(error(
			core::StatusCode::Unsupported,
			"TensorRT input must be a contiguous host Float32 tensor"));
	}
	auto dimensions = toTensorRtDimensions(input.shape());
	if (!dimensions) {
		return core::Result<preprocess::TensorMap>::failure(dimensions.status());
	}

	std::scoped_lock lock(impl_->inferMutex);
	auto cudaStatus = cudaSetDevice(options_.deviceIndex);
	if (cudaStatus != cudaSuccess) {
		return core::Result<preprocess::TensorMap>::failure(
			cudaError("CUDA device selection", cudaStatus));
	}
	if (!impl_->context->setInputShape(options_.inputName.c_str(), dimensions.value())) {
		return core::Result<preprocess::TensorMap>::failure(error(
			core::StatusCode::InvalidArgument,
			"TensorRT input shape is outside the selected optimization profile"));
	}
	auto outputShape = fromTensorRtDimensions(
		impl_->context->getTensorShape(options_.outputName.c_str()));
	if (!outputShape) {
		return core::Result<preprocess::TensorMap>::failure(outputShape.status());
	}
	auto resolvedOutputShape = std::move(outputShape).value();
	auto outputByteSize = core::Tensor::requiredByteSize(
		core::DataType::Float32, resolvedOutputShape);
	if (!outputByteSize) {
		return core::Result<preprocess::TensorMap>::failure(outputByteSize.status());
	}
	if (!impl_->hostOutputPool ||
		impl_->hostOutputPool->bufferCapacity() < outputByteSize.value()) {
		auto pool = memory::CpuBufferPool::create(
			options_.outputBufferCount, outputByteSize.value());
		if (!pool) {
			return core::Result<preprocess::TensorMap>::failure(pool.status());
		}
		impl_->hostOutputPool = std::move(pool).value();
	}
	auto hostOutputBuffer = impl_->hostOutputPool->acquire();
	if (!hostOutputBuffer) {
		return core::Result<preprocess::TensorMap>::failure(hostOutputBuffer.status());
	}
	auto output = core::Tensor::wrap(
		std::move(hostOutputBuffer).value(), core::DataType::Float32,
		std::move(resolvedOutputShape));
	if (!output) {
		return core::Result<preprocess::TensorMap>::failure(output.status());
	}

	if (!impl_->inputPool ||
		impl_->inputPool->bufferCapacity() < input.byteSize()) {
		auto pool = memory::GpuBufferPool::create(
			1, input.byteSize(), impl_->gpuAllocator);
		if (!pool) {
			return core::Result<preprocess::TensorMap>::failure(pool.status());
		}
		impl_->inputPool = std::move(pool).value();
	}
	if (!impl_->outputPool ||
		impl_->outputPool->bufferCapacity() < output->byteSize()) {
		auto pool = memory::GpuBufferPool::create(
			1, output->byteSize(), impl_->gpuAllocator);
		if (!pool) {
			return core::Result<preprocess::TensorMap>::failure(pool.status());
		}
		impl_->outputPool = std::move(pool).value();
	}
	auto inputBuffer = impl_->inputPool->acquire();
	if (!inputBuffer) {
		return core::Result<preprocess::TensorMap>::failure(inputBuffer.status());
	}
	auto outputBuffer = impl_->outputPool->acquire();
	if (!outputBuffer) {
		return core::Result<preprocess::TensorMap>::failure(outputBuffer.status());
	}
	if (!impl_->context->setTensorAddress(
			options_.inputName.c_str(), inputBuffer->data()) ||
		!impl_->context->setTensorAddress(
			options_.outputName.c_str(), outputBuffer->data())) {
		return core::Result<preprocess::TensorMap>::failure(error(
			core::StatusCode::BackendError,
			"TensorRT tensor address binding failed"));
	}

	cudaStatus = cudaMemcpyAsync(inputBuffer->data(), input.data(),
		input.byteSize(), cudaMemcpyHostToDevice, impl_->stream.get());
	if (cudaStatus != cudaSuccess) {
		return core::Result<preprocess::TensorMap>::failure(
			cudaError("CUDA input copy", cudaStatus));
	}
	if (!impl_->context->enqueueV3(impl_->stream.get())) {
		const auto synchronizeStatus = cudaStreamSynchronize(impl_->stream.get());
		if (synchronizeStatus != cudaSuccess) {
			return core::Result<preprocess::TensorMap>::failure(
				cudaError("CUDA stream recovery synchronization", synchronizeStatus));
		}
		return core::Result<preprocess::TensorMap>::failure(error(
			core::StatusCode::BackendError, "TensorRT enqueueV3 failed"));
	}
	cudaStatus = cudaMemcpyAsync(output->data(), outputBuffer->data(),
		output->byteSize(), cudaMemcpyDeviceToHost, impl_->stream.get());
	if (cudaStatus != cudaSuccess) {
		const auto copyStatus = cudaStatus;
		const auto synchronizeStatus = cudaStreamSynchronize(impl_->stream.get());
		if (synchronizeStatus != cudaSuccess) {
			return core::Result<preprocess::TensorMap>::failure(
				cudaError("CUDA stream recovery synchronization", synchronizeStatus));
		}
		return core::Result<preprocess::TensorMap>::failure(
			cudaError("CUDA output copy", copyStatus));
	}
	cudaStatus = cudaStreamSynchronize(impl_->stream.get());
	if (cudaStatus != cudaSuccess) {
		return core::Result<preprocess::TensorMap>::failure(
			cudaError("CUDA output copy", cudaStatus));
	}

	preprocess::TensorMap outputs;
	outputs.emplace(options_.outputName, std::move(output).value());
	return core::Result<preprocess::TensorMap>::success(std::move(outputs));
}

} // namespace visionRuntime::backends
