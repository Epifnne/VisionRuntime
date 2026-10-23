#include "runtime/eventInspectionSession.hpp"
#include "core/completionDispatcher.hpp"
#include "logs/logger.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <iterator>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace visionRuntime::runtime {
namespace {

using Clock = std::chrono::steady_clock;
using IdResult = core::Result<std::uint64_t>;
std::atomic_uint64_t nextInspectionId{1};
std::atomic_uint64_t nextCaptureId{1};

core::Status error(core::StatusCode code, const char* message) {
	return core::Status::error(code, message);
}

} // namespace

class EventInspectionSession::Impl {
public:
	Impl(ResultObserver observer, EventInspectionOptions options)
		: observer_(std::move(observer)), options_(options),
		  deliveries_(options.maxInspectionsInFlight ? options.maxInspectionsInFlight : 1) {}

	core::Result<void> addObservers(executor::FrameObservers<ViewDecision> observers) {
		std::lock_guard lock(mutex_);
		if (started_ || stopped_) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidState, "observers must be attached before start"));
		}
		observers_.push_back(std::move(observers));
		return core::Result<void>::success();
	}

	EventInspectionSummary currentSummary() const {
		std::lock_guard lock(mutex_);
		return summary_;
	}
	bool stopRequested() const {
		std::lock_guard lock(mutex_);
		return stopped_;
	}
	bool isRunning() const {
		std::lock_guard lock(mutex_);
		return running_ && !stopped_;
	}
	std::size_t sourceCount() const noexcept { return routes_.size(); }
	std::uint32_t viewId(std::size_t index) const {
		if (index >= routes_.size()) { throw std::out_of_range("event source index is out of range"); }
		auto entry = routes_.begin();
		std::advance(entry, index);
		return entry->first;
	}
	camera::ICameraDevice* cameraDevice(std::size_t index) const noexcept {
		if (index >= routes_.size()) { return nullptr; }
		return routes_.at(viewId(index)).source->cameraDevice();
	}

	core::Result<void> addRoute(std::uint32_t viewId,
		std::unique_ptr<camera::EventTriggerSource> source,
		std::unique_ptr<executor::IPipelineExecutor<ViewDecision>> executor) {
		std::lock_guard lock(mutex_);
		if (started_ || stopped_) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidState, "routes must be configured before start"));
		}
		if (routes_.contains(viewId)) {
			return core::Result<void>::failure(error(
				core::StatusCode::AlreadyExists, "duplicate view route"));
		}
		routes_.emplace(viewId, Route{std::move(source), std::move(executor)});
		return core::Result<void>::success();
	}

	core::Result<void> start() {
		std::lock_guard lifecycle(lifecycleMutex_);
		{
			std::lock_guard lock(mutex_);
			if (started_ || stopped_) {
				return core::Result<void>::failure(error(
					core::StatusCode::InvalidState, "inspection session is single-use"));
			}
			if (!observer_ || !options_.maxInspectionsInFlight || routes_.empty()) {
				return core::Result<void>::failure(error(
					core::StatusCode::InvalidArgument, "session requires observer, capacity and routes"));
			}
			started_ = true;
			summary_.frames.receivedPerSource.resize(routes_.size());
			summary_.frames.droppedPerSource.resize(routes_.size());
			summary_.frames.missingPerSource.resize(routes_.size());
		}
		for (auto& [viewId, route] : routes_) {
			auto started = route.source->start(
				[this, viewId](const camera::CaptureRequest& request, auto frame) {
					onCapture(viewId, request, std::move(frame));
				},
				[this, viewId](const core::Status& status) {
					observeSourceFault(viewId, status, true);
					requestStop();
				});
			if (!started) {
				requestStop();
				return started;
			}
		}
		try {
			std::lock_guard lock(mutex_);
			timer_ = std::thread([this] { runTimer(); });
			running_ = !stopped_;
		} catch (const std::exception& exception) {
			requestStop();
			return core::Result<void>::failure(core::Status::error(
				core::StatusCode::ResourceExhausted,
				std::string("cannot start inspection deadline worker: ") + exception.what()));
		}
		return core::Result<void>::success();
	}

	IdResult begin(std::uint64_t workpieceId, std::vector<std::uint32_t> views,
		std::chrono::milliseconds timeout) {
		std::lock_guard lock(mutex_);
		if (!running_ || stopped_) {
			return IdResult::failure(error(
				core::StatusCode::InvalidState, "inspection session is not running"));
		}
		if (!workpieceId || views.empty() || timeout.count() <= 0 || timeout > std::chrono::hours(24)) {
			return IdResult::failure(error(core::StatusCode::InvalidArgument,
				"inspection requires a workpiece, views and timeout in (0, 24h]"));
		}
		if (inspections_.size() + pendingDeliveries_ >= options_.maxInspectionsInFlight) {
			return IdResult::failure(error(core::StatusCode::QueueFull, "inspection capacity exhausted"));
		}
		Pending inspection{.workpieceId = workpieceId, .deadline = Clock::now() + timeout};
		for (const auto view : views) {
			if (!routes_.contains(view) || !inspection.views.emplace(view,
				ViewInspectionResult{.viewId = view}).second) {
				return IdResult::failure(error(core::StatusCode::InvalidArgument,
					"inspection views must be unique configured routes"));
			}
		}
		const auto id = nextInspectionId.fetch_add(1);
		inspections_.emplace(id, std::move(inspection));
		++summary_.inspectionsStarted;
		++summary_.inspectionsInFlight;
		ready_.notify_all();
		return IdResult::success(id);
	}

	core::Result<vision::CaptureIdentity> request(std::uint64_t inspectionId, std::uint32_t viewId,
		std::optional<Clock::time_point> captureDeadline) {
		using Result = core::Result<vision::CaptureIdentity>;
		camera::CaptureRequest request;
		camera::EventTriggerSource* source;
		{
			std::lock_guard lock(mutex_);
			if (!running_ || stopped_) {
				return Result::failure(error(core::StatusCode::InvalidState, "session is stopping"));
			}
			auto found = inspections_.find(inspectionId);
			if (found == inspections_.end()) {
				return Result::failure(error(core::StatusCode::NotFound, "unknown or finished inspection"));
			}
			auto& pending = found->second;
			if (Clock::now() >= pending.deadline) {
				return Result::failure(error(core::StatusCode::DeadlineExceeded, "inspection deadline expired"));
			}
			const auto deadline = std::min(pending.deadline, captureDeadline.value_or(pending.deadline));
			if (deadline <= Clock::now()) {
				return Result::failure(error(core::StatusCode::DeadlineExceeded, "capture deadline expired"));
			}
			auto view = pending.views.find(viewId);
			if (view == pending.views.end()) {
				return Result::failure(error(core::StatusCode::InvalidArgument, "view is not required by inspection"));
			}
			if (view->second.capture) {
				return Result::failure(error(core::StatusCode::AlreadyExists, "view capture already requested"));
			}
			request = {{pending.workpieceId, inspectionId, nextCaptureId.fetch_add(1), viewId},
				deadline};
			view->second.capture = request.identity;
			source = routes_.at(viewId).source.get();
		}
		auto accepted = source->request(request);
		if (!accepted) {
			complete(request.identity, std::nullopt, core::Result<ViewDecision>::failure(accepted.status()));
			return Result::failure(accepted.status());
		}
		return Result::success(request.identity);
	}

	void requestStop(executor::StopMode mode = executor::StopMode::Graceful) noexcept {
		{
			std::lock_guard lock(mutex_);
			stopped_ = true;
			running_ = false;
			for (auto& [id, route] : routes_) {
				route.source->requestStop();
				if (mode == executor::StopMode::Immediate) {
					route.executor->requestStop(mode);
				}
			}
		}
		ready_.notify_all();
	}

	void wait() noexcept {
		{
			std::unique_lock lock(mutex_);
			ready_.wait(lock, [this] { return stopped_; });
		}
		std::lock_guard lifecycle(lifecycleMutex_);
		if (timer_.joinable()) {
			timer_.join();
		}
		for (auto& [id, route] : routes_) {
			route.source->wait();
			route.executor->requestStop();
			route.executor->wait();
		}
		std::vector<InspectionResult> cancelled;
		{
			std::lock_guard lock(mutex_);
			for (auto& [id, pending] : inspections_) {
				cancelled.push_back(finish(id, pending, error(
					core::StatusCode::Cancelled, "inspection cancelled before all views completed")));
			}
			inspections_.clear();
		}
		for (auto& result : cancelled) {
			deliver(std::move(result));
		}
		{
			std::unique_lock lock(mutex_);
			ready_.wait(lock, [this] { return pendingDeliveries_ == 0; });
		}
		deliveries_.closeInput();
		deliveries_.wait();
	}

