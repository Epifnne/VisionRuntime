#include "exports/exportContract.hpp"

#include <limits>
#include <algorithm>

namespace visionService::exports {
namespace {

manifest::EndpointDescriptions hostDescriptions(const profile::ProductProfile& profile) {
	manifest::EndpointDescriptions host;
	host.commands = {
		{"session.start", "start the detection session"},
		{"session.stop", "request a graceful stop of the running session"},
		{"session.recover", "reload the profile and recreate camera/model resources; does not start detection",
			endpoints::AccessLevel::Engineer}};
	host.states = {
		{"session.state", "session lifecycle state"},
		{"session.summary", "session counters and per-source statistics"},
		{"metrics.performance", "last batch performance counters"},
		{"inspection.result", "latest correlated inspection outcome; subscribe for immutable outcome snapshots"},
		{"source.fault", "latest acquisition fault or frame-sequence gap"}};
	for (std::size_t index = 0; index < profile.sources.size(); ++index) {
		const auto& source = profile.sources[index];
		host.streams.push_back({"stream." + source.id, source.role,
			endpoints::AccessLevel::Operator, static_cast<std::uint32_t>(index)});
		host.parameters.push_back({
			.name = "camera." + source.id + ".exposureMicroseconds",
			.description = "camera exposure in microseconds",
			.type = endpoints::ParameterType::Decimal, .writable = true,
			.accessLevel = endpoints::AccessLevel::Engineer, .minimum = 1.0});
		host.parameters.push_back({
			.name = "camera." + source.id + ".gain",
			.description = "camera analog gain",
			.type = endpoints::ParameterType::Decimal, .writable = true,
			.accessLevel = endpoints::AccessLevel::Engineer, .minimum = 0.0});
		host.commands.push_back({"camera." + source.id + ".softwareTrigger",
			"issue one software trigger"});
	}
	return host;
}

} // namespace

core::Result<std::string> ExportContract::describe(
	const profile::ProductProfile& profile) const {
	if (profile.runtimeProduct.empty() ||
		profile.sources.size() > std::numeric_limits<std::uint32_t>::max()) {
		return detail::failure<std::string>(core::StatusCode::InvalidArgument,
			"export contract requires a runtimeProduct and representable source indices");
	}
	auto descriptions = hostDescriptions(profile);
	descriptions.parameters.insert(descriptions.parameters.end(),
		descriptions_.parameters.begin(), descriptions_.parameters.end());
	descriptions.commands.insert(descriptions.commands.end(),
		descriptions_.commands.begin(), descriptions_.commands.end());
	descriptions.states.insert(descriptions.states.end(),
		descriptions_.states.begin(), descriptions_.states.end());
	descriptions.streams.insert(descriptions.streams.end(),
		descriptions_.streams.begin(), descriptions_.streams.end());
	try {
		auto document = nlohmann::json::parse(manifest::exportManifest(descriptions));
		std::unordered_set<std::string> names;
		for (const auto& endpoint : document["endpoints"]) {
			const auto name = endpoint.at("name").get<std::string>();
			if (name.empty() || !names.insert(name).second) {
				return detail::failure<std::string>(core::StatusCode::InvalidArgument,
					"export names must be nonempty and unique: " + name);
			}
		}
		for (const auto& parameter : descriptions.parameters) {
			if ((parameter.minimum && !std::isfinite(*parameter.minimum)) ||
				(parameter.maximum && !std::isfinite(*parameter.maximum)) ||
				(parameter.minimum && parameter.maximum &&
					*parameter.minimum > *parameter.maximum)) {
				return detail::failure<std::string>(core::StatusCode::InvalidArgument,
					"invalid export parameter range: " + parameter.name);
			}
		}
		for (const auto& state : descriptions_.states) {
			std::unordered_set<std::string> fields;
			if (state.fields.empty()) {
				return detail::failure<std::string>(core::StatusCode::InvalidArgument,
					"declared business state requires fields: " + state.name);
			}
			for (const auto& field : state.fields) {
				if (field.name.empty() || !fields.insert(field.name).second) {
					return detail::failure<std::string>(core::StatusCode::InvalidArgument,
						"state fields must be nonempty and unique: " + state.name);
				}
			}
		}
		for (const auto& exposure : profile.endpoints) {
			auto endpoint = std::ranges::find_if(document["endpoints"], [&](const auto& value) {
				return value.at("name") == exposure.name;
			});
			if (endpoint == document["endpoints"].end() ||
				(exposure.access != "operator" && exposure.access != "engineer" &&
					exposure.access != "administrator")) {
				return detail::failure<std::string>(core::StatusCode::InvalidArgument,
					"invalid endpoint exposure: " + exposure.name);
			}
			(*endpoint)["access"] = exposure.access;
		}
		document["runtimeProduct"] = profile.runtimeProduct;
		return core::Result<std::string>::success(document.dump(2));
	} catch (const nlohmann::json::exception& exception) {
		return detail::failure<std::string>(core::StatusCode::InvalidArgument,
			std::string("export description failed: ") + exception.what());
	}
}

core::Result<void> validateExportContract(
	std::string_view expectedManifest, std::string_view actualManifest) {
	try {
		const auto expected = nlohmann::json::parse(expectedManifest);
		const auto actual = nlohmann::json::parse(actualManifest);
		if (expected.contains("runtimeProduct") &&
			expected.at("runtimeProduct") != actual.value("runtimeProduct", std::string{})) {
			return detail::failure<void>(core::StatusCode::InvalidState,
				"runtime product does not match the declared export contract");
		}
		if (expected.at("endpoints") != actual.at("endpoints")) {
			return detail::failure<void>(core::StatusCode::InvalidState,
				"runtime endpoint bindings do not match the declared export contract");
		}
		return core::Result<void>::success();
	} catch (const nlohmann::json::exception& exception) {
		return detail::failure<void>(core::StatusCode::InvalidArgument,
			std::string("invalid export contract: ") + exception.what());
	}
}

} // namespace visionService::exports
