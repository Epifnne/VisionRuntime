#include "backends/openVinoBackend.hpp"

#include "core/dataType.hpp"
#include "core/tensor.hpp"
#include "memory/cpuBufferPool.hpp"

#include <openvino/c/openvino.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace visionRuntime::backends {
namespace {

template<typename T, void (*FreeFunction)(T*)>
struct OpenVinoDeleter {
	void operator()(T* value) const noexcept { FreeFunction(value); }
};

using CorePtr = std::unique_ptr<ov_core_t, OpenVinoDeleter<ov_core_t, ov_core_free>>;
using CompiledModelPtr = std::unique_ptr<ov_compiled_model_t,
	OpenVinoDeleter<ov_compiled_model_t, ov_compiled_model_free>>;
using InferRequestPtr = std::unique_ptr<ov_infer_request_t,
	OpenVinoDeleter<ov_infer_request_t, ov_infer_request_free>>;
using TensorPtr = std::unique_ptr<ov_tensor_t,
	OpenVinoDeleter<ov_tensor_t, ov_tensor_free>>;

[[nodiscard]] core::Status error(core::StatusCode code, std::string message) {
	return core::Status::error(code, std::move(message));
}

[[nodiscard]] core::Status openVinoError(
	const char* operation, ov_status_e status) {
	std::ostringstream message;
	message << operation << " failed";
	const auto* details = ov_get_last_err_msg();
	if (details != nullptr && *details != '\0') {
		message << ": " << details;
	}
	message << " (" << static_cast<int>(status) << ')';
	return error(core::StatusCode::BackendError, message.str());
}

[[nodiscard]] bool isSupportedInput(const core::Tensor& tensor) {
	return tensor.dataType() == core::DataType::Float32 &&
		tensor.memoryKind() == core::MemoryKind::Host && tensor.isContiguous() &&
		tensor.data() != nullptr;
}

} // namespace

class OpenVinoBackend::Impl {
public:
	CorePtr core;
	CompiledModelPtr compiledModel;
	InferRequestPtr inferRequest;
	std::optional<memory::CpuBufferPool> outputPool;
	std::mutex inferMutex;
};

core::Result<std::unique_ptr<OpenVinoBackend>> OpenVinoBackend::create(
	OpenVinoBackendOptions options) {
	if (options.modelPath.empty() || !std::filesystem::is_regular_file(options.modelPath)) {
		return core::Result<std::unique_ptr<OpenVinoBackend>>::failure(
			error(core::StatusCode::NotFound, "OpenVINO model file was not found"));
	}
	if (options.device.empty() || options.inputName.empty() || options.outputName.empty()) {
		return core::Result<std::unique_ptr<OpenVinoBackend>>::failure(error(
			core::StatusCode::InvalidArgument,
			"OpenVINO device and tensor map names must not be empty"));
	}
	if (options.outputBufferCount == 0) {
		return core::Result<std::unique_ptr<OpenVinoBackend>>::failure(error(
			core::StatusCode::InvalidArgument,
			"OpenVINO output buffer count must be greater than zero"));
	}

	auto impl = std::make_unique<Impl>();
	ov_core_t* rawCore = nullptr;
	auto status = ov_core_create(&rawCore);
	if (status != OK) {
		return core::Result<std::unique_ptr<OpenVinoBackend>>::failure(
			openVinoError("OpenVINO core creation", status));
	}
	impl->core.reset(rawCore);

	ov_compiled_model_t* rawCompiledModel = nullptr;
	const auto modelPath = std::filesystem::absolute(options.modelPath).string();
	const auto inferenceThreads = std::to_string(options.inferenceThreads);
	status = options.inferenceThreads == 0
		? ov_core_compile_model_from_file(
			impl->core.get(), modelPath.c_str(), options.device.c_str(), 0,
			&rawCompiledModel)
		: ov_core_compile_model_from_file(
			impl->core.get(), modelPath.c_str(), options.device.c_str(), 2,
			&rawCompiledModel, ov_property_key_inference_num_threads,
			inferenceThreads.c_str());
	if (status != OK) {
		return core::Result<std::unique_ptr<OpenVinoBackend>>::failure(
			openVinoError("OpenVINO model compilation", status));
	}
	impl->compiledModel.reset(rawCompiledModel);

	std::size_t inputCount = 0;
	std::size_t outputCount = 0;
	status = ov_compiled_model_inputs_size(impl->compiledModel.get(), &inputCount);
	if (status == OK) {
		status = ov_compiled_model_outputs_size(impl->compiledModel.get(), &outputCount);
	}
	if (status != OK) {
		return core::Result<std::unique_ptr<OpenVinoBackend>>::failure(
			openVinoError("OpenVINO model port inspection", status));
	}
	if (inputCount != 1 || outputCount != 1) {
		return core::Result<std::unique_ptr<OpenVinoBackend>>::failure(error(
			core::StatusCode::Unsupported,
			"initial OpenVINO backend requires exactly one input and one output"));
	}

	ov_infer_request_t* rawInferRequest = nullptr;
	status = ov_compiled_model_create_infer_request(
		impl->compiledModel.get(), &rawInferRequest);
	if (status != OK) {
		return core::Result<std::unique_ptr<OpenVinoBackend>>::failure(
			openVinoError("OpenVINO infer request creation", status));
	}
	impl->inferRequest.reset(rawInferRequest);

	return core::Result<std::unique_ptr<OpenVinoBackend>>::success(
		std::unique_ptr<OpenVinoBackend>(
			new OpenVinoBackend(std::move(options), std::move(impl))));
}

