/**
 * @file manifestExporter.hpp
 * @brief Serializes the endpoint registry into the manifest JSON consumed by visionDesigner.
 */

#pragma once

#include <string>
#include "endpoints/endpointRegistry.hpp"

namespace visionService {

namespace endpoints {
class EndpointRegistry;
}

namespace manifest {

struct EndpointDescriptions {
	std::vector<endpoints::ParameterDescriptor> parameters;
	std::vector<endpoints::CommandDescriptor> commands;
	std::vector<endpoints::StateDescriptor> states;
	std::vector<endpoints::StreamDescriptor> streams;
};

[[nodiscard]] std::string exportManifest(const EndpointDescriptions& descriptions);

[[nodiscard]] std::string exportManifest(
	const endpoints::EndpointRegistry& registry);

} // namespace manifest
} // namespace visionService
