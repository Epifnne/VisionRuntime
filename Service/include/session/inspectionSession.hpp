#pragma once

#include "runtime/multiCameraSession.hpp"
#include "runtime/runtimeSession.hpp"
#include "runtime/eventInspectionSession.hpp"
#include "vision/anomalyResult.hpp"
#include <type_traits>

namespace visionService::session {

using SessionResult = visionRuntime::vision::AnomalyResult;
using SessionSummary = visionRuntime::runtime::MultiCameraExecutionSummary;
using OutcomeObserver = std::function<void(
	visionRuntime::executor::FrameOutcome,
	const visionRuntime::pipeline::FrameIdentity&,
	std::optional<visionRuntime::executor::TaskId>,
	const visionRuntime::core::Status&)>;

struct SessionObservers {
	visionRuntime::executor::FrameOutcomeCallback<SessionResult> result;
	std::function<void(const visionRuntime::camera::SourceFault&)> sourceFault;
	std::function<void(std::uint32_t, const visionRuntime::vision::Frame&)> frame;
	OutcomeObserver outcome;
};

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
	[[nodiscard]] virtual bool hasSource(std::size_t index) const noexcept { return source(index) != nullptr; }
	[[nodiscard]] virtual visionRuntime::camera::ICameraDevice* cameraDevice(
		std::size_t index) const noexcept {
		const auto* input = source(index);
		return input ? input->cameraDevice() : nullptr;
	}
	[[nodiscard]] virtual bool allowsDirectTrigger(std::size_t) const noexcept { return true; }
	[[nodiscard]] virtual bool isRunning() const {
		for (std::size_t index = 0; index < sourceCount(); ++index) {
			const auto* input = source(index);
			if (input && input->isRunning()) { return true; }
		}
		return false;
	}
	virtual visionRuntime::core::Result<void> addObservers(SessionObservers observers) = 0;
};

namespace detail {

class EventInspectionAdapter final : public IInspectionSession {
public:
	explicit EventInspectionAdapter(std::unique_ptr<visionRuntime::runtime::EventInspectionSession> runtime)
		: runtime_(std::move(runtime)) {}
	visionRuntime::core::Result<void> start() override { return runtime_->start(); }
	void requestStop(visionRuntime::executor::StopMode mode) noexcept override { runtime_->requestStop(mode); }
	SessionSummary wait() override { runtime_->wait(); return currentSummary(); }
	SessionSummary currentSummary() const override { return runtime_->currentSummary().frames; }
	bool stopRequested() const override { return runtime_->stopRequested(); }
	std::size_t sourceCount() const noexcept override { return runtime_->sourceCount(); }
	visionRuntime::camera::IFrameSource* source(std::size_t) const noexcept override { return nullptr; }
	bool hasSource(std::size_t index) const noexcept override { return index < sourceCount(); }
	visionRuntime::camera::ICameraDevice* cameraDevice(std::size_t index) const noexcept override {
		return runtime_->cameraDevice(index);
	}
	bool allowsDirectTrigger(std::size_t) const noexcept override { return false; }
	bool isRunning() const override { return runtime_->isRunning(); }
	visionRuntime::core::Result<void> addObservers(SessionObservers observers) override {
		if (observers.result) {
			return visionRuntime::core::Result<void>::failure(visionRuntime::core::Status::error(
				visionRuntime::core::StatusCode::Unsupported, "event sessions do not produce anomaly results"));
		}
		visionRuntime::executor::FrameObservers<visionRuntime::runtime::ViewDecision> adapted;
		adapted.frame = std::move(observers.frame);
		adapted.sourceFault = std::move(observers.sourceFault);
		adapted.result = [outcome = std::move(observers.outcome)](
			auto kind, const auto& identity, auto task, const auto& result) {
			if (outcome) { outcome(kind, identity, task, result.status()); }
		};
		return runtime_->addObservers(std::move(adapted));
	}
private:
	std::unique_ptr<visionRuntime::runtime::EventInspectionSession> runtime_;
};

template<typename Runtime, typename ResultType = SessionResult>
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
		if constexpr (!std::is_same_v<ResultType, SessionResult>) {
			if (observers.result) {
				return visionRuntime::core::Result<void>::failure(
					visionRuntime::core::Status::error(
						visionRuntime::core::StatusCode::Unsupported,
						"typed anomaly observers cannot observe a custom result session"));
			}
		}
		visionRuntime::executor::FrameObservers<ResultType> adapted;
		adapted.frame = std::move(observers.frame);
		adapted.sourceFault = std::move(observers.sourceFault);
		adapted.result = [typed = std::move(observers.result),
			outcome = std::move(observers.outcome)](
			auto kind, const auto& identity, auto taskId, const auto& result) {
			if constexpr (std::is_same_v<ResultType, SessionResult>) {
				if (typed) {
					typed(kind, identity, taskId, result);
				}
			}
			if (outcome) {
				outcome(kind, identity, taskId, result.status());
			}
		};
		return runtime_->addObservers(std::move(adapted));
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

[[nodiscard]] inline std::unique_ptr<IInspectionSession> adoptEventInspectionSession(
	std::unique_ptr<visionRuntime::runtime::EventInspectionSession> runtime) {
	if (!runtime) {
		visionRuntime::logs::error("cannot adopt a null event inspection session");
		return nullptr;
	}
	return std::make_unique<detail::EventInspectionAdapter>(std::move(runtime));
}

template<typename Runtime>
[[nodiscard]] std::unique_ptr<IInspectionSession> adoptRuntime(
	std::unique_ptr<Runtime> runtime) {
	if (!runtime) {
		visionRuntime::logs::error("cannot adopt a null runtime session");
		return nullptr;
	}
	return std::make_unique<detail::InspectionSessionAdapter<Runtime>>(std::move(runtime));
}

template<typename ResultType, typename Runtime>
[[nodiscard]] std::unique_ptr<IInspectionSession> adoptProductRuntime(
	std::unique_ptr<Runtime> runtime) {
	if (!runtime) {
		visionRuntime::logs::error("cannot adopt a null product runtime session");
		return nullptr;
	}
	return std::make_unique<detail::InspectionSessionAdapter<Runtime, ResultType>>(
		std::move(runtime));
}

} // namespace visionService::session