OpenVinoBackend::OpenVinoBackend(
	OpenVinoBackendOptions options, std::unique_ptr<Impl> impl)
	: options_(std::move(options)), impl_(std::move(impl)) {}

OpenVinoBackend::~OpenVinoBackend() = default;

core::Result<preprocess::TensorMap> OpenVinoBackend::infer(
	const preprocess::TensorMap& inputs) {
	const auto iterator = inputs.find(options_.inputName);
	if (iterator == inputs.end()) {
		return core::Result<preprocess::TensorMap>::failure(error(
			core::StatusCode::InvalidArgument,
			"OpenVINO input tensor was not found: " + options_.inputName));
	}
	const auto& input = iterator->second;
	if (!isSupportedInput(input)) {
		return core::Result<preprocess::TensorMap>::failure(error(
			core::StatusCode::Unsupported,
			"OpenVINO input must be a contiguous host Float32 tensor"));
	}

	std::vector<std::int64_t> dimensions = input.shape().dimensions();
	ov_shape_t inputShape{};
	auto status = ov_shape_create(dimensions.size(), dimensions.data(), &inputShape);
	if (status != OK) {
		return core::Result<preprocess::TensorMap>::failure(
			openVinoError("OpenVINO input shape creation", status));
	}
	ov_tensor_t* rawInputTensor = nullptr;
	status = ov_tensor_create_from_host_ptr(
		F32, inputShape, const_cast<void*>(input.data()), &rawInputTensor);
	ov_shape_free(&inputShape);
	if (status != OK) {
		return core::Result<preprocess::TensorMap>::failure(
			openVinoError("OpenVINO input tensor creation", status));
	}
	TensorPtr inputTensor(rawInputTensor);

	std::scoped_lock lock(impl_->inferMutex);
	status = ov_infer_request_set_input_tensor(impl_->inferRequest.get(), inputTensor.get());
	if (status == OK) {
		status = ov_infer_request_infer(impl_->inferRequest.get());
	}
	if (status != OK) {
		return core::Result<preprocess::TensorMap>::failure(
			openVinoError("OpenVINO synchronous inference", status));
	}

	ov_tensor_t* rawOutputTensor = nullptr;
	status = ov_infer_request_get_output_tensor(
		impl_->inferRequest.get(), &rawOutputTensor);
	if (status != OK) {
		return core::Result<preprocess::TensorMap>::failure(
			openVinoError("OpenVINO output tensor access", status));
	}
	TensorPtr outputTensor(rawOutputTensor);

	ov_element_type_e outputType = DYNAMIC;
	ov_shape_t outputShape{};
	void* outputData = nullptr;
	status = ov_tensor_get_element_type(outputTensor.get(), &outputType);
	if (status == OK) {
		status = ov_tensor_get_shape(outputTensor.get(), &outputShape);
	}
	if (status == OK) {
		status = ov_tensor_data(outputTensor.get(), &outputData);
	}
	if (status != OK) {
		ov_shape_free(&outputShape);
		return core::Result<preprocess::TensorMap>::failure(
			openVinoError("OpenVINO output tensor read", status));
	}
	if (outputType != F32 || outputData == nullptr) {
		ov_shape_free(&outputShape);
		return core::Result<preprocess::TensorMap>::failure(error(
			core::StatusCode::Unsupported,
			"initial OpenVINO backend requires a Float32 output"));
	}

	std::vector<std::int64_t> outputDimensions(
		outputShape.dims, outputShape.dims + outputShape.rank);
	ov_shape_free(&outputShape);
	core::TensorShape resolvedOutputShape(std::move(outputDimensions));
	auto outputByteSize = core::Tensor::requiredByteSize(
		core::DataType::Float32, resolvedOutputShape);
	if (!outputByteSize) {
		return core::Result<preprocess::TensorMap>::failure(outputByteSize.status());
	}
	if (!impl_->outputPool ||
		impl_->outputPool->bufferCapacity() < outputByteSize.value()) {
		auto pool = memory::CpuBufferPool::create(
			options_.outputBufferCount, outputByteSize.value());
		if (!pool) {
			return core::Result<preprocess::TensorMap>::failure(pool.status());
		}
		impl_->outputPool = std::move(pool).value();
	}
	auto outputBuffer = impl_->outputPool->acquire();
	if (!outputBuffer) {
		return core::Result<preprocess::TensorMap>::failure(outputBuffer.status());
	}
	auto output = core::Tensor::wrap(
		std::move(outputBuffer).value(), core::DataType::Float32,
		std::move(resolvedOutputShape));
	if (!output) {
		return core::Result<preprocess::TensorMap>::failure(output.status());
	}
	std::memcpy(output->data(), outputData, output->byteSize());

	preprocess::TensorMap outputs;
	outputs.emplace(options_.outputName, std::move(output).value());
	return core::Result<preprocess::TensorMap>::success(std::move(outputs));
}

} // namespace visionRuntime::backends