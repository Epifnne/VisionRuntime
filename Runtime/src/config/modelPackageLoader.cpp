#include "config/modelPackageLoader.hpp"

#include "logs/logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <fstream>
#include <string>
#include <unordered_set>
#include <utility>

namespace visionRuntime::config {
namespace {

template<typename T>
[[nodiscard]] core::Result<T> failure(core::StatusCode code, std::string message) {
	auto status = core::Status::error(code, std::move(message));
	logs::report(status);
	return core::Result<T>::failure(std::move(status));
}

[[nodiscard]] TensorElementType parseElementType(const std::string& value) {
	if (value != "float32") {
		throw nlohmann::json::other_error::create(
			501, "tensor elementType must be float32", nullptr);
	}
	return TensorElementType::Float32;
}

[[nodiscard]] TensorLayout parseLayout(const std::string& value) {
	if (value == "nchw") {
		return TensorLayout::Nchw;
	}
	if (value == "embedding") {
		return TensorLayout::Embedding;
	}
	if (value == "scalar") {
		return TensorLayout::Scalar;
	}
	throw nlohmann::json::other_error::create(
		501, "tensor layout must be nchw, embedding, or scalar", nullptr);
}

[[nodiscard]] TensorManifest parseTensor(const nlohmann::json& document) {
	TensorManifest tensor{
		.name = document.at("name").get<std::string>(),
		.elementType = parseElementType(document.at("elementType").get<std::string>()),
		.layout = parseLayout(document.at("layout").get<std::string>()),
		.shape = {},
	};
	for (const auto& dimension : document.at("shape")) {
		if (!dimension.is_number_integer() || dimension.get<std::int64_t>() <= 0) {
			throw nlohmann::json::other_error::create(
				501, "tensor dimensions must be positive integers", nullptr);
		}
		tensor.shape.push_back(dimension.get<std::size_t>());
	}
	if (tensor.name.empty() || tensor.shape.empty() ||
		std::find(tensor.shape.begin(), tensor.shape.end(), 0) != tensor.shape.end()) {
		throw nlohmann::json::other_error::create(
			501, "tensor name and positive shape dimensions are required", nullptr);
	}
	return tensor;
}

[[nodiscard]] std::vector<TensorManifest> parseTensors(const nlohmann::json& document) {
	if (!document.is_array() || document.empty()) {
		throw nlohmann::json::other_error::create(
			501, "model inputs and outputs must be nonempty arrays", nullptr);
	}
	std::vector<TensorManifest> tensors;
	std::unordered_set<std::string> names;
	for (const auto& entry : document) {
		auto tensor = parseTensor(entry);
		if (!names.insert(tensor.name).second) {
			throw nlohmann::json::other_error::create(
				501, "tensor names must be unique within each port list", nullptr);
		}
		tensors.push_back(std::move(tensor));
	}
	return tensors;
}

[[nodiscard]] bool isInside(
	const std::filesystem::path& root,
	const std::filesystem::path& candidate) {
	auto rootIterator = root.begin();
	auto candidateIterator = candidate.begin();
	for (; rootIterator != root.end(); ++rootIterator, ++candidateIterator) {
		if (candidateIterator == candidate.end() || *candidateIterator != *rootIterator) {
			return false;
		}
	}
	return true;
}

} // namespace

ModelPackage::ModelPackage(
	std::filesystem::path root,
	std::string id,
	std::string version,
	ModelManifest manifest,
	std::vector<ModelArtifact> artifacts)
	: root_(std::move(root)),
	  id_(std::move(id)),
	  version_(std::move(version)),
	  manifest_(std::move(manifest)),
	  artifacts_(std::move(artifacts)) {}

core::Result<SelectedModelArtifact> ModelPackage::selectArtifact(
	const BackendDeploymentConfig& deployment) const {
	const ModelArtifact* selected = nullptr;
	for (const auto& artifact : artifacts_) {
		const auto supportsDevice = std::find(
			artifact.devices.begin(), artifact.devices.end(), deployment.device) !=
			artifact.devices.end();
		if (artifact.backendId != deployment.id || !supportsDevice) {
			continue;
		}
		if (selected != nullptr) {
			return failure<SelectedModelArtifact>(core::StatusCode::InvalidArgument,
				"model package has multiple artifacts matching the deployment");
		}
		selected = &artifact;
	}
	if (selected == nullptr) {
		return failure<SelectedModelArtifact>(core::StatusCode::NotFound,
			"model package has no artifact matching the deployment");
	}
	return core::Result<SelectedModelArtifact>::success({
		.artifactPath = selected->path,
		.artifactKind = selected->kind,
		.optionsJson = selected->optionsJson,
		.device = deployment.device,
	});
}

core::Result<ModelPackage> ModelPackageLoader::load(
	const std::filesystem::path& root) {
	const auto manifestPath = root / "manifest.json";
	std::ifstream stream(manifestPath);
	if (!stream) {
		return failure<ModelPackage>(core::StatusCode::NotFound,
			"model package manifest could not be opened: " + manifestPath.string());
	}

	try {
		const auto packageRoot = std::filesystem::canonical(root);
		const auto artifactsRoot = std::filesystem::canonical(packageRoot / "artifacts");
		if (!isInside(packageRoot, artifactsRoot)) {
			return failure<ModelPackage>(core::StatusCode::InvalidArgument,
				"artifacts directory must remain inside the package");
		}
		const auto document = nlohmann::json::parse(stream);
		const auto& schemaVersion = document.at("schemaVersion");
		if (schemaVersion.at("major").get<std::uint32_t>() != 1) {
			return failure<ModelPackage>(core::StatusCode::Unsupported,
				"model package schema major version is unsupported");
		}

		const auto packageId = document.at("id").get<std::string>();
		const auto packageVersion = document.at("version").get<std::string>();
		if (packageId.empty() || packageVersion.empty()) {
			return failure<ModelPackage>(core::StatusCode::InvalidArgument,
				"model package id and version must not be empty");
		}
		ModelManifest manifest{
			.inputs = parseTensors(document.at("inputs")),
			.outputs = parseTensors(document.at("outputs")),
		};

		std::unordered_set<std::string> artifactIds;
		std::vector<ModelArtifact> artifacts;
		for (const auto& artifactDocument : document.at("artifacts")) {
			auto id = artifactDocument.at("id").get<std::string>();
			auto backendId = artifactDocument.at("backendId").get<std::string>();
			auto kind = artifactDocument.at("kind").get<std::string>();
			auto devices = artifactDocument.at("devices").get<std::vector<std::string>>();
			if (id.empty() || backendId.empty() || kind.empty() || devices.empty()) {
				return failure<ModelPackage>(core::StatusCode::InvalidArgument,
					"model artifact id, backendId, kind, and devices must not be empty");
			}
			std::unordered_set<std::string> deviceIds;
			for (const auto& device : devices) {
				if (device.empty() || !deviceIds.insert(device).second) {
					return failure<ModelPackage>(core::StatusCode::InvalidArgument,
						"model artifact devices must be unique and not empty");
				}
			}
			if (!artifactIds.insert(id).second) {
				return failure<ModelPackage>(core::StatusCode::InvalidArgument,
					"model package artifact IDs must be unique");
			}
			const auto encodedPath = artifactDocument.at("path").get<std::string>();
			const auto relativePath = std::filesystem::path(std::u8string(
				reinterpret_cast<const char8_t*>(encodedPath.data()), encodedPath.size()));
			if (relativePath.is_absolute()) {
				return failure<ModelPackage>(core::StatusCode::InvalidArgument,
					"model artifact path must be relative to the package");
			}
			const auto artifactPath = std::filesystem::weakly_canonical(
				packageRoot / relativePath);
			if (!isInside(artifactsRoot, artifactPath)) {
				return failure<ModelPackage>(core::StatusCode::InvalidArgument,
					"model artifact path must remain inside the artifacts directory");
			}
			if (!std::filesystem::is_regular_file(artifactPath)) {
				return failure<ModelPackage>(core::StatusCode::NotFound,
					"model artifact file was not found: " + artifactPath.string());
			}
			artifacts.push_back({
				.id = std::move(id),
				.backendId = std::move(backendId),
				.kind = std::move(kind),
				.path = artifactPath,
				.devices = std::move(devices),
				.optionsJson = artifactDocument.value(
					"options", nlohmann::json::object()).dump(),
			});
		}

		return core::Result<ModelPackage>::success(ModelPackage(
			packageRoot,
			packageId,
			packageVersion,
			std::move(manifest), std::move(artifacts)));
	} catch (const std::exception& exception) {
		return failure<ModelPackage>(core::StatusCode::InvalidArgument,
			std::string("invalid model package manifest: ") + exception.what());
	}
}

} // namespace visionRuntime::config
