#include "exports/businessExports.hpp"

namespace visionService::exports {

core::Result<void> BusinessExports::registerWith(endpoints::EndpointRegistry& registry) {
	if (binding_) {
		return detail::failure<void>(core::StatusCode::InvalidState,
			"business exports are already registered");
	}
	std::vector<std::string> registered;
	auto add = [&](const auto& entries, auto registerEndpoint, auto nameOf) {
		for (const auto& endpoint : entries) {
			auto result = (registry.*registerEndpoint)(endpoint);
			if (!result) {
				visionRuntime::logs::report(result.status());
				return result;
			}
			registered.push_back(nameOf(endpoint));
		}
		return core::Result<void>::success();
	};
	auto result = add(parameters_, &endpoints::EndpointRegistry::registerParameter,
		[](const auto& endpoint) { return endpoint.descriptor.name; });
	if (result) {
		result = add(commands_, &endpoints::EndpointRegistry::registerCommand,
			[](const auto& endpoint) { return endpoint.name; });
	}
	if (result) {
		result = add(states_, &endpoints::EndpointRegistry::registerState,
			[](const auto& endpoint) { return endpoint.name; });
	}
	if (result) {
		result = add(streams_, &endpoints::EndpointRegistry::registerStream,
			[](const auto& endpoint) { return endpoint.name; });
	}
	if (!result) {
		for (const auto& name : registered) {
			auto removed = registry.remove(name);
			if (!removed) {
				visionRuntime::logs::report(removed.status());
			}
		}
		return result;
	}
	binding_ = std::make_shared<detail::Binding>();
	binding_->registry = &registry;
	for (const auto& connect : connectors_) {
		connect(binding_);
	}
	return core::Result<void>::success();
}

void BusinessExports::disconnect() noexcept {
	if (binding_) {
		std::lock_guard lock(binding_->mutex);
		binding_->registry = nullptr;
	}
}

} // namespace visionService::exports
