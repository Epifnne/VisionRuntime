#include "service/visionService.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <latch>
#include <string>
#include <thread>

#ifndef VISION_SERVICE_TEST_PLUGIN_DIRECTORY
#error VISION_SERVICE_TEST_PLUGIN_DIRECTORY must be defined
#endif

namespace {

using visionRuntime::core::StatusCode;
using visionService::VisionService;
using visionService::endpoints::ParameterValue;
using visionService::endpoints::StateSnapshot;
using visionService::session::SessionLifecycle;

class TemporaryProduct {
public:
	TemporaryProduct()
		: root_(std::filesystem::temp_directory_path() /
			("vision-service-directory-" +
			 std::to_string(
				 std::chrono::steady_clock::now().time_since_epoch().count()))) {
		std::filesystem::create_directories(imageDirectory_);
		std::filesystem::create_directories(packageDirectory_ / "artifacts");
		for (int index = 0; index < 6; ++index) {
			std::ofstream stream(
				imageDirectory_ / ("frame" + std::to_string(index) + ".ppm"),
				std::ios::binary);
			stream << "P6\n1 1\n255\n";
			const char pixel[3] = {0, 0, 0};
			stream.write(pixel, sizeof(pixel));
		}
		std::ofstream(packageDirectory_ / "artifacts" / "model.fake")
			<< "artifact";
		std::ofstream(packageDirectory_ / "manifest.json") << R"({
			"schemaVersion": {"major": 1, "minor": 0},
			"id": "fake-anomaly",
			"version": "1.0.0",
			"inputs": [{
				"name": "images",
				"elementType": "float32",
				"layout": "nchw",
				"shape": [1, 1, 224, 224]
			}],
			"outputs": [{
				"name": "output",
				"elementType": "float32",
				"layout": "scalar",
				"shape": [1]
			}],
			"artifacts": [{
				"id": "fake-cpu",
				"backendId": "fake",
				"kind": "identity",
				"path": "artifacts/model.fake",
				"devices": ["CPU"]
			}]
		})";

		const auto imageDirectory =
			std::filesystem::relative(imageDirectory_, root_);
		const auto packageDirectory =
			std::filesystem::relative(packageDirectory_, root_);
		std::ofstream(profilePath_) << R"({
			"schemaVersion": {"major": 1, "minor": 0},
			"id": "directory-inspection",
			"name": "Directory Inspection",
			"sources": [{
				"id": "files",
				"type": "directory",
				"role": "offline replay",
				"directory": ")" + imageDirectory.generic_string() + R"(",
				"extensions": [".ppm"],
				"loop": false,
				"frameIntervalMilliseconds": 5
			}],
			"model": {
				"packagePath": ")" + packageDirectory.generic_string() + R"(",
				"backendId": "fake",
				"device": "CPU",
				"pluginDirectory": ")"
				+ std::string(VISION_SERVICE_TEST_PLUGIN_DIRECTORY) + R"("
			},
			"pipeline": {
				"resizeShortSide": 256,
				"cropWidth": 224,
				"cropHeight": 224,
				"mean": [0.449],
				"standardDeviation": [0.226],
				"threshold": 2.0,
				"maxBatchSize": 1,
				"flushTimeoutMilliseconds": 10,
				"queueCapacity": 8,
				"queueFullPolicy": "block"
			},
			"endpoints": [
				{"name": "session.start"},
				{"name": "session.stop"},
				{"name": "session.state"},
				{"name": "session.summary"},
				{"name": "metrics.performance"},
				{"name": "stream.files"},
				{"name": "camera.files.exposureMicroseconds", "access": "engineer"}
			],
			"ui": {"pages": [{"id": "monitor"}]}
		})";
	}

	~TemporaryProduct() {
		std::error_code error;
		std::filesystem::remove_all(root_, error);
	}

	[[nodiscard]] const std::filesystem::path& profilePath() const noexcept {
		return profilePath_;
	}

private:
	std::filesystem::path root_;
	std::filesystem::path imageDirectory_ = root_ / "images";
	std::filesystem::path packageDirectory_ = root_ / "package";
	std::filesystem::path profilePath_ = root_ / "profile.json";
};

[[nodiscard]] bool waitLatchFor(
	std::latch& latch,
	std::chrono::milliseconds timeout) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (latch.try_wait()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return latch.try_wait();
}

[[nodiscard]] bool waitForState(
	VisionService& service,
	SessionLifecycle expected,
	std::chrono::milliseconds timeout) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (service.sessionController().state() == expected) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return service.sessionController().state() == expected;
}

