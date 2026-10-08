#include "serviceClient.hpp"
#include "camera/fileSource.hpp"
#include "runtime/runtimeFactory.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {
using namespace visionRuntime;

class BusinessPipeline final : public pipeline::IVisionPipeline<vision::AnomalyResult> {
public:
	core::Result<vision::AnomalyResult> run(pipeline::PipelinePacket) override {
		return core::Result<vision::AnomalyResult>::success(
			{7.0F, 2.0F, vision::AnomalyDecision::Ng});
	}
};

TEST(ServiceClientRuntimeTest, RecoveryRetainsBusinessCallbacksAndReconnectsGuiObservers) {
	QTemporaryDir fixture;
	ASSERT_TRUE(fixture.isValid());
	const auto root = std::filesystem::path(fixture.path().toStdString());
	for (int index = 0; index < 6; ++index) {
		std::ofstream image(root / (std::to_string(index) + ".ppm"), std::ios::binary);
		image << "P6\n1 1\n255\n";
		const char pixels[3] = {static_cast<char>(index), 0, 0};
		image.write(pixels, 3);
	}
	const auto profilePath = root / "profile.json";
	std::ofstream(profilePath) << nlohmann::json{
		{"schemaVersion", {{"major", 1}, {"minor", 0}}},
		{"id", "qt-business"},
		{"sources", {{{"id", "files"}, {"type", "directory"}, {"directory", "."},
			{"extensions", {".ppm"}}, {"frameIntervalMilliseconds", 10}}}},
		{"model", {{"packagePath", "not-loaded"}, {"backendId", "custom"},
			{"device", "CPU"}, {"pluginDirectory", "not-loaded"}}}}.dump();
	std::atomic_size_t factories{0}, business{0};
	std::atomic_uintptr_t sourcePixels{0};
	std::size_t frames = 0;
	QImage latestImage;
	visionShell::ServiceClient client([&](const auto& profile, auto&) ->
		core::Result<std::unique_ptr<visionService::session::IInspectionSession>> {
		using Session = std::unique_ptr<visionService::session::IInspectionSession>;
		++factories;
		auto source = camera::FileSource::create({
			.directory = profile.sources.front().directory.directory,
			.extensions = {".ppm"}, .frameInterval = std::chrono::milliseconds(10)});
		if (!source) {
			return core::Result<Session>::failure(source.status());
		}
		executor::FrameExecutionOptions<vision::AnomalyResult> execution;
		execution.completionCallback = [&](auto, const auto&) { ++business; };
		auto runtime = runtime::RuntimeFactory::createRuntime<vision::AnomalyResult>(
			std::move(source).value(), std::make_unique<BusinessPipeline>(),
			config::DeploymentConfig{.executor = {
				.performancePolicy = config::PerformancePolicy::Serial}}, std::move(execution));
		if (!runtime) {
			return core::Result<Session>::failure(runtime.status());
		}
		auto observed = runtime.value()->addObservers({
			.frame = [&](auto, const auto& frame) {
				sourcePixels = reinterpret_cast<std::uintptr_t>(frame.data());
			}});
		if (!observed) {
			return core::Result<Session>::failure(observed.status());
		}
		return core::Result<Session>::success(
			visionService::session::adoptRuntime(std::move(runtime).value()));
	});
	QObject::connect(&client, &visionShell::ServiceClient::frameReceived,
		&client, [&](auto id, const QImage& image) {
			EXPECT_EQ(id, 0U);
			EXPECT_FALSE(image.isNull());
			latestImage = image;
			++frames;
		});
	ASSERT_TRUE(client.loadProfile(QString::fromStdString(profilePath.string())));
	auto waitForRun = [&] {
		const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (std::chrono::steady_clock::now() < limit) {
			QCoreApplication::processEvents();
			if (client.stateSnapshot("session.state").contains("\"idle\"") &&
				!latestImage.isNull() && latestImage.pixelColor(0, 0).red() == 5) {
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return false;
	};
	ASSERT_TRUE(client.invokeCommand("session.start"));
	ASSERT_TRUE(waitForRun());
	EXPECT_EQ(business, 6U);
	EXPECT_GT(frames, 0U);
	EXPECT_NE(reinterpret_cast<std::uintptr_t>(latestImage.constBits()), sourcePixels.load());
	const auto firstFrames = frames;
	ASSERT_TRUE(client.invokeCommand("session.recover"));
	EXPECT_EQ(factories, 2U);
	ASSERT_TRUE(client.invokeCommand("session.start"));
	ASSERT_TRUE(waitForRun());
	EXPECT_EQ(business, 12U);
	EXPECT_GT(frames, firstFrames);
}
} // namespace

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