private:
	struct Route {
		std::unique_ptr<camera::EventTriggerSource> source;
		std::unique_ptr<executor::IPipelineExecutor<ViewDecision>> executor;
		camera::FrameSequenceTracker sequence;
	};
	struct Pending {
		std::uint64_t workpieceId;
		Clock::time_point deadline;
		std::map<std::uint32_t, ViewInspectionResult> views;
	};

	static bool done(const ViewInspectionResult& view) {
		return view.decision.has_value() || !view.status.isOk();
	}

	InspectionResult finish(std::uint64_t id, Pending& pending, const core::Status& missing) {
		++pendingDeliveries_;
		InspectionResult result{pending.workpieceId, id, InspectionDecision::Ok, {}};
		for (auto& [viewId, view] : pending.views) {
			if (!done(view)) {
				view.status = missing;
			}
			if (!view.status) {
				result.decision = InspectionDecision::Fault;
			} else if (view.decision == ViewDecision::Ng && result.decision != InspectionDecision::Fault) {
				result.decision = InspectionDecision::Ng;
			}
			result.views.push_back(std::move(view));
		}
		--summary_.inspectionsInFlight;
		++summary_.inspectionsCompleted;
		switch (result.decision) {
		case InspectionDecision::Ok: ++summary_.ok; break;
		case InspectionDecision::Ng: ++summary_.ng; break;
		case InspectionDecision::Fault: ++summary_.fault; break;
		}
		return result;
	}

	template<typename Callback>
	void notify(Callback callback) noexcept {
		for (const auto& observer : observers_) {
			try { callback(observer); }
			catch (const std::exception& exception) {
				logs::error(std::string("event session observer threw: ") + exception.what());
			} catch (...) { logs::error("event session observer threw an unknown exception"); }
		}
	}

	void observeOutcome(const pipeline::FrameIdentity& identity,
		std::optional<executor::TaskId> taskId, const core::Result<ViewDecision>& result) {
		{
			std::lock_guard lock(mutex_);
			if (result) { ++summary_.frames.completed; }
			else {
				++summary_.frames.failed;
				summary_.frames.lastError = result.status().toString();
				if (!taskId) { ++summary_.frames.rejected; }
				if (result.status().code() == core::StatusCode::DeadlineExceeded) {
					++summary_.frames.expired;
				}
			}
		}
		notify([&](const auto& observer) {
			if (observer.result) {
				observer.result(taskId ? executor::FrameOutcome::Completed : executor::FrameOutcome::Rejected,
					identity, taskId, result);
			}
		});
	}

	void deliver(InspectionResult result) {
		if (!deliveries_.dispatch(core::Result<InspectionResult>::success(std::move(result)),
			[this](auto delivered) { observer_(delivered.value()); })) {
			logs::error("inspection result delivery closed unexpectedly");
		}
		{
			std::lock_guard lock(mutex_);
			--pendingDeliveries_;
		}
		ready_.notify_all();
	}

	void complete(const vision::CaptureIdentity& capture,
		std::optional<pipeline::FrameIdentity> frame, const core::Result<ViewDecision>& result) {
		std::optional<InspectionResult> finished;
		{
			std::lock_guard lock(mutex_);
			auto found = inspections_.find(capture.inspectionId);
			if (found == inspections_.end()) {
				logs::error("discarded late inspection result for capture " + std::to_string(capture.captureId));
				return;
			}
			auto& pending = found->second;
			auto view = pending.views.find(capture.viewId);
			if (view == pending.views.end() || view->second.capture != capture || done(view->second)) {
				logs::error("discarded duplicate or mismatched inspection result");
				return;
			}
			const auto timeout = error(core::StatusCode::DeadlineExceeded,
				"inspection deadline expired before all views completed");
			if (stopped_) {
				finished = finish(found->first, pending, error(
					core::StatusCode::Cancelled, "inspection stopped before all views completed"));
			} else if (Clock::now() >= pending.deadline) {
				finished = finish(found->first, pending, timeout);
			} else {
				view->second.frame = std::move(frame);
				if (result) {
					view->second.decision = result.value();
				} else {
					view->second.status = result.status();
				}
				bool all = true;
				for (const auto& [id, value] : pending.views) {
					all = all && done(value);
				}
				if (all) {
					finished = finish(found->first, pending, timeout);
				}
			}
			if (finished) {
				inspections_.erase(found);
			}
		}
		if (finished) {
			deliver(std::move(*finished));
		}
	}

	void onCapture(std::uint32_t viewId, const camera::CaptureRequest& request,
		core::Result<vision::Frame> frame) {
		if (!frame) {
			if (frame.status().code() != core::StatusCode::Cancelled &&
				!routes_.at(viewId).source->faulted()) {
				observeSourceFault(viewId, frame.status(), false);
			}
			complete(request.identity, std::nullopt, core::Result<ViewDecision>::failure(frame.status()));
			return;
		}
		const auto index = sourceIndex(viewId);
		std::optional<camera::SourceFault> gap;
		{
			std::lock_guard lock(mutex_);
			++summary_.frames.received;
			++summary_.frames.receivedPerSource[index];
			auto& sequence = routes_.at(viewId).sequence;
			const auto previous = sequence.previous();
			const auto missing = sequence.observe(frame->metadata().sequenceNumber);
			if (missing) {
				summary_.frames.missingFrames += missing;
				summary_.frames.missingPerSource[index] += missing;
				gap = camera::SourceFault{.sourceId = index,
					.status = error(core::StatusCode::DataLoss, "event camera frame sequence gap"),
					.missingFrames = missing, .previousSequence = previous,
					.currentSequence = frame->metadata().sequenceNumber};
			}
			if (!inspections_.contains(request.identity.inspectionId) || stopped_) {
				logs::error("discarded capture for a finished or stopping inspection");
				return;
			}
		}
		if (gap) {
			notify([&](const auto& observer) { if (observer.sourceFault) { observer.sourceFault(*gap); } });
		}
		notify([&](const auto& observer) {
			if (observer.frame) { observer.frame(index, frame.value()); }
		});
		pipeline::PipelinePacket packet(std::move(frame).value(), {}, index);
		const auto identity = packet.identity();
		auto submitted = routes_.at(viewId).executor->submit(std::move(packet),
			[this, capture = request.identity, identity](auto taskId, const auto& result) {
				observeOutcome(identity, taskId, result);
				complete(capture, identity, result);
			});
		if (!submitted) {
			observeOutcome(identity, std::nullopt, core::Result<ViewDecision>::failure(submitted.status()));
			complete(request.identity, identity, core::Result<ViewDecision>::failure(submitted.status()));
		} else {
			std::lock_guard lock(mutex_);
			++summary_.frames.submitted;
		}
	}

	std::uint32_t sourceIndex(std::uint32_t view) const {
		return static_cast<std::uint32_t>(std::distance(routes_.begin(), routes_.find(view)));
	}

	void observeSourceFault(std::uint32_t view, const core::Status& status, bool terminal) {
		{
			std::lock_guard lock(mutex_);
			++summary_.frames.sourceFailures;
			summary_.frames.lastError = status.toString();
		}
		notify([&](const auto& observer) {
			if (observer.sourceFault) {
				observer.sourceFault({.sourceId = sourceIndex(view), .status = status, .terminal = terminal});
			}
		});
	}

	void runTimer() {
		std::unique_lock lock(mutex_);
		while (!stopped_) {
			if (inspections_.empty()) {
				ready_.wait(lock, [this] { return stopped_ || !inspections_.empty(); });
				continue;
			}
			auto next = Clock::time_point::max();
			for (const auto& [id, pending] : inspections_) {
				next = std::min(next, pending.deadline);
			}
			ready_.wait_until(lock, next);
			std::vector<InspectionResult> expired;
			for (auto entry = inspections_.begin(); entry != inspections_.end();) {
				if (Clock::now() >= entry->second.deadline) {
					expired.push_back(finish(entry->first, entry->second, error(
						core::StatusCode::DeadlineExceeded, "inspection timed out waiting for views")));
					entry = inspections_.erase(entry);
				} else {
					++entry;
				}
			}
			lock.unlock();
			for (auto& result : expired) {
				deliver(std::move(result));
			}
			lock.lock();
		}
	}

	ResultObserver observer_;
	EventInspectionOptions options_;
	core::CompletionDispatcher<InspectionResult> deliveries_;
	std::mutex lifecycleMutex_;
	mutable std::mutex mutex_;
	std::condition_variable ready_;
	std::thread timer_;
	std::map<std::uint32_t, Route> routes_;
	std::map<std::uint64_t, Pending> inspections_;
	std::vector<executor::FrameObservers<ViewDecision>> observers_;
	EventInspectionSummary summary_;
	// Finalized results must be enqueued before wait() can close delivery.
	std::size_t pendingDeliveries_ = 0;
	bool started_ = false;
	bool running_ = false;
	bool stopped_ = false;
};

