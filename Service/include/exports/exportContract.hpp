#pragma once

#include "exports/businessExports.hpp"
#include "manifest/manifestExporter.hpp"
#include "profile/productProfile.hpp"

namespace visionService::exports {

template<typename T>
struct ParameterDefinition {
	endpoints::ParameterDescriptor descriptor;

	[[nodiscard]] core::Result<void> bind(BusinessExports& exports,
		std::function<core::Result<T>()> read,
		std::function<core::Result<void>(const T&)> write = {}) const {
		if (!read || descriptor.writable != static_cast<bool>(write)) {
			return detail::failure<void>(core::StatusCode::InvalidArgument,
				"parameter binding does not match its declaration: " + descriptor.name);
		}
		exports.parameter<T>(descriptor, std::move(read), std::move(write));
		return core::Result<void>::success();
	}
};

struct CommandDefinition {
	endpoints::CommandDescriptor descriptor;

	[[nodiscard]] core::Result<void> bind(BusinessExports& exports,
		std::function<core::Result<void>()> invoke) const {
		if (!invoke) {
			return detail::failure<void>(core::StatusCode::InvalidArgument,
				"command binding is empty: " + descriptor.name);
		}
		exports.command({descriptor.name, descriptor.description,
			descriptor.accessLevel, std::move(invoke)});
		return core::Result<void>::success();
	}
};

template<typename T>
struct StateDefinition {
	endpoints::StateDescriptor descriptor;
	StateSchema<T> schema;

	[[nodiscard]] core::Result<std::shared_ptr<ExportedState<T>>> bind(
		BusinessExports& exports, const T& initial) const {
		return exports.state(descriptor, schema, initial);
	}
};

struct StreamDefinition {
	endpoints::StreamDescriptor descriptor;

	[[nodiscard]] std::shared_ptr<ExportedStream> bind(BusinessExports& exports) const {
		return exports.stream({descriptor.name, descriptor.description,
			descriptor.accessLevel, descriptor.sourceId});
	}
};

class ExportContract {
public:
	template<typename T>
	[[nodiscard]] ParameterDefinition<T> parameter(
		endpoints::ParameterDescriptor descriptor) {
		static_assert(std::is_same_v<T, double> || std::is_same_v<T, std::int64_t> ||
			std::is_same_v<T, bool> || std::is_same_v<T, std::string>);
		descriptor.type = detail::fieldType<T>();
		descriptions_.parameters.push_back(descriptor);
		return {std::move(descriptor)};
	}

	[[nodiscard]] CommandDefinition command(endpoints::CommandDescriptor descriptor) {
		descriptions_.commands.push_back(descriptor);
		return {std::move(descriptor)};
	}

	template<typename T>
	[[nodiscard]] StateDefinition<T> state(
		endpoints::StateDescriptor descriptor, StateSchema<T> schema) {
		descriptor.fields = schema.fields();
		descriptions_.states.push_back(descriptor);
		return {std::move(descriptor), std::move(schema)};
	}

	[[nodiscard]] StreamDefinition stream(endpoints::StreamDescriptor descriptor) {
		descriptions_.streams.push_back(descriptor);
		return {std::move(descriptor)};
	}

	[[nodiscard]] core::Result<std::string> describe(
		const profile::ProductProfile& profile) const;

private:
	manifest::EndpointDescriptions descriptions_;
};

[[nodiscard]] core::Result<void> validateExportContract(
	std::string_view expectedManifest, std::string_view actualManifest);

} // namespace visionService::exports
