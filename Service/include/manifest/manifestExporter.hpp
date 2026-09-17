/**
 * @file manifestExporter.hpp
 * @brief Serializes the endpoint registry into the manifest JSON consumed by visionDesigner.
 */

#pragma once

#include <string>

namespace visionService {

namespace endpoints {
class EndpointRegistry;
}

namespace manifest {

[[nodiscard]] std::string exportManifest(
	const endpoints::EndpointRegistry& registry);

} // namespace manifest
} // namespace visionService
