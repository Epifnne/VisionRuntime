#include "service/visionService.hpp"
#include "camera/fileSource.hpp"
#include "runtime/runtimeFactory.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {
using namespace visionRuntime;
using visionService::session::IInspectionSession;
using visionService::session::SessionLifecycle;
using SessionPointer = std::unique_ptr<IInspectionSession>;

class ProductFixture {
public:
	explicit ProductFixture(std::size_t count = 1)
		: root(std::filesystem::temp_directory_path() /
			("runtime-adoption-" + std::to_string(
				std::chrono::steady_clock::now().time_since_epoch().count()))) {
		std::filesystem::create_directories(root / "image");
		for (int index = 0; index < 6; ++index) {
			std::ofstream file(root / "image" / (std::to_string(index) + ".ppm"),
				std::ios::binary);
			file << "P6\n1 1\n255\n";
			file.write("\0\0\0", 3);
		}
		document = {
			{"schemaVersion", {{"major", 1}, {"minor", 0}}},
			{"id", "adoption-test"},
			{"sources", nlohmann::json::array()},
			{"model", {{"packagePath", "must-not-be-loaded"},
				{"backendId", "custom"}, {"device", "CPU"}, {"pluginDirectory", "missing"}}},
			{"pipeline", {{"mean", {0.449}}, {"standardDeviation", {0.226}}}}};
		for (std::size_t index = 0; index < count; ++index) {
			document["sources"].push_back({{"id", "files" + std::to_string(index)},
				{"type", "directory"}, {"directory", "image"}, {"extensions", {".ppm"}},
				{"loop", false}, {"frameIntervalMilliseconds", 2}});
		}
		save();
	}
	~ProductFixture() {
		std::error_code error;
		std::filesystem::remove_all(root, error);
	}
	void save() { std::ofstream(path()) << document.dump(); }
	std::filesystem::path path() const { return root / "profile.json"; }
	std::filesystem::path root;
	nlohmann::json document;
};

class BusinessPipeline final : public pipeline::IVisionPipeline<vision::AnomalyResult> {
public:
	explicit BusinessPipeline(std::atomic_size_t& executions, std::atomic_size_t* active = nullptr)
		: executions_(executions), active_(active) {
		if (active_) {
			++*active_;
		}
	}
	~BusinessPipeline() override {
		if (active_) {
			--*active_;
		}
	}
	core::Result<vision::AnomalyResult> run(pipeline::PipelinePacket) override {
		++executions_;
		return core::Result<vision::AnomalyResult>::success(
			{7.0F, 2.0F, vision::AnomalyDecision::Ng});
	}
private:
	std::atomic_size_t& executions_;
	std::atomic_size_t* active_;
};

core::Result<SessionPointer> makeSingleRuntime(
	const visionService::profile::ProductProfile& profile, std::atomic_size_t& executions,
	executor::FrameExecutionOptions<vision::AnomalyResult> options = {},
	bool startBeforeAdoption = false) {
	auto source = camera::FileSource::create({
		.directory = profile.sources.front().directory.directory, .extensions = {".ppm"}});
	if (!source) {
		return core::Result<SessionPointer>::failure(source.status());
	}
	auto runtime = runtime::RuntimeFactory::createRuntime<vision::AnomalyResult>(
		std::move(source).value(), std::make_unique<BusinessPipeline>(executions),
		config::DeploymentConfig{.executor = {
			.performancePolicy = config::PerformancePolicy::Serial}}, std::move(options));
	if (!runtime) {
		return core::Result<SessionPointer>::failure(runtime.status());
	}
	if (startBeforeAdoption) {
		auto started = runtime.value()->start();
		if (!started) {
			return core::Result<SessionPointer>::failure(started.status());
		}
	}
	return core::Result<SessionPointer>::success(
		visionService::session::adoptRuntime(std::move(runtime).value()));
}

