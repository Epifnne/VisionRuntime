#pragma once

#include "endpoints/endpointRegistry.hpp"
#include "logs/logger.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace visionService::exports {

namespace core = visionRuntime::core;

namespace detail {

template<typename T>
core::Result<T> failure(core::StatusCode code, std::string message) {
	auto status = core::Status::error(code, std::move(message));
	visionRuntime::logs::report(status);
	return core::Result<T>::failure(std::move(status));
}

struct Binding {
	std::mutex mutex;
	endpoints::EndpointRegistry* registry = nullptr;
};

template<typename T>
constexpr endpoints::ParameterType fieldType() {
	if constexpr (std::is_same_v<T, bool>) {
		return endpoints::ParameterType::Flag;
	} else if constexpr (std::is_integral_v<T>) {
		return endpoints::ParameterType::Integer;
	} else if constexpr (std::is_floating_point_v<T>) {
		return endpoints::ParameterType::Decimal;
	} else {
		static_assert(std::is_same_v<T, std::string>,
			"export fields must be numbers, bool or std::string");
		return endpoints::ParameterType::Text;
	}
}

} // namespace detail

template<typename T>
class StateSchema {
public:
	template<typename Member>
	StateSchema& field(std::string name, Member T::* member,
		std::string description = {}, std::string unit = {}) {
		fields_.push_back({std::move(name), detail::fieldType<Member>(),
			std::move(description), std::move(unit)});
		readers_.push_back([member](const T& value) -> core::Result<nlohmann::json> {
			if constexpr (std::is_floating_point_v<Member>) {
				if (!std::isfinite(value.*member)) {
					return detail::failure<nlohmann::json>(core::StatusCode::InvalidArgument,
						"exported numeric field must be finite");
				}
			}
			return core::Result<nlohmann::json>::success(value.*member);
		});
		return *this;
	}

	[[nodiscard]] core::Result<std::string> encode(const T& value) const {
		nlohmann::json document = nlohmann::json::object();
		std::unordered_set<std::string> names;
		if (fields_.empty()) {
			return detail::failure<std::string>(core::StatusCode::InvalidArgument,
				"exported state requires at least one declared field");
		}
		for (std::size_t index = 0; index < fields_.size(); ++index) {
			const auto& name = fields_[index].name;
			if (name.empty() || !names.insert(name).second) {
				return detail::failure<std::string>(core::StatusCode::InvalidArgument,
					"exported state field names must be nonempty and unique");
			}
			auto field = readers_[index](value);
			if (!field) {
				return core::Result<std::string>::failure(field.status().withContext(name));
			}
			document[name] = std::move(field).value();
		}
		try {
			return core::Result<std::string>::success(document.dump());
		} catch (const nlohmann::json::exception& exception) {
			return detail::failure<std::string>(core::StatusCode::InvalidArgument,
				std::string("exported state encoding failed: ") + exception.what());
		}
	}

	[[nodiscard]] const std::vector<endpoints::StateDescriptor::Field>& fields() const {
		return fields_;
	}

private:
	std::vector<endpoints::StateDescriptor::Field> fields_;
	std::vector<std::function<core::Result<nlohmann::json>(const T&)>> readers_;
};

class BusinessExports;

class ExportedStream {
public:
	[[nodiscard]] core::Result<void> publish(visionRuntime::vision::Frame frame) const {
		const auto active = binding_.lock();
		if (active) {
			std::lock_guard lock(active->mutex);
			if (active->registry) {
				active->registry->publishFrame(name_, sourceId_, std::move(frame));
				return core::Result<void>::success();
			}
		}
		return detail::failure<void>(core::StatusCode::InvalidState,
			"business stream is not attached: " + name_);
	}

private:
	friend class BusinessExports;
	std::string name_;
	std::uint32_t sourceId_ = 0;
	std::weak_ptr<detail::Binding> binding_;
};

template<typename T>
class ExportedState {
public:
	[[nodiscard]] core::Result<void> set(const T& value) {
		auto encoded = schema_.encode(value);
		if (!encoded) {
			return core::Result<void>::failure(encoded.status());
		}
		std::lock_guard lock(mutex_);
		if (publisher_) {
			auto published = publisher_({0, encoded.value()});
			if (!published) {
				return published;
			}
		}
		payload_ = std::move(encoded).value();
		return core::Result<void>::success();
	}

	[[nodiscard]] endpoints::StateSnapshot snapshot() const {
		std::lock_guard lock(mutex_);
		return {0, payload_};
	}

private:
	friend class BusinessExports;
	ExportedState(StateSchema<T> schema, std::string payload)
		: schema_(std::move(schema)), payload_(std::move(payload)) {}

