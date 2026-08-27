#include <visionruntime>

#include "config/buildProfile.hpp"
#include "core/status.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <variant>

namespace {

using namespace std::chrono_literals;

class TemporaryImageDirectory {
public:
	TemporaryImageDirectory()
		: path_(std::filesystem::temp_directory_path() /
			("vision-runtime-factory-" +
				std::to_string(std::chrono::steady_clock::now()
					.time_since_epoch().count()))) {
		std::filesystem::create_directories(path_);
		std::ofstream(path_ / "frame.png").put('\0');
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

static_assert(std::variant_size_v<visionRuntime::camera::FrameSourceConfig> == 3);

} // namespace

TEST(FrameSourceFactoryTest, CreatesFileSourceThroughCommonInterface) {
	using namespace visionRuntime;

	TemporaryImageDirectory images;
	const camera::FrameSourceConfig config = camera::FileFrameSourceConfig{
		.source = {.directory = images.path()},
	};
	auto source = camera::FrameSourceFactory::create(config);
	ASSERT_TRUE(source) << source.status().toString();

	const auto info = source.value()->info();
	EXPECT_TRUE(info.isFinite);
	ASSERT_TRUE(info.expectedFrameCount);
	EXPECT_EQ(*info.expectedFrameCount, 1U);
}

TEST(FrameSourceFactoryTest, RejectsCameraSourcesWithoutCameraSdk) {
	using namespace visionRuntime;

	if constexpr (config::BuildProfile::cameraSdk == config::CameraSdk::None) {
		const camera::FrameSourceConfig continuous =
			camera::ContinuousCameraSourceConfig{};
		auto continuousSource = camera::FrameSourceFactory::create(continuous);
		ASSERT_FALSE(continuousSource);
		EXPECT_EQ(continuousSource.status().code(), core::StatusCode::Unsupported);

		const camera::FrameSourceConfig timed = camera::TimedCameraSourceConfig{};
		auto timedSource = camera::FrameSourceFactory::create(timed);
		ASSERT_FALSE(timedSource);
		EXPECT_EQ(timedSource.status().code(), core::StatusCode::Unsupported);
	}
}

TEST(FrameSourceFactoryTest, ValidatesSourceOptionsBeforeCreatingCamera) {
	using namespace visionRuntime;

	const camera::FrameSourceConfig continuous =
		camera::ContinuousCameraSourceConfig{
			.source = {.frameRate = 0.0},
		};
	auto continuousSource = camera::FrameSourceFactory::create(continuous);
	ASSERT_FALSE(continuousSource);
	EXPECT_EQ(continuousSource.status().code(), core::StatusCode::InvalidArgument);

	const camera::FrameSourceConfig timed = camera::TimedCameraSourceConfig{
		.source = {.triggerInterval = -1ms, .responseTimeout = 1s},
	};
	auto timedSource = camera::FrameSourceFactory::create(timed);
	ASSERT_FALSE(timedSource);
	EXPECT_EQ(timedSource.status().code(), core::StatusCode::InvalidArgument);
}