TEST(VisionServiceDirectorySessionTest, RegistersEndpointsFromProfile) {
	TemporaryProduct product;
	VisionService service;
	ASSERT_TRUE(service.loadProfile(product.profilePath()))
		<< "profile load failed";

	const auto names = service.endpoints().endpointNames();
	EXPECT_FALSE(names.empty());
	EXPECT_TRUE(service.endpoints().contains("session.start"));
	EXPECT_TRUE(service.endpoints().contains("session.stop"));
	EXPECT_TRUE(service.endpoints().contains("session.state"));
	EXPECT_TRUE(service.endpoints().contains("session.summary"));
	EXPECT_TRUE(service.endpoints().contains("metrics.performance"));
	EXPECT_TRUE(service.endpoints().contains("stream.files"));
	EXPECT_TRUE(
		service.endpoints().contains("camera.files.exposureMicroseconds"));

	const auto streams = service.endpoints().streams();
	ASSERT_EQ(streams.size(), 1U);
	EXPECT_EQ(streams.front().name, "stream.files");
	EXPECT_EQ(streams.front().sourceId, 0U);

	const auto manifest = service.exportManifest();
	EXPECT_NE(manifest.find("\"name\": \"session.start\""), std::string::npos);
	EXPECT_NE(manifest.find("\"kind\": \"stream\""), std::string::npos);
	EXPECT_NE(manifest.find("\"sourceId\": 0"), std::string::npos);

	// Directory sources have no camera device; tuning endpoints report it.
	auto exposure = service.endpoints().writeParameter(
		"camera.files.exposureMicroseconds", ParameterValue{12000.0});
	EXPECT_EQ(exposure.status().code(), StatusCode::InvalidState);
	EXPECT_EQ(
		service.endpoints().parameters().front().accessLevel,
		visionService::endpoints::AccessLevel::Engineer);
}

TEST(VisionServiceDirectorySessionTest, RunsDirectorySessionEndToEnd) {
	TemporaryProduct product;
	VisionService service;
	ASSERT_TRUE(service.loadProfile(product.profilePath()));

	std::latch framesDelivered(3);
	auto streamSubscription = service.endpoints().subscribeStream(
		"stream.files",
		[&](std::uint32_t sourceId,
			const visionRuntime::vision::Frame& frame) {
			EXPECT_EQ(sourceId, 0U);
			EXPECT_EQ(frame.width(), 1U);
			framesDelivered.count_down();
		});
	ASSERT_TRUE(streamSubscription) << streamSubscription.status().toString();

	std::latch summaryPublished(1);
	std::atomic<bool> summaryMatched{false};
	auto stateSubscription = service.endpoints().subscribeState(
		"session.summary",
		[&](const StateSnapshot& snapshot) {
			// The final summary may be published more than once (completion
			// callback + explicit metricsPublishSummary); count down once.
			if (snapshot.payload.find("\"received\":6") != std::string::npos &&
				!summaryMatched.exchange(true)) {
				summaryPublished.count_down();
			}
		});
	ASSERT_TRUE(stateSubscription) << stateSubscription.status().toString();

	ASSERT_TRUE(service.endpoints().invokeCommand("session.start"))
		<< service.sessionController().lastError();
	EXPECT_EQ(service.sessionController().state(), SessionLifecycle::Running);

	ASSERT_TRUE(waitLatchFor(framesDelivered, std::chrono::seconds(10)))
		<< "state=" << static_cast<int>(service.sessionController().state())
		<< " lastError=" << service.sessionController().lastError();
	ASSERT_TRUE(waitForState(
		service, SessionLifecycle::Idle, std::chrono::seconds(30)))
		<< "lastError=" << service.sessionController().lastError();

	service.metricsPublishSummary();
	ASSERT_TRUE(waitLatchFor(summaryPublished, std::chrono::seconds(10)));

	const auto counters = service.sessionController().counters();
	EXPECT_EQ(counters.received, 6U);
	EXPECT_EQ(counters.submitted, 6U);
	EXPECT_EQ(counters.completed, 6U);
	// The fake backend echoes the [1,1,224,224] input tensor, which is not the
	// scalar the anomaly postprocessor expects, so every result reports a
	// failure; the session still runs end to end and the counters stay
	// consistent. A passing inference requires a real model package.
	EXPECT_EQ(counters.failed, 6U);
	EXPECT_EQ(counters.dropped, 0U);
	EXPECT_EQ(counters.sourceFailures, 0U);
	ASSERT_EQ(counters.receivedPerSource.size(), 1U);
	EXPECT_EQ(counters.receivedPerSource[0], 6U);
	EXPECT_TRUE(service.sessionController().lastError().empty());

	service.endpoints().unsubscribe(streamSubscription.value());
	service.endpoints().unsubscribe(stateSubscription.value());
}

TEST(VisionServiceDirectorySessionTest, StopIsGracefulFromCommandEndpoint) {
	TemporaryProduct product;
	VisionService service;
	ASSERT_TRUE(service.loadProfile(product.profilePath()));

	ASSERT_TRUE(service.endpoints().invokeCommand("session.start"));
	EXPECT_EQ(service.sessionController().state(), SessionLifecycle::Running);

	ASSERT_TRUE(service.endpoints().invokeCommand("session.stop"));
	EXPECT_TRUE(
		service.sessionController().state() == SessionLifecycle::Stopping ||
		service.sessionController().state() == SessionLifecycle::Idle);
	ASSERT_TRUE(waitForState(
		service, SessionLifecycle::Idle, std::chrono::seconds(30)));
	EXPECT_TRUE(service.sessionController().lastError().empty());
}

} // namespace
