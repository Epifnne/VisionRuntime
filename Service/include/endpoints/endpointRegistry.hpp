/**
 * @file endpointRegistry.hpp
 * @brief Endpoint registry: the single contract between visionService and its UI host.
 *
 * The registry holds Parameter, Command, State and Stream endpoints. Domain
 * semantics live in the registered endpoints and the product profile; callers
 * of the registry only ever see endpoints.
 *
 * Endpoint handlers (parameter read/write, command invoke, state snapshot
 * providers) run on the caller's thread. Subscription callbacks are always
 * delivered serially on the EndpointDispatcher thread.
 */

#pragma once

#include "core/result.hpp"
#include "endpoints/endpointTypes.hpp"
#include "vision/frame.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace visionService::endpoints {

class EndpointDispatcher;

struct ParameterDescriptor {
	std::string name;
	std::string description;
	ParameterType type = ParameterType::Decimal;
	bool writable = false;
	AccessLevel accessLevel = AccessLevel::Operator;
	std::optional<double> minimum;
	std::optional<double> maximum;
};

struct ParameterEndpoint {
	ParameterDescriptor descriptor;
	std::function<visionRuntime::core::Result<ParameterValue>()> read;
	std::function<visionRuntime::core::Result<void>(const ParameterValue&)> write;
};

struct CommandDescriptor {
	std::string name;
	std::string description;
	AccessLevel accessLevel = AccessLevel::Operator;
};

struct CommandEndpoint {
	std::string name;
	std::string description;
	AccessLevel accessLevel = AccessLevel::Operator;
	std::function<visionRuntime::core::Result<void>()> invoke;
};

struct StateSnapshot {
	std::uint64_t version = 0;
	std::string payload;
};

using StateCallback = std::function<void(const StateSnapshot&)>;
using StreamCallback = std::function<void(
	std::uint32_t sourceId, const visionRuntime::vision::Frame&)>;

struct StateDescriptor {
	std::string name;
	std::string description;
	AccessLevel accessLevel = AccessLevel::Operator;
};

struct StateEndpoint {
	std::string name;
	std::string description;
	AccessLevel accessLevel = AccessLevel::Operator;
	std::function<StateSnapshot()> current;
};

struct StreamDescriptor {
	std::string name;
	std::string description;
	AccessLevel accessLevel = AccessLevel::Operator;
	std::uint32_t sourceId = 0;
};

struct StreamEndpoint {
	std::string name;
	std::string description;
	AccessLevel accessLevel = AccessLevel::Operator;
	std::uint32_t sourceId = 0;
};

class EndpointRegistry {
public:
	explicit EndpointRegistry(EndpointDispatcher& dispatcher) noexcept;
	~EndpointRegistry() = default;

	EndpointRegistry(const EndpointRegistry&) = delete;
	EndpointRegistry& operator=(const EndpointRegistry&) = delete;
	EndpointRegistry(EndpointRegistry&&) = delete;
	EndpointRegistry& operator=(EndpointRegistry&&) = delete;

	[[nodiscard]] visionRuntime::core::Result<void> registerParameter(
		ParameterEndpoint endpoint);
	[[nodiscard]] visionRuntime::core::Result<void> registerCommand(
		CommandEndpoint endpoint);
	[[nodiscard]] visionRuntime::core::Result<void> registerState(
		StateEndpoint endpoint);
	[[nodiscard]] visionRuntime::core::Result<void> registerStream(
		StreamEndpoint endpoint);
	[[nodiscard]] visionRuntime::core::Result<void> remove(std::string_view name);
	void clear() noexcept;
	[[nodiscard]] bool contains(std::string_view name) const;
	[[nodiscard]] std::vector<std::string> endpointNames() const;

	[[nodiscard]] visionRuntime::core::Result<ParameterValue> readParameter(
		std::string_view name) const;
	[[nodiscard]] visionRuntime::core::Result<void> writeParameter(
		std::string_view name, const ParameterValue& value);
	[[nodiscard]] visionRuntime::core::Result<void> invokeCommand(
		std::string_view name);
	[[nodiscard]] visionRuntime::core::Result<StateSnapshot> stateSnapshot(
		std::string_view name) const;

	[[nodiscard]] visionRuntime::core::Result<std::uint64_t> subscribeState(
		std::string_view name, StateCallback callback);
	[[nodiscard]] visionRuntime::core::Result<std::uint64_t> subscribeStream(
		std::string_view name, StreamCallback callback);
	void unsubscribe(std::uint64_t token) noexcept;

	/**
	 * Reads the endpoint's snapshot provider, bumps the version and delivers
	 * the snapshot to subscribers on the dispatch thread.
	 */
	[[nodiscard]] visionRuntime::core::Result<void> publishState(
		std::string_view name);
	/**
	 * Offers a frame to stream subscribers. Only the latest frame per stream
	 * is kept while a dispatch is pending; superseded frames are counted as
	 * dropped so the pipeline is never back-pressured by the UI.
	 */
	void publishFrame(
		std::string_view name,
		std::uint32_t sourceId,
		visionRuntime::vision::Frame frame);

	[[nodiscard]] visionRuntime::core::Result<void> setAccessLevel(
		std::string_view name, AccessLevel level);

	[[nodiscard]] std::vector<ParameterDescriptor> parameters() const;
	[[nodiscard]] std::vector<CommandDescriptor> commands() const;
	[[nodiscard]] std::vector<StateDescriptor> states() const;
	[[nodiscard]] std::vector<StreamDescriptor> streams() const;
	[[nodiscard]] std::size_t droppedFrames(std::string_view name) const;

private:
	struct StateSlot {
		StateEndpoint endpoint;
		StateSnapshot latest;
		std::unordered_map<std::uint64_t, StateCallback> subscribers;
	};

	struct StreamSlot {
		StreamEndpoint endpoint;
		std::unordered_map<std::uint64_t, StreamCallback> subscribers;
		std::shared_ptr<visionRuntime::vision::Frame> pending;
		std::uint32_t pendingSourceId = 0;
		bool dispatchScheduled = false;
		std::size_t dropped = 0;
	};

	[[nodiscard]] visionRuntime::core::Result<void> checkNameAvailable(
		std::string_view name) const;

	EndpointDispatcher& dispatcher_;
	mutable std::mutex mutex_;
	std::unordered_map<std::string, ParameterEndpoint> parameters_;
	std::unordered_map<std::string, CommandEndpoint> commands_;
	std::unordered_map<std::string, StateSlot> states_;
	std::unordered_map<std::string, StreamSlot> streams_;
	std::uint64_t nextToken_ = 1;
	// Bumped by clear(); frame dispatch tasks from before a clear are dropped.
	std::uint64_t generation_ = 0;
};

} // namespace visionService::endpoints