bool awaitIdle(visionService::VisionService& service) {
	const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (std::chrono::steady_clock::now() < limit) {
		if (service.sessionController().state() == SessionLifecycle::Idle) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return false;
}

TEST(RuntimeAdoptionTest, SingleRuntimePreservesBusinessAndDeliversGuiResultsAcrossRecovery) {
	ProductFixture product;
	std::atomic_size_t factories{0}, executions{0}, business{0}, originalObserver{0}, gui{0}, frames{0};
	std::atomic_size_t activePipelines{0};
	visionService::VisionService service(
		[&](const auto& profile, auto& registry) -> core::Result<SessionPointer> {
			++factories;
			EXPECT_EQ(activePipelines, 0U);
			auto registered = registry.registerState({.name = "business.count",
				.description = "business callback count",
				.accessLevel = visionService::endpoints::AccessLevel::Operator,
				.current = [&] { return visionService::endpoints::StateSnapshot{
					0, std::to_string(business.load())}; }});
			if (!registered) {
				return core::Result<SessionPointer>::failure(registered.status());
			}
			auto source = camera::FileSource::create({
				.directory = profile.sources.front().directory.directory, .extensions = {".ppm"}});
			if (!source) {
				return core::Result<SessionPointer>::failure(source.status());
			}
			executor::FrameExecutionOptions<vision::AnomalyResult> options;
			options.completionCallback = [&](auto, const auto& result) {
				EXPECT_TRUE(result);
				++business;
			};
			options.resultObserver = [&](auto, const auto&, auto, const auto&) { ++originalObserver; };
			auto runtime = runtime::RuntimeFactory::createRuntime<vision::AnomalyResult>(
				std::move(source).value(), std::make_unique<BusinessPipeline>(executions, &activePipelines),
				config::DeploymentConfig{.executor = {
					.performancePolicy = config::PerformancePolicy::Serial}}, std::move(options));
			if (!runtime) {
				return core::Result<SessionPointer>::failure(runtime.status());
			}
			return core::Result<SessionPointer>::success(
				visionService::session::adoptRuntime(std::move(runtime).value()));
		});
	service.resultObserver = [&](auto, const auto& identity, auto, const auto& result) {
		ASSERT_TRUE(result);
		EXPECT_EQ(result->score, 7.0F);
		EXPECT_EQ(identity.sourceId, 0U);
		++gui;
	};
	ASSERT_TRUE(service.loadProfile(product.path()));
	auto subscription = service.endpoints().subscribeStream("stream.files0",
		[&](auto id, const auto& frame) {
			EXPECT_EQ(id, 0U);
			EXPECT_EQ(frame.width(), 1U);
			++frames;
		});
	ASSERT_TRUE(subscription);
	ASSERT_TRUE(service.endpoints().invokeCommand("session.start"));
	ASSERT_TRUE(awaitIdle(service));
	ASSERT_TRUE(service.endpoints().invokeCommand("session.recover"));
	EXPECT_EQ(factories, 2U);
	EXPECT_GT(frames.load(), 0U);
	ASSERT_TRUE(service.endpoints().invokeCommand("session.start"));
	ASSERT_TRUE(awaitIdle(service));
	service.shutdown();
	EXPECT_EQ(activePipelines, 0U);
	EXPECT_EQ(executions, 12U);
	EXPECT_EQ(business, 12U);
	EXPECT_EQ(originalObserver, 12U);
	EXPECT_EQ(gui, 12U);
	EXPECT_EQ(service.sessionController().counters().failed, 0U);
}

TEST(RuntimeAdoptionTest, MultiRuntimeUsesTheOriginalSourcesAndObservers) {
	ProductFixture product(2);
	std::atomic_size_t executions{0}, business{0}, gui{0};
	std::atomic_uint sourceMask{0};
	visionService::VisionService service(
		[&](const auto& profile, auto&) -> core::Result<SessionPointer> {
			std::vector<std::unique_ptr<camera::IFrameSource>> sources;
			for (const auto& entry : profile.sources) {
				auto source = camera::FileSource::create({
					.directory = entry.directory.directory, .extensions = {".ppm"}});
				if (!source) {
					return core::Result<SessionPointer>::failure(source.status());
				}
				sources.push_back(std::move(source).value());
			}
			runtime::MultiCameraExecutionOptions<vision::AnomalyResult> options;
			options.resultObserver = [&](auto, const auto&, auto, const auto&) { ++business; };
			auto runtime = std::make_unique<runtime::MultiCameraSession<vision::AnomalyResult>>(
				std::move(sources),
				std::make_unique<executor::SerialPipelineExecutor<vision::AnomalyResult>>(
					std::make_unique<BusinessPipeline>(executions)), std::move(options));
			return core::Result<SessionPointer>::success(
				visionService::session::adoptRuntime(std::move(runtime)));
		});
	service.resultObserver = [&](auto, const auto& identity, auto, const auto& result) {
		ASSERT_TRUE(result);
		ASSERT_TRUE(identity.sourceId);
		sourceMask.fetch_or(1U << *identity.sourceId);
		++gui;
	};
	ASSERT_TRUE(service.loadProfile(product.path()));
	EXPECT_TRUE(service.endpoints().contains("stream.files0"));
	EXPECT_TRUE(service.endpoints().contains("stream.files1"));
	ASSERT_TRUE(service.endpoints().invokeCommand("session.start"));
	ASSERT_TRUE(awaitIdle(service));
	service.shutdown();
	EXPECT_EQ(executions, 12U);
	EXPECT_EQ(business, 12U);
	EXPECT_EQ(gui, 12U);
	EXPECT_EQ(sourceMask, 3U);
	EXPECT_EQ(service.sessionController().counters().receivedPerSource,
		(std::vector<std::size_t>{6, 6}));
}

TEST(RuntimeAdoptionTest, GenericServiceRefusesAProductBoundProfile) {
	ProductFixture product;
	product.document["runtimeProduct"] = "product-with-business-callback";
	product.save();
	visionService::VisionService service;
	auto loaded = service.loadProfile(product.path());
	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), core::StatusCode::Unsupported);
}

