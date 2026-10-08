#pragma once

#include "runtime/multiCameraSession.hpp"
#include "runtime/runtimeSession.hpp"
#include "vision/anomalyResult.hpp"

namespace visionService::session {

using SessionResult = visionRuntime::vision::AnomalyResult;
using SessionSummary = visionRuntime::runtime::MultiCameraExecutionSummary;
using SessionObservers = visionRuntime::executor::FrameObservers<SessionResult>;

class IInspectionSession {
public:
	virtual ~IInspectionSession() = default;
	virtual visionRuntime::core::Result<void> start() = 0;
	virtual void requestStop(visionRuntime::executor::StopMode mode) noexcept = 0;
	virtual SessionSummary wait() = 0;
	[[nodiscard]] virtual SessionSummary currentSummary() const = 0;
	[[nodiscard]] virtual bool stopRequested() const = 0;
	[[nodiscard]] virtual std::size_t sourceCount() const noexcept = 0;
	[[nodiscard]] virtual visionRuntime::camera::IFrameSource* source(
		std::size_t index) const noexcept = 0;
	virtual visionRuntime::core::Result<void> addObservers(SessionObservers observers) = 0;
};

namespace detail {

template<typename Runtime>
class InspectionSessionAdapter final : public IInspectionSession {
public:
	explicit InspectionSessionAdapter(std::unique_ptr<Runtime> runtime)
		: runtime_(std::move(runtime)) {}

	visionRuntime::core::Result<void> start() override { return runtime_->start(); }
	void requestStop(visionRuntime::executor::StopMode mode) noexcept override {
		runtime_->requestStop(mode);
	}
	SessionSummary wait() override { return convert(runtime_->wait()); }
	SessionSummary currentSummary() const override {
		return convert(runtime_->currentSummary());
	}
	bool stopRequested() const override { return runtime_->stopRequested(); }
	std::size_t sourceCount() const noexcept override {
		if constexpr (requires { runtime_->sources(); }) {
			return runtime_->sources().size();
		} else {
			return 1;
		}
	}
	visionRuntime::camera::IFrameSource* source(std::size_t index) const noexcept override {
		if constexpr (requires { runtime_->sources(); }) {
			return index < runtime_->sources().size()
				? runtime_->sources()[index].get() : nullptr;
		} else {
			return index == 0 ? runtime_->source() : nullptr;
		}
	}
	visionRuntime::core::Result<void> addObservers(SessionObservers observers) override {
		return runtime_->addObservers(std::move(observers));
	}

private:
	static SessionSummary convert(SessionSummary summary) { return summary; }
	static SessionSummary convert(const visionRuntime::executor::FrameExecutionSummary& single) {
		SessionSummary summary;
		summary.received = single.received;
		summary.submitted = single.submitted;
		summary.completed = single.completed;
		summary.failed = single.failed;
		summary.dropped = single.dropped;
		summary.sourceFailures = single.sourceFailures;
		summary.receivedPerSource = {single.received};
		summary.droppedPerSource = {single.dropped};
		summary.missingFrames = single.missingFrames;
		summary.missingPerSource = {single.missingFrames};
		summary.rejected = single.rejected;
		summary.expired = single.expired;
		summary.lastError = single.lastError;
		return summary;
	}
	std::unique_ptr<Runtime> runtime_;
};

} // namespace detail

template<typename Runtime>
[[nodiscard]] std::unique_ptr<IInspectionSession> adoptRuntime(
	std::unique_ptr<Runtime> runtime) {
	if (!runtime) {
		visionRuntime::logs::error("cannot adopt a null runtime session");
		return nullptr;
	}
	return std::make_unique<detail::InspectionSessionAdapter<Runtime>>(std::move(runtime));
}

} // namespace visionService::session
