#pragma once

#include "camera/eventTriggerSource.hpp"
#include "executor/serialPipelineExecutor.hpp"
#include "executor/frameObservers.hpp"
#include "runtime/multiCameraSession.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace visionRuntime::runtime {

enum class ViewDecision { Ok, Ng };
enum class InspectionDecision { Ok, Ng, Fault };

struct ViewInspectionResult {
	std::uint32_t viewId = 0;
	std::optional<vision::CaptureIdentity> capture;
	std::optional<pipeline::FrameIdentity> frame;
	std::optional<ViewDecision> decision;
	core::Status status;
};

struct InspectionResult {
	std::uint64_t workpieceId = 0;
	std::uint64_t inspectionId = 0;
	InspectionDecision decision = InspectionDecision::Fault;
	std::vector<ViewInspectionResult> views;
};

struct EventInspectionOptions {
	std::size_t maxInspectionsInFlight = 128;
};

struct EventInspectionSummary {
	MultiCameraExecutionSummary frames;
	std::size_t inspectionsStarted = 0;
	std::size_t inspectionsInFlight = 0;
	std::size_t inspectionsCompleted = 0;
	std::size_t ok = 0;
	std::size_t ng = 0;
	std::size_t fault = 0;
};

namespace detail {

template<typename ResultType>
class InspectionPipelineAdapter final : public pipeline::IVisionPipeline<ViewDecision> {
public:
	using Mapper = std::function<core::Result<ViewDecision>(const ResultType&)>;
	using Observer = std::function<void(
		const pipeline::FrameIdentity&, const core::Result<ResultType>&)>;

	InspectionPipelineAdapter(std::unique_ptr<pipeline::IVisionPipeline<ResultType>> pipeline,
		Mapper mapper, Observer observer)
		: pipeline_(std::move(pipeline)), mapper_(std::move(mapper)), observer_(std::move(observer)) {}

	core::Result<ViewDecision> run(pipeline::PipelinePacket packet) override {
		const auto identity = packet.identity();
		auto result = pipeline_->run(std::move(packet));
		if (observer_) {
			observer_(identity, result);
		}
		if (!result) {
			return core::Result<ViewDecision>::failure(result.status());
		}
		auto mapped = mapper_(result.value());
		if (mapped && mapped.value() != ViewDecision::Ok && mapped.value() != ViewDecision::Ng) {
			return core::Result<ViewDecision>::failure(core::Status::error(
				core::StatusCode::InvalidArgument, "pipeline adapter returned an invalid decision"));
		}
		return mapped;
	}

	void finishBatch() noexcept override { pipeline_->finishBatch(); }

private:
	std::unique_ptr<pipeline::IVisionPipeline<ResultType>> pipeline_;
	Mapper mapper_;
	Observer observer_;
};

} // namespace detail

// Does not listen to sensors or calculate conveyor delays. Business code opens
// an inspection, then requests each view when its external trigger is due.
class EventInspectionSession {
public:
	using ResultObserver = std::function<void(const InspectionResult&)>;

	explicit EventInspectionSession(ResultObserver observer, EventInspectionOptions options = {});
	~EventInspectionSession();
	EventInspectionSession(const EventInspectionSession&) = delete;
	EventInspectionSession& operator=(const EventInspectionSession&) = delete;

	template<typename ResultType>
	[[nodiscard]] core::Result<void> addRoute(
		std::uint32_t viewId, std::unique_ptr<camera::EventTriggerSource> source,
		std::unique_ptr<pipeline::IVisionPipeline<ResultType>> pipeline,
		typename detail::InspectionPipelineAdapter<ResultType>::Mapper mapper,
		typename detail::InspectionPipelineAdapter<ResultType>::Observer observer = {},
		executor::ExecutorOptions options = {}) {
		if (!source || !pipeline || !mapper || !options.queueCapacity ||
			(options.taskTimeout && (options.taskTimeout->count() <= 0 ||
				*options.taskTimeout > std::chrono::hours(24)))) {
			return core::Result<void>::failure(core::Status::error(
				core::StatusCode::InvalidArgument,
				"route requires source, pipeline, mapper, queue capacity and a valid task timeout"));
		}
		// A source callback must never block on a busy inference queue.
		options.queueFullPolicy = executor::QueueFullPolicy::Drop;
		auto adapted = std::make_unique<detail::InspectionPipelineAdapter<ResultType>>(
			std::move(pipeline), std::move(mapper), std::move(observer));
		return addRouteExecutor(viewId, std::move(source),
			std::make_unique<executor::SerialPipelineExecutor<ViewDecision>>(
				std::move(adapted), options));
	}

	[[nodiscard]] core::Result<void> start();
	[[nodiscard]] core::Result<void> addObservers(executor::FrameObservers<ViewDecision> observers);
	[[nodiscard]] EventInspectionSummary currentSummary() const;
	[[nodiscard]] bool stopRequested() const;
	[[nodiscard]] bool isRunning() const;
	// Configure routes on one thread; do not change them while accessing sources.
	[[nodiscard]] std::size_t sourceCount() const noexcept;
	[[nodiscard]] std::uint32_t viewId(std::size_t sourceIndex) const;
	[[nodiscard]] camera::ICameraDevice* cameraDevice(std::size_t sourceIndex) const noexcept;
	[[nodiscard]] core::Result<std::uint64_t> beginInspection(
		std::uint64_t workpieceId, std::vector<std::uint32_t> requiredViews,
		std::chrono::milliseconds timeout);
	// The optional deadline bounds capture queueing/response, not inference.
	[[nodiscard]] core::Result<vision::CaptureIdentity> requestCapture(
		std::uint64_t inspectionId, std::uint32_t viewId,
		std::optional<std::chrono::steady_clock::time_point> captureDeadline = {});
	// Both modes cancel incomplete inspections as Fault. Graceful drains accepted
	// pipeline work; Immediate also requests executor cancellation. wait() only
	// waits for an explicit stop, then drains all callbacks. Never call it from an observer.
	void requestStop(executor::StopMode mode = executor::StopMode::Graceful) noexcept;
	void wait() noexcept;

private:
	core::Result<void> addRouteExecutor(std::uint32_t viewId,
		std::unique_ptr<camera::EventTriggerSource> source,
		std::unique_ptr<executor::IPipelineExecutor<ViewDecision>> executor);
	class Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace visionRuntime::runtime
