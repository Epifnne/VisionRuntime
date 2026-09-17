#include "manifest/manifestExporter.hpp"

#include "endpoints/endpointRegistry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace visionService::manifest {

std::string exportManifest(const endpoints::EndpointRegistry& registry) {
	nlohmann::json document;
	document["schemaVersion"] = {{"major", 1}, {"minor", 0}};
	document["endpoints"] = nlohmann::json::array();

	for (const auto& parameter : registry.parameters()) {
		nlohmann::json entry;
		entry["name"] = parameter.name;
		entry["kind"] = std::string(endpoints::endpointKindName(
			endpoints::EndpointKind::Parameter));
		entry["valueType"] = std::string(
			endpoints::parameterTypeName(parameter.type));
		entry["writable"] = parameter.writable;
		entry["access"] = std::string(
			endpoints::accessLevelName(parameter.accessLevel));
		if (!parameter.description.empty()) {
			entry["description"] = parameter.description;
		}
		if (parameter.minimum) {
			entry["minimum"] = *parameter.minimum;
		}
		if (parameter.maximum) {
			entry["maximum"] = *parameter.maximum;
		}
		document["endpoints"].push_back(std::move(entry));
	}

	for (const auto& command : registry.commands()) {
		nlohmann::json entry;
		entry["name"] = command.name;
		entry["kind"] = std::string(endpoints::endpointKindName(
			endpoints::EndpointKind::Command));
		entry["access"] = std::string(
			endpoints::accessLevelName(command.accessLevel));
		if (!command.description.empty()) {
			entry["description"] = command.description;
		}
		document["endpoints"].push_back(std::move(entry));
	}

	for (const auto& state : registry.states()) {
		nlohmann::json entry;
		entry["name"] = state.name;
		entry["kind"] = std::string(endpoints::endpointKindName(
			endpoints::EndpointKind::State));
		entry["access"] = std::string(
			endpoints::accessLevelName(state.accessLevel));
		if (!state.description.empty()) {
			entry["description"] = state.description;
		}
		document["endpoints"].push_back(std::move(entry));
	}

	for (const auto& stream : registry.streams()) {
		nlohmann::json entry;
		entry["name"] = stream.name;
		entry["kind"] = std::string(endpoints::endpointKindName(
			endpoints::EndpointKind::Stream));
		entry["access"] = std::string(
			endpoints::accessLevelName(stream.accessLevel));
		entry["sourceId"] = stream.sourceId;
		if (!stream.description.empty()) {
			entry["description"] = stream.description;
		}
		document["endpoints"].push_back(std::move(entry));
	}

	auto& entries = document["endpoints"];
	auto& items = entries.get_ref<nlohmann::json::array_t&>();
	std::ranges::sort(items, [](const nlohmann::json& left,
		const nlohmann::json& right) {
		return left.at("name").get<std::string>() <
			right.at("name").get<std::string>();
	});
	return document.dump(2);
}

} // namespace visionService::manifest