	StateSchema<T> schema_;
	mutable std::mutex mutex_;
	std::string payload_;
	std::function<core::Result<void>(endpoints::StateSnapshot)> publisher_;
};

class BusinessExports {
public:
	BusinessExports() = default;
	~BusinessExports() { disconnect(); }
	BusinessExports(const BusinessExports&) = delete;
	BusinessExports& operator=(const BusinessExports&) = delete;
	BusinessExports(BusinessExports&&) = default;
	BusinessExports& operator=(BusinessExports&&) = delete;

	BusinessExports& parameter(endpoints::ParameterEndpoint endpoint) {
		parameters_.push_back(std::move(endpoint));
		return *this;
	}
	template<typename T>
	BusinessExports& parameter(endpoints::ParameterDescriptor descriptor,
		std::function<core::Result<T>()> read,
		std::function<core::Result<void>(const T&)> write = {}) {
		static_assert(std::is_same_v<T, double> || std::is_same_v<T, std::int64_t> ||
			std::is_same_v<T, bool> || std::is_same_v<T, std::string>,
			"export parameters use double, int64_t, bool or std::string");
		descriptor.type = detail::fieldType<T>();
		descriptor.writable = static_cast<bool>(write);
		endpoints::ParameterEndpoint endpoint;
		endpoint.descriptor = std::move(descriptor);
		if (read) {
			endpoint.read = [read = std::move(read)] {
				auto value = read();
				if (!value) {
					return core::Result<endpoints::ParameterValue>::failure(value.status());
				}
				return core::Result<endpoints::ParameterValue>::success(std::move(value).value());
			};
		}
		if (write) {
			endpoint.write = [write = std::move(write)](const endpoints::ParameterValue& value) {
				const auto* typed = std::get_if<T>(&value);
				if (!typed) {
					return detail::failure<void>(core::StatusCode::InvalidArgument,
						"export parameter type mismatch");
				}
				return write(*typed);
			};
		}
		return parameter(std::move(endpoint));
	}
	BusinessExports& command(endpoints::CommandEndpoint endpoint) {
		commands_.push_back(std::move(endpoint));
		return *this;
	}
	[[nodiscard]] std::shared_ptr<ExportedStream> stream(endpoints::StreamEndpoint endpoint) {
		auto stream = std::make_shared<ExportedStream>();
		stream->name_ = endpoint.name;
		stream->sourceId_ = endpoint.sourceId;
		streams_.push_back(std::move(endpoint));
		connectors_.push_back([stream](std::weak_ptr<detail::Binding> binding) {
			stream->binding_ = std::move(binding);
		});
		return stream;
	}

	template<typename T>
	[[nodiscard]] core::Result<std::shared_ptr<ExportedState<T>>> state(
		endpoints::StateDescriptor descriptor, StateSchema<T> schema, const T& initial) {
		auto payload = schema.encode(initial);
		if (!payload) {
			return core::Result<std::shared_ptr<ExportedState<T>>>::failure(payload.status());
		}
		auto state = std::shared_ptr<ExportedState<T>>(
			new ExportedState<T>(std::move(schema), std::move(payload).value()));
		states_.push_back({
			.name = descriptor.name,
			.description = std::move(descriptor.description),
			.accessLevel = descriptor.accessLevel,
			.current = [state] { return state->snapshot(); },
			.fields = state->schema_.fields()});
		connectors_.push_back([state, name = std::move(descriptor.name)](
			std::weak_ptr<detail::Binding> binding) {
			std::lock_guard lock(state->mutex_);
			state->publisher_ = [binding, name](endpoints::StateSnapshot snapshot) {
				const auto active = binding.lock();
				if (active) {
					std::lock_guard bindingLock(active->mutex);
					if (active->registry) {
						return active->registry->publishState(name, std::move(snapshot));
					}
				}
				return detail::failure<void>(core::StatusCode::InvalidState,
					"business export is no longer attached: " + name);
			};
		});
		return core::Result<std::shared_ptr<ExportedState<T>>>::success(std::move(state));
	}

	[[nodiscard]] core::Result<void> registerWith(endpoints::EndpointRegistry& registry);
	void disconnect() noexcept;

private:
	std::vector<endpoints::ParameterEndpoint> parameters_;
	std::vector<endpoints::CommandEndpoint> commands_;
	std::vector<endpoints::StateEndpoint> states_;
	std::vector<endpoints::StreamEndpoint> streams_;
	std::vector<std::function<void(std::weak_ptr<detail::Binding>)>> connectors_;
	std::shared_ptr<detail::Binding> binding_;
};

} // namespace visionService::exports
