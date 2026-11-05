#include "pluginSupport.hpp"

#include <openvino/c/openvino.h>

#include <memory>
#include <mutex>

namespace visionRuntime::plugins {
namespace {

template<typename Type, void (*Free)(Type*)>
struct Deleter {
	void operator()(Type* value) const noexcept { Free(value); }
};

template<typename Type, void (*Free)(Type*)>
using Handle = std::unique_ptr<Type, Deleter<Type, Free>>;

void check(ov_status_e status) {
	if (status != OK) {
		const auto* message = ov_get_last_err_msg();
		throw Failure(VISION_RUNTIME_BACKEND_STATUS_BACKEND_ERROR,
			message != nullptr ? message : "OpenVINO operation failed");
	}
}

struct Backend {
	Handle<ov_core_t, ov_core_free> core;
	Handle<ov_compiled_model_t, ov_compiled_model_free> model;
	Handle<ov_infer_request_t, ov_infer_request_free> request;
	std::string inputName;
	std::string outputName;
	std::mutex mutex;
};

struct Shape {
	ov_shape_t value{};
	~Shape() { ov_shape_free(&value); }
};

VisionRuntimeBackendStatus create(const VisionRuntimeBackendCreateOptions* options,
	void** handle, VisionRuntimeErrorBuffer error) noexcept {
	*handle = nullptr;
	return boundary(error, [&] {
		const auto document = parseOptions(*options);
		require(text(options->artifactKind) == "ir" || text(options->artifactKind) == "onnx",
			VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED, "OpenVINO requires ir or onnx artifact");
		const auto path = artifactPath(*options);
		require(std::filesystem::is_regular_file(path), VISION_RUNTIME_BACKEND_STATUS_NOT_FOUND,
			"OpenVINO artifact was not found");
		const std::string device(text(options->device));
		require(device == "CPU" || device == "GPU" || device == "NPU",
			VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT, "OpenVINO device must be CPU, GPU, or NPU");
		const auto threads = document.value("inferenceThreads", 0);
		require(threads >= 0, VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT,
			"inferenceThreads must not be negative");
		const auto dynamicBatch = document.value("dynamicBatch", false);
		auto backend = std::make_unique<Backend>();
		backend->inputName = text(options->inputName);
		backend->outputName = text(options->outputName);
		ov_core_t* core = nullptr;
		check(ov_core_create(&core));
		backend->core.reset(core);

		if (dynamicBatch) {
			// Reshape the model's batch dimension to dynamic before compiling so
			// one compiled model serves any N. Requires reading the model first.
			ov_model_t* rawModel = nullptr;
#if defined(_WIN32)
			check(ov_core_read_model_unicode(core, path.c_str(), nullptr, &rawModel));
#else
			check(ov_core_read_model(core, path.c_str(), nullptr, &rawModel));
#endif
			Handle<ov_model_t, ov_model_free> model(rawModel);
			ov_output_const_port_t* inputPort = nullptr;
			check(ov_model_const_input_by_name(model.get(), backend->inputName.c_str(), &inputPort));
			Handle<ov_output_const_port_t, ov_output_const_port_free> input(inputPort);
			ov_shape_t modelShape{};
			check(ov_port_get_shape(reinterpret_cast<const ov_output_port_t*>(inputPort), &modelShape));
			Shape shapeGuard;
			shapeGuard.value = modelShape;
			require(modelShape.rank >= 1, VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED,
				"OpenVINO dynamic batch requires a ranked input");
			std::vector<ov_dimension_t> dynamicDims(
				modelShape.dims, modelShape.dims + modelShape.rank);
			dynamicDims[0] = {-1, -1};
			ov_partial_shape_t partialShape{};
			check(ov_partial_shape_create(
				static_cast<int64_t>(modelShape.rank), dynamicDims.data(), &partialShape));
			check(ov_model_reshape_input_by_name(model.get(),
				backend->inputName.c_str(), partialShape));
			ov_partial_shape_free(&partialShape);
			ov_compiled_model_t* compiled = nullptr;
			const auto threadCount = std::to_string(threads);
			check(threads == 0
				? ov_core_compile_model(core, model.get(), device.c_str(), 0, &compiled)
				: ov_core_compile_model(core, model.get(), device.c_str(), 2, &compiled,
					ov_property_key_inference_num_threads, threadCount.c_str()));
			backend->model.reset(compiled);
		} else {
			ov_compiled_model_t* compiled = nullptr;
			const auto threadCount = std::to_string(threads);
#if defined(_WIN32)
			const auto compile = ov_core_compile_model_from_file_unicode;
#else
			const auto compile = ov_core_compile_model_from_file;
#endif
			check(threads == 0
				? compile(core, path.c_str(), device.c_str(), 0, &compiled)
				: compile(core, path.c_str(), device.c_str(), 2, &compiled,
					ov_property_key_inference_num_threads, threadCount.c_str()));
			backend->model.reset(compiled);
		}
		std::size_t inputCount = 0;
		std::size_t outputCount = 0;
		auto* model = backend->model.get();
		check(ov_compiled_model_inputs_size(model, &inputCount));
		check(ov_compiled_model_outputs_size(model, &outputCount));
		require(inputCount == 1 && outputCount == 1, VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED,
			"OpenVINO requires exactly one input and one output");
		ov_output_const_port_t* inputPort = nullptr;
		ov_output_const_port_t* outputPort = nullptr;
		check(ov_compiled_model_input_by_name(model, backend->inputName.c_str(), &inputPort));
		Handle<ov_output_const_port_t, ov_output_const_port_free> input(inputPort);
		check(ov_compiled_model_output_by_name(model, backend->outputName.c_str(), &outputPort));
		Handle<ov_output_const_port_t, ov_output_const_port_free> output(outputPort);
		ov_element_type_e inputType = DYNAMIC;
		ov_element_type_e outputType = DYNAMIC;
		check(ov_port_get_element_type(inputPort, &inputType));
		check(ov_port_get_element_type(outputPort, &outputType));
		require(inputType == F32 && outputType == F32, VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED,
			"OpenVINO requires Float32 input and output");
		ov_infer_request_t* request = nullptr;
		check(ov_compiled_model_create_infer_request(model, &request));
		backend->request.reset(request);
		*handle = backend.release();
	});
}

void destroy(void* handle) noexcept { delete static_cast<Backend*>(handle); }

VisionRuntimeBackendStatus infer(void* handle, const VisionRuntimeTensorView* inputs,
	std::size_t count, VisionRuntimeOutputAllocator outputs,
	VisionRuntimeErrorBuffer error) noexcept {
	return boundary(error, [&] {
		auto& backend = *static_cast<Backend*>(handle);
		const auto& input = singleInput(inputs, count, backend.inputName);
		Shape inputShape;
		check(ov_shape_create(static_cast<int64_t>(input.rank), input.dimensions, &inputShape.value));
		ov_tensor_t* rawInput = nullptr;
		check(ov_tensor_create_from_host_ptr(F32, inputShape.value,
			const_cast<void*>(input.data), &rawInput));
		Handle<ov_tensor_t, ov_tensor_free> inputTensor(rawInput);
		std::scoped_lock lock(backend.mutex);
		check(ov_infer_request_set_input_tensor(backend.request.get(), inputTensor.get()));
		check(ov_infer_request_infer(backend.request.get()));
		ov_tensor_t* rawOutput = nullptr;
		check(ov_infer_request_get_output_tensor(backend.request.get(), &rawOutput));
		Handle<ov_tensor_t, ov_tensor_free> outputTensor(rawOutput);
		Shape shape;
		check(ov_tensor_get_shape(rawOutput, &shape.value));
		void* data = nullptr;
		check(ov_tensor_data(rawOutput, &data));
		const auto rank = static_cast<std::size_t>(shape.value.rank);
		const auto bytes = tensorBytes(shape.value.dims, rank);
		void* destination = allocateOutput(outputs, backend.outputName, shape.value.dims, rank, bytes);
		std::memcpy(destination, data, bytes);
	});
}

const VisionRuntimeBackendApi api{
	sizeof(VisionRuntimeBackendApi), VISION_RUNTIME_BACKEND_ABI_MAJOR,
	VISION_RUNTIME_BACKEND_ABI_MINOR, {"openvino", 8}, create, destroy, infer,
};

} // namespace
} // namespace visionRuntime::plugins

extern "C" VISION_RUNTIME_BACKEND_EXPORT const VisionRuntimeBackendApi*
visionRuntimeBackendQuery(void) {
	return &visionRuntime::plugins::api;
}