TEST(RuntimeAdoptionTest, RefusesNullFactoryOutputInsteadOfUsingDefaultAssembly) {
	ProductFixture product;
	visionService::VisionService service([](const auto&, auto&) {
		return core::Result<SessionPointer>::success(nullptr);
	});
	auto loaded = service.loadProfile(product.path());
	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), core::StatusCode::InvalidArgument);
}

TEST(RuntimeAdoptionTest, ReportsFactoryExceptions) {
	ProductFixture product;
	visionService::VisionService service([](const auto&, auto&) -> core::Result<SessionPointer> {
		throw std::runtime_error("business initialization failed");
	});
	auto loaded = service.loadProfile(product.path());
	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), core::StatusCode::Internal);
	EXPECT_NE(loaded.status().message().find("business initialization failed"), std::string_view::npos);
}

TEST(RuntimeAdoptionTest, RejectsSourceCountMismatchBeforeStartingAcquisition) {
	ProductFixture product(2);
	std::atomic_size_t executions{0};
	visionService::VisionService service([&](const auto& profile, auto&) {
		return makeSingleRuntime(profile, executions);
	});
	auto loaded = service.loadProfile(product.path());
	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), core::StatusCode::InvalidArgument);
	EXPECT_EQ(executions, 0U);
}

TEST(RuntimeAdoptionTest, RejectsAnAlreadyStartedRuntime) {
	ProductFixture product;
	std::atomic_size_t executions{0};
	visionService::VisionService service([&](const auto& profile, auto&) {
		return makeSingleRuntime(profile, executions, {}, true);
	});
	auto loaded = service.loadProfile(product.path());
	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), core::StatusCode::InvalidState);
}

TEST(RuntimeAdoptionTest, ThrowingBusinessCallbackDoesNotSuppressServiceObservation) {
	ProductFixture product;
	std::atomic_size_t executions{0}, business{0}, gui{0};
	visionService::VisionService service([&](const auto& profile, auto&) {
		executor::FrameExecutionOptions<vision::AnomalyResult> options;
		options.completionCallback = [&](auto, const auto&) {
			++business;
			throw std::runtime_error("business callback failure");
		};
		return makeSingleRuntime(profile, executions, std::move(options));
	});
	service.resultObserver = [&](auto, const auto&, auto, const auto& result) {
		EXPECT_TRUE(result);
		++gui;
	};
	ASSERT_TRUE(service.loadProfile(product.path()));
	ASSERT_TRUE(service.endpoints().invokeCommand("session.start"));
	ASSERT_TRUE(awaitIdle(service));
	service.shutdown();
	EXPECT_EQ(executions, 6U);
	EXPECT_EQ(business, 6U);
	EXPECT_EQ(gui, 6U);
}
} // namespace
