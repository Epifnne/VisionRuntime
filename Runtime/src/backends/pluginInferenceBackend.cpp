#include "backends/pluginInferenceBackend.hpp"

#include "backends/backendPluginApi.h"
#include "core/dataType.hpp"
#include "logs/logger.hpp"
#include "memory/cpuAllocator.hpp"

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace visionRuntime::backends {
namespace {

[[nodiscard]] core::Status error(core::StatusCode code, std::string message) {
	return core::Status::error(code, std::move(message));
}

[[nodiscard]] std::string makePluginFileName(std::string_view backendId) {
	std::string name = "vision-backend-";
	name.append(backendId);
#if defined(_WIN32)
	name += ".dll";
#elif defined(__APPLE__)
	name += ".dylib";
#else
	name += ".so";
#endif
	return name;
}

class DynamicLibrary {
public:
	~DynamicLibrary() {
#if defined(_WIN32)
		if (handle_ != nullptr) {
			FreeLibrary(handle_);
		}
#else
		if (handle_ != nullptr) {
			dlclose(handle_);
		}
#endif
	}

	DynamicLibrary(const DynamicLibrary&) = delete;
	DynamicLibrary& operator=(const DynamicLibrary&) = delete;
	DynamicLibrary(DynamicLibrary&&) = delete;
	DynamicLibrary& operator=(DynamicLibrary&&) = delete;

	[[nodiscard]] static core::Result<std::unique_ptr<DynamicLibrary>> load(
		const std::filesystem::path& path) {
		auto library = std::unique_ptr<DynamicLibrary>(new DynamicLibrary());
#if defined(_WIN32)
		const auto nativePath = std::filesystem::absolute(path).make_preferred();
		library->handle_ = LoadLibraryExW(nativePath.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
		library->handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
		if (library->handle_ == nullptr) {
#if defined(_WIN32)
			const auto details = " (Windows error " + std::to_string(GetLastError()) + ")";
#else
			const auto details = std::string(": ") + dlerror();
#endif
			return core::Result<std::unique_ptr<DynamicLibrary>>::failure(
				error(core::StatusCode::NotFound,
					"backend plugin could not be loaded: " + path.string() + details));
		}
		return core::Result<std::unique_ptr<DynamicLibrary>>::success(
			std::move(library));
	}

	template<typename Function>
	[[nodiscard]] Function symbol(const char* name) const noexcept {
#if defined(_WIN32)
		const auto address = GetProcAddress(handle_, name);
		Function function = nullptr;
		static_assert(sizeof(function) == sizeof(address));
		std::memcpy(&function, &address, sizeof(function));
		return function;
#else
		return reinterpret_cast<Function>(dlsym(handle_, name));
#endif
	}

private:
	DynamicLibrary() = default;

#if defined(_WIN32)
	HMODULE handle_ = nullptr;
#else
	void* handle_ = nullptr;
#endif
};

[[nodiscard]] VisionRuntimeStringView stringView(std::string_view value) noexcept {
	return {value.data(), value.size()};
}

[[nodiscard]] std::string utf8Path(const std::filesystem::path& path) {
	const auto encoded = path.generic_u8string();
	return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] std::string_view stringView(VisionRuntimeStringView value) noexcept {
	return {value.data, value.size};
}

[[nodiscard]] core::StatusCode statusCode(VisionRuntimeBackendStatus status) noexcept {
	switch (status) {
	case VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT:
		return core::StatusCode::InvalidArgument;
	case VISION_RUNTIME_BACKEND_STATUS_NOT_FOUND:
		return core::StatusCode::NotFound;
	case VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED:
		return core::StatusCode::Unsupported;
	case VISION_RUNTIME_BACKEND_STATUS_BACKEND_ERROR:
		return core::StatusCode::BackendError;
	case VISION_RUNTIME_BACKEND_STATUS_INTERNAL:
	case VISION_RUNTIME_BACKEND_STATUS_OK:
		return core::StatusCode::Internal;
	}
	return core::StatusCode::Internal;
}

[[nodiscard]] std::string pluginError(
	const std::array<char, 1024>& buffer, std::string fallback) {
	if (buffer.front() == '\0') {
		return fallback;
	}
	return buffer.data();
}

struct OutputContext {
	preprocess::TensorMap outputs;
	core::Status status;
};

VisionRuntimeBackendStatus allocateOutput(
	void* rawContext,
	VisionRuntimeStringView name,
	VisionRuntimeBackendDataType dataType,
	const std::int64_t* dimensions,
	std::size_t rank,
	std::size_t byteSize,
	void** data) noexcept {
	auto& context = *static_cast<OutputContext*>(rawContext);
	try {
		if (dataType != VISION_RUNTIME_BACKEND_DATA_TYPE_FLOAT32) {
			context.status = error(
				core::StatusCode::Unsupported, "plugin output data type is unsupported");
			return VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED;
		}
		std::vector<std::int64_t> shape(dimensions, dimensions + rank);
		memory::CpuAllocator allocator;
		auto tensor = allocator.allocateTensor(
			core::DataType::Float32, core::TensorShape(std::move(shape)));
		if (!tensor || tensor->byteSize() != byteSize) {
			context.status = tensor
				? error(core::StatusCode::InvalidArgument,
					"plugin output byte size does not match its shape")
				: tensor.status();
			return VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT;
		}
		auto [iterator, inserted] = context.outputs.emplace(
			std::string(stringView(name)), std::move(tensor).value());
		if (!inserted) {
			context.status = error(
				core::StatusCode::InvalidArgument, "plugin produced a duplicate output name");
			return VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT;
		}
		*data = iterator->second.data();
		return VISION_RUNTIME_BACKEND_STATUS_OK;
	} catch (...) {
		context.status = error(
			core::StatusCode::Internal, "plugin output allocation failed");
		return VISION_RUNTIME_BACKEND_STATUS_INTERNAL;
	}
}

} // namespace

std::filesystem::path backendPluginPath(
	const std::filesystem::path& pluginDirectory, std::string_view backendId) {
	const std::string id(backendId);
	return pluginDirectory / id / makePluginFileName(id);
}

class PluginInferenceBackend::Impl {
public:
	~Impl() {
		if (backend != nullptr) {
			api->destroy(backend);
		}
	}

	std::unique_ptr<DynamicLibrary> library;
	const VisionRuntimeBackendApi* api = nullptr;
	void* backend = nullptr;
};

core::Result<std::unique_ptr<PluginInferenceBackend>>
PluginInferenceBackend::createUnchecked(PluginBackendOptions options) {
	auto loaded = DynamicLibrary::load(options.pluginPath);
	if (!loaded) {
		return core::Result<std::unique_ptr<PluginInferenceBackend>>::failure(
			loaded.status());
	}
	auto library = std::move(loaded).value();
	const auto query = library->symbol<VisionRuntimeBackendQueryFunction>(
		VISION_RUNTIME_BACKEND_ENTRY_POINT);
	if (query == nullptr) {
		return core::Result<std::unique_ptr<PluginInferenceBackend>>::failure(error(
			core::StatusCode::InvalidArgument,
			"backend plugin does not export " VISION_RUNTIME_BACKEND_ENTRY_POINT));
	}
	const auto* api = query();
	if (api == nullptr || api->structSize < VISION_RUNTIME_BACKEND_API_V1_SIZE ||
		api->abiMajor != VISION_RUNTIME_BACKEND_ABI_MAJOR ||
		api->abiMinor > VISION_RUNTIME_BACKEND_ABI_MINOR) {
		return core::Result<std::unique_ptr<PluginInferenceBackend>>::failure(error(
			core::StatusCode::Unsupported, "backend plugin ABI is incompatible"));
	}
	if (api->create == nullptr || api->destroy == nullptr || api->infer == nullptr) {
		return core::Result<std::unique_ptr<PluginInferenceBackend>>::failure(error(
			core::StatusCode::InvalidArgument,
			"backend plugin function table is incomplete"));
	}
	if (stringView(api->backendId) != options.backendId) {
		return core::Result<std::unique_ptr<PluginInferenceBackend>>::failure(error(
			core::StatusCode::InvalidArgument, "backend plugin ID does not match"));
	}

	const auto artifactPath = utf8Path(options.artifactPath);
	VisionRuntimeBackendCreateOptions createOptions{
		.structSize = sizeof(VisionRuntimeBackendCreateOptions),
		.artifactPath = stringView(artifactPath),
		.device = stringView(options.device),
		.optionsJson = stringView(options.optionsJson),
		.artifactKind = stringView(options.artifactKind),
		.inputName = stringView(options.inputName),
		.outputName = stringView(options.outputName),
	};
	std::array<char, 1024> errorBuffer{};
	void* backend = nullptr;
	const auto status = api->create(
		&createOptions, &backend, {errorBuffer.data(), errorBuffer.size() - 1});
	if (status != VISION_RUNTIME_BACKEND_STATUS_OK) {
		return core::Result<std::unique_ptr<PluginInferenceBackend>>::failure(error(
			statusCode(status), pluginError(errorBuffer, "backend plugin creation failed")));
	}
	if (backend == nullptr) {
		return core::Result<std::unique_ptr<PluginInferenceBackend>>::failure(error(
			core::StatusCode::Internal,
			"backend plugin returned success without a backend handle"));
	}

	auto impl = std::make_unique<Impl>();
	impl->library = std::move(library);
	impl->api = api;
	impl->backend = backend;
	return core::Result<std::unique_ptr<PluginInferenceBackend>>::success(
		std::unique_ptr<PluginInferenceBackend>(
			new PluginInferenceBackend(std::move(impl))));
}

core::Result<std::unique_ptr<PluginInferenceBackend>>
PluginInferenceBackend::create(PluginBackendOptions options) {
	auto backend = createUnchecked(std::move(options));
	if (!backend) {
		logs::report(backend.status());
	}
	return backend;
}

PluginInferenceBackend::PluginInferenceBackend(std::unique_ptr<Impl> impl)
	: impl_(std::move(impl)) {}

PluginInferenceBackend::~PluginInferenceBackend() = default;

core::Result<preprocess::TensorMap> PluginInferenceBackend::infer(
	const preprocess::TensorMap& inputs) {
	std::vector<VisionRuntimeTensorView> inputViews;
	inputViews.reserve(inputs.size());
	for (const auto& [name, tensor] : inputs) {
		if (tensor.dataType() != core::DataType::Float32 ||
			tensor.memoryKind() != core::MemoryKind::Host || !tensor.isContiguous()) {
			return core::Result<preprocess::TensorMap>::failure(error(
				core::StatusCode::Unsupported,
				"plugin input must be a contiguous host Float32 tensor"));
		}
		inputViews.push_back({
			.name = stringView(name),
			.dataType = VISION_RUNTIME_BACKEND_DATA_TYPE_FLOAT32,
			.dimensions = tensor.shape().dimensions().data(),
			.rank = tensor.shape().rank(),
			.data = tensor.data(),
			.byteSize = tensor.byteSize(),
		});
	}

	OutputContext outputContext;
	std::array<char, 1024> errorBuffer{};
	const auto status = impl_->api->infer(
		impl_->backend, inputViews.data(), inputViews.size(),
		{&outputContext, allocateOutput},
		{errorBuffer.data(), errorBuffer.size() - 1});
	if (status != VISION_RUNTIME_BACKEND_STATUS_OK) {
		if (!outputContext.status) {
			return core::Result<preprocess::TensorMap>::failure(outputContext.status);
		}
		return core::Result<preprocess::TensorMap>::failure(error(
			statusCode(status), pluginError(errorBuffer, "backend plugin inference failed")));
	}
	return core::Result<preprocess::TensorMap>::success(
		std::move(outputContext.outputs));
}

} // namespace visionRuntime::backends
