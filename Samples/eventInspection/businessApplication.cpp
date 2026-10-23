#include "businessContract.hpp"
#include "simulatedCamera.hpp"
#include "exports/productEntry.hpp"
#include "pipeline/openCvPipelineBuilder.hpp"
#include "runtime/eventInspectionSession.hpp"

#include <opencv2/core.hpp>

#include <array>
#include <condition_variable>
#include <queue>
#include <thread>

namespace visionProduct {
namespace {
using namespace visionRuntime;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

class BrightnessAlgorithm final : public pipeline::IOpenCvAlgorithm<Brightness> {
public:
	core::Result<Brightness> process(const pipeline::PipelinePacket& packet) override {
		const auto* frame = packet.cameraFrame();
		if (!frame || frame->width() != 2 || frame->height() != 2 ||
			frame->pixelFormat() != vision::PixelFormat::Gray8) {
			return core::Result<Brightness>::failure(core::Status::error(
				core::StatusCode::InvalidArgument, "brightness sample requires a 2x2 Gray8 image"));
		}
		const cv::Mat image(2, 2, CV_8UC1, const_cast<void*>(frame->data()), frame->rowStride());
		const auto value = cv::mean(image)[0];
		return core::Result<Brightness>::success(Brightness{value, value >= 100});
	}
};
class EdgeAlgorithm final : public pipeline::IOpenCvAlgorithm<Edges> {
public:
	core::Result<Edges> process(const pipeline::PipelinePacket& packet) override {
		const auto* frame = packet.cameraFrame();
		if (!frame || frame->width() != 2 || frame->height() != 2 ||
			frame->pixelFormat() != vision::PixelFormat::Gray8) {
			return core::Result<Edges>::failure(core::Status::error(
				core::StatusCode::InvalidArgument, "edge sample requires a 2x2 Gray8 image"));
		}
		const cv::Mat image(2, 2, CV_8UC1, const_cast<void*>(frame->data()), frame->rowStride());
		cv::Mat difference;
		cv::absdiff(image.col(0), image.col(1), difference);
		return core::Result<Edges>::success(Edges{cv::countNonZero(difference)});
	}
};
template<typename T, typename Algorithm>
core::Result<std::unique_ptr<pipeline::IVisionPipeline<T>>> makePipeline() {
	pipeline::OpenCvPipelineBuilder<T> builder;
	auto built = builder.setAlgorithm(std::make_unique<Algorithm>()).build();
	if (!built) {
		return core::Result<std::unique_ptr<pipeline::IVisionPipeline<T>>>::failure(built.status());
	}
	return core::Result<std::unique_ptr<pipeline::IVisionPipeline<T>>>::success(
		std::make_unique<pipeline::OpenCvPipeline<T>>(std::move(built).value()));
}

struct SharedState {
	std::mutex mutex;
	std::condition_variable ready;
	Summary summary;
	std::size_t completed = 0;
	bool stop = false;
	std::string error;
};
struct SimulationOptions {
	bool missingView = false;
	std::array<std::chrono::milliseconds, 2> delays{10ms, 70ms};
};

// The business owns sensor input and delayed scheduling; Service only manages
// this object's lifecycle, and the framework owns acquisition/routing.
class BusinessSession final : public visionService::session::IInspectionSession {
public:
	BusinessSession(std::unique_ptr<runtime::EventInspectionSession> runtime,
		std::shared_ptr<SharedState> state, SimulationOptions options)
		: runtime_(runtime.get()), session_(visionService::session::adoptEventInspectionSession(
			std::move(runtime))), state_(std::move(state)), options_(options) {}
	~BusinessSession() override { requestStop(executor::StopMode::Immediate); wait(); }
	core::Result<void> start() override {
		auto started = session_->start();
		if (!started) { return started; }
		try { scheduler_ = std::thread([this] { schedule(); }); }
		catch (const std::exception& exception) {
			requestStop(executor::StopMode::Immediate);
			return core::Result<void>::failure(core::Status::error(
				core::StatusCode::ResourceExhausted,
				std::string("cannot start business scheduler: ") + exception.what()));
		}
		return started;
	}
	void requestStop(executor::StopMode mode) noexcept override {
		{
			std::lock_guard lock(state_->mutex);
			state_->stop = true;
		}
		state_->ready.notify_all();
		session_->requestStop(mode);
	}
	visionService::session::SessionSummary wait() override {
		// Wait on Runtime first: a terminal acquisition fault also stops the
		// business scheduler, even when no more sensor events are arriving.
		auto result = session_->wait();
		{
			std::lock_guard lock(state_->mutex);
			state_->stop = true;
		}
		state_->ready.notify_all();
		if (scheduler_.joinable()) { scheduler_.join(); }
		{
			std::lock_guard lock(state_->mutex);
			if (!state_->error.empty()) { result.lastError = state_->error; }
		}
		return result;
	}
	visionService::session::SessionSummary currentSummary() const override {
		return session_->currentSummary();
	}
	bool stopRequested() const override { return session_->stopRequested(); }
	bool isRunning() const override { return session_->isRunning(); }
	std::size_t sourceCount() const noexcept override { return session_->sourceCount(); }
	camera::IFrameSource* source(std::size_t) const noexcept override { return nullptr; }
	bool hasSource(std::size_t index) const noexcept override { return session_->hasSource(index); }
	camera::ICameraDevice* cameraDevice(std::size_t index) const noexcept override {
		return session_->cameraDevice(index);
	}
	bool allowsDirectTrigger(std::size_t) const noexcept override { return false; }
	core::Result<void> addObservers(visionService::session::SessionObservers observers) override {
		return session_->addObservers(std::move(observers));
	}
private:
	struct Event {
		Clock::time_point at;
		std::uint64_t workpiece;
		std::optional<std::uint32_t> view;
		std::uint64_t inspection;
		bool operator<(const Event& other) const { return at > other.at; }
	};
	void fail(const core::Status& status) {
		logs::report(status);
		{
			std::lock_guard lock(state_->mutex);
			state_->error = status.toString();
		}
		requestStop(executor::StopMode::Immediate);
	}
	void schedule() noexcept {
		try {
			std::priority_queue<Event> events;
			const auto start = Clock::now();
			for (std::uint64_t workpiece = 1; workpiece <= 4; ++workpiece) {
				events.push({start + 25ms * (workpiece - 1), workpiece, {}, 0});
			}
			while (!events.empty()) {
				const auto event = events.top();
				events.pop();
				{
					std::unique_lock lock(state_->mutex);
					if (state_->ready.wait_until(lock, event.at, [this] { return state_->stop; })) { return; }
				}
				if (!event.view) {
					auto opened = runtime_->beginInspection(event.workpiece, {0, 1}, 1s);
					if (!opened) {
						if (!runtime_->stopRequested()) { fail(opened.status()); }
						return;
					}
					for (std::uint32_t view = 0; view < 2; ++view) {
						if (options_.missingView && event.workpiece == 4 && view == 1) { continue; }
						events.push({event.at + options_.delays[view], event.workpiece, view, opened.value()});
					}
				} else {
					auto capture = runtime_->requestCapture(event.inspection, *event.view, event.at + 200ms);
					if (!capture) {
						if (!runtime_->stopRequested()) { fail(capture.status()); }
						return;
					}
				}
			}
			{
				std::unique_lock lock(state_->mutex);
				state_->ready.wait(lock, [this] { return state_->stop || state_->completed == 4; });
			}
			requestStop(executor::StopMode::Graceful);
		} catch (const std::exception& exception) {
			fail(core::Status::error(core::StatusCode::Internal,
				std::string("business scheduler threw: ") + exception.what()));
		} catch (...) {
			fail(core::Status::error(core::StatusCode::Internal, "business scheduler threw an unknown exception"));
		}
	}
	runtime::EventInspectionSession* runtime_;
	std::unique_ptr<visionService::session::IInspectionSession> session_;
	std::shared_ptr<SharedState> state_;
	SimulationOptions options_;
	std::thread scheduler_;
};

template<typename T>
auto typedObserver(std::shared_ptr<visionService::exports::ExportedState<T>> state,
	std::shared_ptr<SharedState> shared) {
	return [state, shared](const pipeline::FrameIdentity& identity, const core::Result<T>& result) {
		if (!result) { return; } // Errors are delivered through frame outcomes and final FAULT.
		auto value = result.value();
		value.workpieceId = identity.metadata.capture->workpieceId;
		value.inspectionId = identity.metadata.capture->inspectionId;
		value.captureId = identity.metadata.capture->captureId;
		auto published = state->set(value);
		if (!published) {
			logs::report(published.status());
			std::lock_guard lock(shared->mutex);
			shared->error = published.status().toString();
		}
	};
}
}

core::Result<visionService::ProductApplication> createApplication(
	const visionService::profile::ProductProfile& profile) {
	using Application = visionService::ProductApplication;
	auto manifest = describeApplication(profile);
	if (!manifest) { return core::Result<Application>::failure(manifest.status()); }
	SimulationOptions options;
	try {
		const auto document = nlohmann::json::parse(profile.documentJson);
		const auto simulation = document.value("simulation", nlohmann::json::object());
		options.missingView = simulation.value("missingView", false);
		for (std::size_t index = 0; index < 2; ++index) {
			const auto key = index == 0 ? "topDelayMilliseconds" : "sideDelayMilliseconds";
			const auto value = simulation.value(key, index == 0 ? 10 : 70);
			if (value < 0 || value > 500) {
				return core::Result<Application>::failure(core::Status::error(
					core::StatusCode::InvalidArgument, "sample capture delays must be in [0, 500] ms"));
			}
			options.delays[index] = std::chrono::milliseconds(value);
		}
	} catch (const nlohmann::json::exception& exception) {
		return core::Result<Application>::failure(core::Status::error(core::StatusCode::InvalidArgument,
			std::string("invalid simulation configuration: ") + exception.what()));
	}
	Application application;
	application.expectedManifest = manifest.value();
	const auto& contract = businessContract();
	auto topState = contract.top.bind(application.exports, Brightness{});
	auto sideState = contract.side.bind(application.exports, Edges{});
	auto resultState = contract.result.bind(application.exports, Summary{});
	if (!topState || !sideState || !resultState) {
		return core::Result<Application>::failure(!topState ? topState.status() :
			!sideState ? sideState.status() : resultState.status());
	}
	auto shared = std::make_shared<SharedState>();
	auto runtime = std::make_unique<runtime::EventInspectionSession>(
		[state = resultState.value(), shared](const runtime::InspectionResult& result) {
			std::lock_guard lock(shared->mutex);
			auto& summary = shared->summary;
			summary.workpieceId = result.workpieceId;
			summary.inspectionId = result.inspectionId;
			switch (result.decision) {
			case runtime::InspectionDecision::Ok: summary.decision = "OK"; ++summary.okCount; break;
			case runtime::InspectionDecision::Ng: summary.decision = "NG"; ++summary.ngCount; break;
			case runtime::InspectionDecision::Fault: summary.decision = "FAULT"; ++summary.faultCount; break;
			}
			nlohmann::json views = nlohmann::json::array();
			for (const auto& view : result.views) {
				views.push_back({{"viewId", view.viewId}, {"captureId", view.capture
					? nlohmann::json(view.capture->captureId) : nlohmann::json(nullptr)},
					{"decision", !view.status ? "FAULT" :
						view.decision == runtime::ViewDecision::Ok ? "OK" : "NG"},
					{"status", view.status.toString()}});
			}
			summary.views = views.dump();
			auto published = state->set(summary);
			if (!published) { logs::report(published.status()); shared->error = published.status().toString(); }
			++shared->completed;
			shared->ready.notify_all();
		});
	auto topSource = camera::EventTriggerSource::create(
		std::make_unique<eventInspectionSample::SimulatedCamera>(false));
	auto sideSource = camera::EventTriggerSource::create(
		std::make_unique<eventInspectionSample::SimulatedCamera>(true));
	auto topPipeline = makePipeline<Brightness, BrightnessAlgorithm>();
	auto sidePipeline = makePipeline<Edges, EdgeAlgorithm>();
	if (!topSource || !sideSource || !topPipeline || !sidePipeline) {
		return core::Result<Application>::failure(!topSource ? topSource.status() :
			!sideSource ? sideSource.status() : !topPipeline ? topPipeline.status() : sidePipeline.status());
	}
	auto top = runtime->addRoute<Brightness>(0, std::move(topSource).value(), std::move(topPipeline).value(),
		[](const Brightness& value) { return core::Result<runtime::ViewDecision>::success(
			value.accepted ? runtime::ViewDecision::Ok : runtime::ViewDecision::Ng); },
		typedObserver(topState.value(), shared));
	auto side = runtime->addRoute<Edges>(1, std::move(sideSource).value(), std::move(sidePipeline).value(),
		[](const Edges& value) { return core::Result<runtime::ViewDecision>::success(
			value.transitions == 2 ? runtime::ViewDecision::Ok : runtime::ViewDecision::Ng); },
		typedObserver(sideState.value(), shared));
	if (!top || !side) { return core::Result<Application>::failure(!top ? top.status() : side.status()); }
	application.session = std::make_unique<BusinessSession>(std::move(runtime), shared, options);
	return core::Result<Application>::success(std::move(application));
}
} // namespace visionProduct
