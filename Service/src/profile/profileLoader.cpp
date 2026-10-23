#include "profile/profileLoader.hpp"
#include "logs/logger.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <fstream>
#include <string>
#include <utility>
#include <unordered_set>
#include <cmath>

namespace visionService::profile {

namespace {

namespace core = visionRuntime::core;

[[nodiscard]] core::Result<ProductProfile> invalid(
	core::StatusCode code, std::string message) {
	auto status = core::Status::error(code, std::move(message));
	visionRuntime::logs::report(status);
	return core::Result<ProductProfile>::failure(std::move(status));
}

[[nodiscard]] std::size_t sizeValue(const nlohmann::json& object,
	const char* field, std::size_t fallback, bool allowZero = false) {
	const auto value = object.value(field, nlohmann::json(fallback));
	if (!value.is_number_integer() || value.get<std::int64_t>() < 0 ||
		(!allowZero && value.get<std::int64_t>() == 0)) {
		throw nlohmann::json::other_error::create(501,
			std::string(field) + " must be a " +
				(allowZero ? "nonnegative" : "positive") + " integer", &object);
	}
	return value.get<std::size_t>();
}

[[nodiscard]] std::chrono::milliseconds millisecondsValue(
	const nlohmann::json& value) {
	return std::chrono::milliseconds(value.get<std::int64_t>());
}

[[nodiscard]] std::vector<std::string> stringList(
	const nlohmann::json& value,
	const char* fieldName) {
	if (!value.is_array()) {
		throw nlohmann::json::type_error::create(
			302, std::string(fieldName) + " must be an array", &value);
	}
	std::vector<std::string> result;
	result.reserve(value.size());
	for (const auto& item : value) {
		result.push_back(item.get<std::string>());
	}
	return result;
}

[[nodiscard]] std::vector<float> floatList(
	const nlohmann::json& value,
	const char* fieldName) {
	if (!value.is_array()) {
		throw nlohmann::json::type_error::create(
			302, std::string(fieldName) + " must be an array", &value);
	}
	std::vector<float> result;
	result.reserve(value.size());
	for (const auto& item : value) {
		result.push_back(item.get<float>());
	}
	return result;
}

[[nodiscard]] std::filesystem::path profileRelativePath(
	const std::filesystem::path& profilePath,
	const nlohmann::json& value) {
	const auto text = value.get<std::string>();
	std::filesystem::path path(std::u8string(
		reinterpret_cast<const char8_t*>(text.data()), text.size()));
	if (path.is_relative()) {
		path = std::filesystem::absolute(profilePath).parent_path() / path;
	}
	return path.lexically_normal();
}

void parseSource(
	const nlohmann::json& document,
	const std::filesystem::path& profilePath,
	SourceProfile& source) {
	source.id = document.at("id").get<std::string>();
	source.type = document.at("type").get<std::string>();
	source.role = document.value("role", std::string{});
	if (source.id.empty()) {
		throw nlohmann::json::other_error::create(
			501, "source id must not be empty", &document);
	}
	if (source.type == "directory") {
		source.directory.directory =
			profileRelativePath(profilePath, document.at("directory"));
		source.directory.extensions = stringList(
			document.value(
				"extensions", nlohmann::json({".bmp", ".jpg", ".jpeg", ".png"})),
			"extensions");
		source.directory.loop = document.value("loop", true);
		source.directory.recursive = document.value("recursive", false);
		source.directory.frameInterval = millisecondsValue(
			document.value("frameIntervalMilliseconds", nlohmann::json(0)));
	} else if (source.type == "camera") {
		source.camera.serialNumber =
			document.value("serialNumber", std::string{});
		source.camera.ipAddress = document.value("ipAddress", std::string{});
		source.camera.vendor = document.value("vendor", std::string{});
		source.camera.pixelFormat =
			document.value("pixelFormat", std::string("gray8"));
		source.camera.maxFramesInFlight = sizeValue(document, "maxFramesInFlight", 6);
		source.camera.mode = document.value("mode", std::string("continuous"));
		if (document.contains("exposureMicroseconds")) {
			source.camera.exposureMicroseconds =
				document.at("exposureMicroseconds").get<double>();
		}
		if (document.contains("gain")) {
			source.camera.gain = document.at("gain").get<double>();
		}
		if (document.contains("frameRate")) {
			source.camera.frameRate = document.at("frameRate").get<double>();
		}
		source.camera.triggerInterval = millisecondsValue(
			document.value("triggerIntervalMilliseconds", nlohmann::json(100)));
		source.camera.responseTimeout = millisecondsValue(
			document.value("responseTimeoutMilliseconds", nlohmann::json(1000)));
		if (document.contains("noFrameTimeoutMilliseconds")) {
			const auto& timeout = document.at("noFrameTimeoutMilliseconds");
			if (!timeout.is_number_integer() ||
				timeout.get<std::int64_t>() <= 0 ||
				timeout.get<std::int64_t>() > 86400000) {
				throw nlohmann::json::other_error::create(501,
					"noFrameTimeoutMilliseconds must be an integer in [1, 86400000]",
					&document);
			}
			source.camera.noFrameTimeout = millisecondsValue(timeout);
		}
	} else {
		throw nlohmann::json::other_error::create(
			501, "source type must be directory or camera", &document);
	}
}

} // namespace

core::Result<ProductProfile> ProfileLoader::load(
	const std::filesystem::path& path) {
	std::ifstream stream(path);
	if (!stream) {
		return invalid(core::StatusCode::Unavailable,
			"product profile could not be opened: " + path.string());
	}

	try {
		const auto document = nlohmann::json::parse(stream);
		const auto& schemaVersion = document.at("schemaVersion");
		ProductProfile profile;
		profile.schemaMajor = schemaVersion.at("major").get<std::uint32_t>();
		profile.schemaMinor =
			schemaVersion.value("minor", std::uint32_t{0});
		if (profile.schemaMajor != 1) {
			return invalid(core::StatusCode::Unsupported,
				"product profile schema major version is unsupported");
		}

		profile.id = document.at("id").get<std::string>();
		profile.runtimeProduct = document.value("runtimeProduct", std::string{});
		profile.name = document.value("name", std::string{});

		const auto& sources = document.at("sources");
		if (!sources.is_array() || sources.empty()) {
			return invalid(core::StatusCode::InvalidArgument,
				"profile requires at least one source");
		}
		profile.sources.reserve(sources.size());
		std::unordered_set<std::string> sourceIds;
		for (const auto& sourceDocument : sources) {
			SourceProfile source;
			parseSource(sourceDocument, path, source);
			if (!sourceIds.insert(source.id).second) {
				return invalid(core::StatusCode::InvalidArgument,
					"source ids must be unique");
			}
			if (source.type == "camera" &&
				source.camera.mode != "continuous" &&
				source.camera.mode != "timedTrigger" &&
				!(source.camera.mode == "eventTrigger" && !profile.runtimeProduct.empty())) {
				return invalid(core::StatusCode::InvalidArgument,
					"camera mode must be continuous, timedTrigger, or eventTrigger for a custom product");
			}
			profile.sources.push_back(std::move(source));
		}

		if (document.contains("model")) {
			const auto& model = document.at("model");
			profile.model.packagePath =
				profileRelativePath(path, model.at("packagePath"));
			profile.model.backendId = model.at("backendId").get<std::string>();
			profile.model.device = model.at("device").get<std::string>();
			profile.model.pluginDirectory =
				profileRelativePath(path, model.at("pluginDirectory"));
		} else if (profile.runtimeProduct.empty()) {
			return invalid(core::StatusCode::InvalidArgument,
				"model is required unless runtimeProduct selects a custom product");
		}

		const auto& pipeline = document.value(
			"pipeline", nlohmann::json::object());
		profile.pipeline.resizeShortSide = sizeValue(pipeline, "resizeShortSide", 256);
		profile.pipeline.cropWidth = sizeValue(pipeline, "cropWidth", 224);
		profile.pipeline.cropHeight = sizeValue(pipeline, "cropHeight", 224);
		profile.pipeline.mean = floatList(
			pipeline.value("mean", nlohmann::json::array({0.0})), "mean");
		profile.pipeline.standardDeviation = floatList(
			pipeline.value("standardDeviation", nlohmann::json::array({1.0})),
			"standardDeviation");
		profile.pipeline.threshold =
			pipeline.value("threshold", 0.5F);
		profile.pipeline.maxBatchSize = sizeValue(pipeline, "maxBatchSize", 0, true);
		profile.pipeline.flushTimeout = millisecondsValue(
			pipeline.value("flushTimeoutMilliseconds", nlohmann::json(20)));
		profile.pipeline.queueCapacity = sizeValue(pipeline, "queueCapacity", 16);
		profile.pipeline.stageQueueCapacity = sizeValue(pipeline, "stageQueueCapacity", 1);
		profile.pipeline.queueFullPolicy =
			pipeline.value("queueFullPolicy", std::string("drop"));
		profile.pipeline.faultPolicy =
			pipeline.value("faultPolicy", std::string("reportAndContinue"));
		if (profile.pipeline.faultPolicy != "reportAndContinue" &&
			profile.pipeline.faultPolicy != "stopSession") {
			return invalid(core::StatusCode::InvalidArgument,
				"pipeline.faultPolicy must be reportAndContinue or stopSession");
		}
		if (pipeline.contains("taskTimeoutMilliseconds")) {
			const auto& timeout = pipeline.at("taskTimeoutMilliseconds");
			if (!timeout.is_number_integer() ||
				timeout.get<std::int64_t>() <= 0 ||
				timeout.get<std::int64_t>() > 86400000) {
				return invalid(core::StatusCode::InvalidArgument,
					"pipeline.taskTimeoutMilliseconds must be an integer in [1, 86400000]");
			}
			profile.pipeline.taskTimeout =
				millisecondsValue(timeout);
		}
		if (!std::isfinite(profile.pipeline.threshold) ||
			profile.pipeline.queueCapacity == 0 ||
			profile.pipeline.stageQueueCapacity == 0 ||
			profile.pipeline.flushTimeout.count() < 0 ||
			(profile.pipeline.queueFullPolicy != "block" &&
			 profile.pipeline.queueFullPolicy != "drop")) {
			return invalid(core::StatusCode::InvalidArgument,
				"invalid pipeline threshold, queue capacities, flush timeout or queue policy");
		}

		profile.endpoints.clear();
		if (document.contains("endpoints")) {
			for (const auto& exposed : document.at("endpoints")) {
				EndpointExposure endpoint;
				endpoint.name = exposed.at("name").get<std::string>();
				endpoint.access =
					exposed.value("access", std::string("operator"));
				profile.endpoints.push_back(std::move(endpoint));
			}
		}

		profile.uiJson = document.contains("ui")
			? document.at("ui").dump()
			: std::string{};
		profile.documentJson = document.dump();

		return core::Result<ProductProfile>::success(std::move(profile));
	} catch (const std::exception& exception) {
		return invalid(core::StatusCode::InvalidArgument,
			std::string("invalid product profile: ") + exception.what());
	}
}

} // namespace visionService::profile
