#include "endpoints/endpointRegistry.hpp"

#include "endpoints/endpointDispatcher.hpp"

#include <utility>

namespace visionService::endpoints {

namespace core = visionRuntime::core;

namespace {

template<typename T>
[[nodiscard]] core::Result<T> failure(
	core::StatusCode code, std::string message) {
	return core::Result<T>::failure(
		core::Status::error(code, std::move(message)));
}

template<typename Callback>
[[nodiscard]] std::vector<Callback> callbackValues(
	const std::unordered_map<std::uint64_t, Callback>& callbacks) {
	std::vector<Callback> values;
	values.reserve(callbacks.size());
	for (const auto& [token, callback] : callbacks) {
		static_cast<void>(token);
		values.push_back(callback);
	}
	return values;
}

} // namespace

EndpointRegistry::EndpointRegistry(EndpointDispatcher& dispatcher) noexcept
	: dispatcher_(dispatcher) {}

core::Result<void> EndpointRegistry::checkNameAvailable(
	std::string_view name) const {
	if (name.empty()) {
		return failure<void>(core::StatusCode::InvalidArgument,
			"endpoint name must not be empty");
	}
	const std::string key(name);
	if (parameters_.contains(key) || commands_.contains(key) ||
		states_.contains(key) || streams_.contains(key)) {
		return failure<void>(core::StatusCode::AlreadyExists,
			"endpoint already registered: " + std::string(name));
	}
	return core::Result<void>::success();
}

core::Result<void> EndpointRegistry::registerParameter(ParameterEndpoint endpoint) {
	if (!endpoint.read) {
		return failure<void>(core::StatusCode::InvalidArgument,
			"parameter endpoint requires a read callback");
	}
	if (endpoint.descriptor.writable && !endpoint.write) {
		return failure<void>(core::StatusCode::InvalidArgument,
			"writable parameter endpoint requires a write callback");
	}
	std::lock_guard lock(mutex_);
	auto available = checkNameAvailable(endpoint.descriptor.name);
	if (!available) {
		return available;
	}
	parameters_.emplace(endpoint.descriptor.name, std::move(endpoint));
	return core::Result<void>::success();
}

core::Result<void> EndpointRegistry::registerCommand(CommandEndpoint endpoint) {
	if (!endpoint.invoke) {
		return failure<void>(core::StatusCode::InvalidArgument,
			"command endpoint requires an invoke callback");
	}
	std::lock_guard lock(mutex_);
	auto available = checkNameAvailable(endpoint.name);
	if (!available) {
		return available;
	}
	commands_.emplace(endpoint.name, std::move(endpoint));
	return core::Result<void>::success();
}

core::Result<void> EndpointRegistry::registerState(StateEndpoint endpoint) {
	if (!endpoint.current) {
		return failure<void>(core::StatusCode::InvalidArgument,
			"state endpoint requires a snapshot provider");
	}
	std::lock_guard lock(mutex_);
	auto available = checkNameAvailable(endpoint.name);
	if (!available) {
		return available;
	}
	StateSlot slot;
	slot.endpoint = std::move(endpoint);
	states_.emplace(slot.endpoint.name, std::move(slot));
	return core::Result<void>::success();
}

core::Result<void> EndpointRegistry::registerStream(StreamEndpoint endpoint) {
	std::lock_guard lock(mutex_);
	auto available = checkNameAvailable(endpoint.name);
	if (!available) {
		return available;
	}
	StreamSlot slot;
	slot.endpoint = std::move(endpoint);
	streams_.emplace(slot.endpoint.name, std::move(slot));
	return core::Result<void>::success();
}

core::Result<void> EndpointRegistry::remove(std::string_view name) {
	std::lock_guard lock(mutex_);
	const std::string key(name);
	const auto removed = parameters_.erase(key) + commands_.erase(key) +
		states_.erase(key) + streams_.erase(key);
	if (removed == 0) {
		return failure<void>(core::StatusCode::NotFound,
			"endpoint not registered: " + std::string(name));
	}
	return core::Result<void>::success();
}

void EndpointRegistry::clear() noexcept {
	std::lock_guard lock(mutex_);
	++generation_;
	parameters_.clear();
	commands_.clear();
	states_.clear();
	streams_.clear();
}

bool EndpointRegistry::contains(std::string_view name) const {
	std::lock_guard lock(mutex_);
	const std::string key(name);
	return parameters_.contains(key) || commands_.contains(key) ||
		states_.contains(key) || streams_.contains(key);
}

std::vector<std::string> EndpointRegistry::endpointNames() const {
	std::lock_guard lock(mutex_);
	std::vector<std::string> names;
	names.reserve(parameters_.size() + commands_.size() + states_.size() +
		streams_.size());
	for (const auto& [name, endpoint] : parameters_) {
		static_cast<void>(endpoint);
		names.push_back(name);
	}
	for (const auto& [name, endpoint] : commands_) {
		static_cast<void>(endpoint);
		names.push_back(name);
	}
	for (const auto& [name, slot] : states_) {
		static_cast<void>(slot);
		names.push_back(name);
	}
	for (const auto& [name, slot] : streams_) {
		static_cast<void>(slot);
		names.push_back(name);
	}
	return names;
}

core::Result<ParameterValue> EndpointRegistry::readParameter(
	std::string_view name) const {
	std::function<core::Result<ParameterValue>()> read;
	{
		std::lock_guard lock(mutex_);
		const auto found = parameters_.find(std::string(name));
		if (found == parameters_.end()) {
			return failure<ParameterValue>(core::StatusCode::NotFound,
				"parameter not registered: " + std::string(name));
		}
		read = found->second.read;
	}
	return read();
}

core::Result<void> EndpointRegistry::writeParameter(
	std::string_view name, const ParameterValue& value) {
	std::function<core::Result<void>(const ParameterValue&)> write;
	ParameterDescriptor descriptor;
	{
		std::lock_guard lock(mutex_);
		const auto found = parameters_.find(std::string(name));
		if (found == parameters_.end()) {
			return failure<void>(core::StatusCode::NotFound,
				"parameter not registered: " + std::string(name));
		}
		descriptor = found->second.descriptor;
		write = found->second.write;
	}
	if (!descriptor.writable) {
		return failure<void>(core::StatusCode::Unsupported,
			"parameter is read-only: " + std::string(name));
	}
	if (parameterTypeOf(value) != descriptor.type) {
		return failure<void>(core::StatusCode::InvalidArgument,
			"parameter value type does not match the declared type: " +
				std::string(name));
	}
	double numeric = 0.0;
	if (std::holds_alternative<double>(value)) {
		numeric = std::get<double>(value);
	} else if (std::holds_alternative<std::int64_t>(value)) {
		numeric = static_cast<double>(std::get<std::int64_t>(value));
	} else {
		return write(value);
	}
	if ((descriptor.minimum && numeric < *descriptor.minimum) ||
		(descriptor.maximum && numeric > *descriptor.maximum)) {
		return failure<void>(core::StatusCode::InvalidArgument,
			"parameter value is outside its allowed range: " + std::string(name));
	}
	return write(value);
}

core::Result<void> EndpointRegistry::invokeCommand(std::string_view name) {
	std::function<core::Result<void>()> invoke;
	{
		std::lock_guard lock(mutex_);
		const auto found = commands_.find(std::string(name));
		if (found == commands_.end()) {
			return failure<void>(core::StatusCode::NotFound,
				"command not registered: " + std::string(name));
		}
		invoke = found->second.invoke;
	}
	return invoke();
}

core::Result<StateSnapshot> EndpointRegistry::stateSnapshot(
	std::string_view name) const {
	std::function<StateSnapshot()> current;
	{
		std::lock_guard lock(mutex_);
		const auto found = states_.find(std::string(name));
		if (found == states_.end()) {
			return failure<StateSnapshot>(core::StatusCode::NotFound,
				"state endpoint not registered: " + std::string(name));
		}
		current = found->second.endpoint.current;
	}
	StateSnapshot snapshot;
	try {
		snapshot = current();
	} catch (const std::exception& exception) {
		return failure<StateSnapshot>(core::StatusCode::Internal,
			"state snapshot provider failed: " + std::string(exception.what()));
	}
	std::lock_guard lock(mutex_);
	const auto found = states_.find(std::string(name));
	snapshot.version = found == states_.end() ? 0 : found->second.latest.version;
	return core::Result<StateSnapshot>::success(std::move(snapshot));
}

core::Result<std::uint64_t> EndpointRegistry::subscribeState(
	std::string_view name, StateCallback callback) {
	if (!callback) {
		return failure<std::uint64_t>(core::StatusCode::InvalidArgument,
			"state subscription requires a callback");
	}
	std::lock_guard lock(mutex_);
	const auto found = states_.find(std::string(name));
	if (found == states_.end()) {
		return failure<std::uint64_t>(core::StatusCode::NotFound,
			"state endpoint not registered: " + std::string(name));
	}
	const auto token = nextToken_++;
	found->second.subscribers.emplace(token, std::move(callback));
	return core::Result<std::uint64_t>::success(token);
}

core::Result<std::uint64_t> EndpointRegistry::subscribeStream(
	std::string_view name, StreamCallback callback) {
	if (!callback) {
		return failure<std::uint64_t>(core::StatusCode::InvalidArgument,
			"stream subscription requires a callback");
	}
	std::lock_guard lock(mutex_);
	const auto found = streams_.find(std::string(name));
	if (found == streams_.end()) {
		return failure<std::uint64_t>(core::StatusCode::NotFound,
			"stream endpoint not registered: " + std::string(name));
	}
	const auto token = nextToken_++;
	found->second.subscribers.emplace(token, std::move(callback));
	return core::Result<std::uint64_t>::success(token);
}

void EndpointRegistry::unsubscribe(std::uint64_t token) noexcept {
	std::lock_guard lock(mutex_);
	for (auto& [name, slot] : states_) {
		static_cast<void>(name);
		slot.subscribers.erase(token);
	}
	for (auto& [name, slot] : streams_) {
		static_cast<void>(name);
		slot.subscribers.erase(token);
	}
}

core::Result<void> EndpointRegistry::publishState(std::string_view name) {
	std::function<StateSnapshot()> current;
	{
		std::lock_guard lock(mutex_);
		const auto found = states_.find(std::string(name));
		if (found == states_.end()) {
			return failure<void>(core::StatusCode::NotFound,
				"state endpoint not registered: " + std::string(name));
		}
		current = found->second.endpoint.current;
	}
	StateSnapshot snapshot;
	try {
		snapshot = current();
	} catch (const std::exception& exception) {
		return failure<void>(core::StatusCode::Internal,
			"state snapshot provider failed: " + std::string(exception.what()));
	}
	std::vector<StateCallback> subscribers;
	{
		std::lock_guard lock(mutex_);
		const auto found = states_.find(std::string(name));
		if (found == states_.end()) {
			return failure<void>(core::StatusCode::NotFound,
				"state endpoint not registered: " + std::string(name));
		}
		auto& slot = found->second;
		++slot.latest.version;
		slot.latest.payload = snapshot.payload;
		snapshot.version = slot.latest.version;
		subscribers = callbackValues(slot.subscribers);
	}
	if (!subscribers.empty()) {
		dispatcher_.post(
			[subscribers = std::move(subscribers), snapshot = std::move(snapshot)] {
				for (const auto& callback : subscribers) {
					try {
						callback(snapshot);
					} catch (...) {
					}
				}
			});
	}
	return core::Result<void>::success();
}

void EndpointRegistry::publishFrame(
	std::string_view name,
	std::uint32_t sourceId,
	visionRuntime::vision::Frame frame) {
	bool schedule = false;
	std::uint64_t generation = 0;
	{
		std::lock_guard lock(mutex_);
		const auto found = streams_.find(std::string(name));
		if (found == streams_.end()) {
			return;
		}
		auto& slot = found->second;
		if (slot.subscribers.empty()) {
			slot.pending.reset();
			return;
		}
		if (slot.pending) {
			++slot.dropped;
		}
		slot.pending =
			std::make_shared<visionRuntime::vision::Frame>(std::move(frame));
		slot.pendingSourceId = sourceId;
		if (!slot.dispatchScheduled) {
			slot.dispatchScheduled = true;
			schedule = true;
		}
		generation = generation_;
	}
	if (!schedule) {
		return;
	}
	dispatcher_.post([this, streamName = std::string(name), generation] {
		std::shared_ptr<visionRuntime::vision::Frame> frame;
		std::uint32_t frameSourceId = 0;
		std::vector<StreamCallback> subscribers;
		{
			std::lock_guard lock(mutex_);
			if (generation != generation_) {
				return;
			}
			const auto found = streams_.find(streamName);
			if (found == streams_.end()) {
				return;
			}
			auto& slot = found->second;
			slot.dispatchScheduled = false;
			frame = std::move(slot.pending);
			slot.pending.reset();
			if (!frame) {
				return;
			}
			frameSourceId = slot.pendingSourceId;
			subscribers = callbackValues(slot.subscribers);
		}
		for (const auto& callback : subscribers) {
			try {
				callback(frameSourceId, *frame);
			} catch (...) {
			}
		}
	});
}

core::Result<void> EndpointRegistry::setAccessLevel(
	std::string_view name, AccessLevel level) {
	std::lock_guard lock(mutex_);
	const std::string key(name);
	if (const auto found = parameters_.find(key); found != parameters_.end()) {
		found->second.descriptor.accessLevel = level;
		return core::Result<void>::success();
	}
	if (const auto found = commands_.find(key); found != commands_.end()) {
		found->second.accessLevel = level;
		return core::Result<void>::success();
	}
	if (const auto found = states_.find(key); found != states_.end()) {
		found->second.endpoint.accessLevel = level;
		return core::Result<void>::success();
	}
	if (const auto found = streams_.find(key); found != streams_.end()) {
		found->second.endpoint.accessLevel = level;
		return core::Result<void>::success();
	}
	return failure<void>(core::StatusCode::NotFound,
		"endpoint not registered: " + std::string(name));
}

std::vector<ParameterDescriptor> EndpointRegistry::parameters() const {
	std::lock_guard lock(mutex_);
	std::vector<ParameterDescriptor> result;
	result.reserve(parameters_.size());
	for (const auto& [name, endpoint] : parameters_) {
		static_cast<void>(name);
		result.push_back(endpoint.descriptor);
	}
	return result;
}

std::vector<CommandDescriptor> EndpointRegistry::commands() const {
	std::lock_guard lock(mutex_);
	std::vector<CommandDescriptor> result;
	result.reserve(commands_.size());
	for (const auto& [name, endpoint] : commands_) {
		static_cast<void>(name);
		result.push_back(CommandDescriptor{
			endpoint.name, endpoint.description, endpoint.accessLevel});
	}
	return result;
}

std::vector<StateDescriptor> EndpointRegistry::states() const {
	std::lock_guard lock(mutex_);
	std::vector<StateDescriptor> result;
	result.reserve(states_.size());
	for (const auto& [name, slot] : states_) {
		static_cast<void>(name);
		result.push_back(StateDescriptor{
			slot.endpoint.name, slot.endpoint.description,
			slot.endpoint.accessLevel});
	}
	return result;
}

std::vector<StreamDescriptor> EndpointRegistry::streams() const {
	std::lock_guard lock(mutex_);
	std::vector<StreamDescriptor> result;
	result.reserve(streams_.size());
	for (const auto& [name, slot] : streams_) {
		static_cast<void>(name);
		result.push_back(StreamDescriptor{
			slot.endpoint.name, slot.endpoint.description,
			slot.endpoint.accessLevel, slot.endpoint.sourceId});
	}
	return result;
}

std::size_t EndpointRegistry::droppedFrames(std::string_view name) const {
	std::lock_guard lock(mutex_);
	const auto found = streams_.find(std::string(name));
	return found == streams_.end() ? 0 : found->second.dropped;
}

} // namespace visionService::endpoints
