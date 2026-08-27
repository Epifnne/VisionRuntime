#include "camera/fileSource.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace {

class TemporaryImageDirectory {
public:
	TemporaryImageDirectory()
		: path_(std::filesystem::temp_directory_path() /
			("vision-runtime-file-source-" +
			 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
		std::filesystem::create_directories(path_);
	}

	~TemporaryImageDirectory() {
		std::error_code error;
		std::filesystem::remove_all(path_, error);
	}

	[[nodiscard]] const std::filesystem::path& path() const noexcept {
		return path_;
	}

private:
	std::filesystem::path path_;
};

void writePpm(const std::filesystem::path& path) {
	std::ofstream stream(path, std::ios::binary);
	stream << "P6\n2 1\n255\n";
	constexpr std::array<unsigned char, 6> pixels{255, 0, 0, 0, 255, 0};
	stream.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
}

} // namespace

TEST(FileSourceTest, ReadsDirectoryImagesAsFrames) {
	using namespace visionRuntime;
	TemporaryImageDirectory directory;
	writePpm(directory.path() / "frame01.ppm");

	camera::FileSourceOptions options;
	options.directory = directory.path();
	options.extensions = {"PPM"};
	auto sourceResult = camera::FileSource::create(std::move(options));
	ASSERT_TRUE(sourceResult) << sourceResult.status().toString();
	auto source = std::move(sourceResult).value();
	const auto info = source->info();
	EXPECT_TRUE(info.isFinite);
	EXPECT_EQ(info.expectedFrameCount, 1U);
	EXPECT_EQ(info.outputSpec.device, core::Device::cpu());
	EXPECT_NE(std::ranges::find(info.outputSpec.pixelFormats,
		vision::PixelFormat::Bgr8), info.outputSpec.pixelFormats.end());

	std::mutex mutex;
	std::condition_variable frameReady;
	bool received = false;
	core::Result<vision::Frame> receivedFrame = core::Result<vision::Frame>::failure(
		core::Status::error(core::StatusCode::Internal, "frame not received"));
	ASSERT_TRUE(source->start([&](core::Result<vision::Frame> frame) {
		{
			std::scoped_lock lock(mutex);
			receivedFrame = std::move(frame);
			received = true;
		}
		frameReady.notify_one();
	}));

	{
		std::unique_lock lock(mutex);
		ASSERT_TRUE(frameReady.wait_for(lock, std::chrono::seconds(5), [&] {
			return received;
		}));
	}
	source->requestStop();
	source->wait();
	ASSERT_TRUE(receivedFrame) << receivedFrame.status().toString();
	EXPECT_EQ(receivedFrame->width(), 2U);
	EXPECT_EQ(receivedFrame->height(), 1U);
	EXPECT_EQ(receivedFrame->pixelFormat(), vision::PixelFormat::Bgr8);
	EXPECT_EQ(receivedFrame->metadata().sequenceNumber, 0U);
	ASSERT_EQ(receivedFrame->bytes().size(), 6U);
	EXPECT_EQ(std::to_integer<unsigned char>(receivedFrame->bytes()[0]), 0U);
	EXPECT_EQ(std::to_integer<unsigned char>(receivedFrame->bytes()[2]), 255U);
}

TEST(FileSourceTest, RejectsDirectoryWithoutMatchingImages) {
	using namespace visionRuntime;
	TemporaryImageDirectory directory;

	camera::FileSourceOptions options;
	options.directory = directory.path();
	auto source = camera::FileSource::create(std::move(options));

	ASSERT_FALSE(source);
	EXPECT_EQ(source.status().code(), core::StatusCode::NotFound);
}

TEST(FileSourceTest, ReportsLoopingDirectoryAsUnbounded) {
	using namespace visionRuntime;
	TemporaryImageDirectory directory;
	writePpm(directory.path() / "frame01.ppm");

	camera::FileSourceOptions options;
	options.directory = directory.path();
	options.extensions = {"ppm"};
	options.loop = true;
	auto sourceResult = camera::FileSource::create(std::move(options));
	ASSERT_TRUE(sourceResult) << sourceResult.status().toString();

	const auto info = sourceResult->get()->info();
	EXPECT_FALSE(info.isFinite);
	EXPECT_FALSE(info.expectedFrameCount);
}

TEST(FileSourceTest, IsSingleUseAndHasIdempotentShutdown) {
	using namespace std::chrono_literals;
	using namespace visionRuntime;
	TemporaryImageDirectory directory;
	writePpm(directory.path() / "frame01.ppm");

	camera::FileSourceOptions options;
	options.directory = directory.path();
	options.extensions = {"ppm"};
	auto sourceResult = camera::FileSource::create(std::move(options));
	ASSERT_TRUE(sourceResult) << sourceResult.status().toString();
	auto source = std::move(sourceResult).value();

	std::atomic_size_t callbackCount = 0;
	ASSERT_TRUE(source->start([&](core::Result<vision::Frame>) {
		++callbackCount;
	}));
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (source->isRunning() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::yield();
	}
	ASSERT_FALSE(source->isRunning());

	auto restarted = source->start([](core::Result<vision::Frame>) {});
	ASSERT_FALSE(restarted);
	EXPECT_EQ(restarted.status().code(), core::StatusCode::InvalidState);
	auto restartedWithoutCallback = source->start({});
	ASSERT_FALSE(restartedWithoutCallback);
	EXPECT_EQ(restartedWithoutCallback.status().code(),
		core::StatusCode::InvalidState);

	source->requestStop();
	source->requestStop();
	source->wait();
	source->wait();
	const auto countAfterWait = callbackCount.load();
	std::this_thread::sleep_for(20ms);
	EXPECT_EQ(callbackCount.load(), countAfterWait);
	EXPECT_EQ(countAfterWait, 1U);
}