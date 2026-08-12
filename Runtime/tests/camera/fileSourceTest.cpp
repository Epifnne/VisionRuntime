#include "camera/fileSource.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace {

class TemporaryImageDirectory {
public:
	TemporaryImageDirectory()
		: path_(std::filesystem::temp_directory_path() /
			("vison-runtime-file-source-" +
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
	using namespace visonRuntime;
	TemporaryImageDirectory directory;
	writePpm(directory.path() / "frame01.ppm");

	camera::FileSourceOptions options;
	options.directory = directory.path();
	options.extensions = {"PPM"};
	auto sourceResult = camera::FileSource::create(std::move(options));
	ASSERT_TRUE(sourceResult) << sourceResult.status().toString();
	auto source = std::move(sourceResult).value();
	EXPECT_EQ(source->imageCount(), 1U);

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
	ASSERT_TRUE(source->stop());
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
	using namespace visonRuntime;
	TemporaryImageDirectory directory;

	camera::FileSourceOptions options;
	options.directory = directory.path();
	auto source = camera::FileSource::create(std::move(options));

	ASSERT_FALSE(source);
	EXPECT_EQ(source.status().code(), core::StatusCode::NotFound);
}