EventInspectionSession::EventInspectionSession(ResultObserver observer, EventInspectionOptions options)
	: impl_(std::make_unique<Impl>(std::move(observer), options)) {}
EventInspectionSession::~EventInspectionSession() { requestStop(); wait(); }
core::Result<void> EventInspectionSession::addRouteExecutor(std::uint32_t viewId,
	std::unique_ptr<camera::EventTriggerSource> source,
	std::unique_ptr<executor::IPipelineExecutor<ViewDecision>> executor) {
	return impl_->addRoute(viewId, std::move(source), std::move(executor));
}
core::Result<void> EventInspectionSession::start() { return impl_->start(); }
core::Result<void> EventInspectionSession::addObservers(executor::FrameObservers<ViewDecision> observers) {
	return impl_->addObservers(std::move(observers));
}
EventInspectionSummary EventInspectionSession::currentSummary() const { return impl_->currentSummary(); }
bool EventInspectionSession::stopRequested() const { return impl_->stopRequested(); }
std::size_t EventInspectionSession::sourceCount() const noexcept { return impl_->sourceCount(); }
std::uint32_t EventInspectionSession::viewId(std::size_t index) const {
	if (index >= sourceCount()) { throw std::out_of_range("event source index"); }
	return impl_->viewId(index);
}
camera::ICameraDevice* EventInspectionSession::cameraDevice(std::size_t index) const noexcept {
	return impl_->cameraDevice(index);
}
core::Result<std::uint64_t> EventInspectionSession::beginInspection(
	std::uint64_t workpieceId, std::vector<std::uint32_t> views, std::chrono::milliseconds timeout) {
	return impl_->begin(workpieceId, std::move(views), timeout);
}
core::Result<vision::CaptureIdentity> EventInspectionSession::requestCapture(
	std::uint64_t inspectionId, std::uint32_t viewId,
	std::optional<std::chrono::steady_clock::time_point> captureDeadline) {
	return impl_->request(inspectionId, viewId, captureDeadline);
}
void EventInspectionSession::requestStop(executor::StopMode mode) noexcept { impl_->requestStop(mode); }
bool EventInspectionSession::isRunning() const { return impl_->isRunning(); }
void EventInspectionSession::wait() noexcept { impl_->wait(); }

} // namespace visionRuntime::runtime
