#include "camera/cameraSourceOptions.hpp"
#include "camera/cameraTypes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <limits>

TEST(ContinuousCameraSourceOptionsTest, ValidatesOptionalFrameRate) {
	using visionRuntime::camera::ContinuousCameraSourceOptions;

	EXPECT_TRUE(ContinuousCameraSourceOptions{}.isValid());
	EXPECT_TRUE(ContinuousCameraSourceOptions{.frameRate = 30.0}.isValid());
	EXPECT_FALSE(ContinuousCameraSourceOptions{.frameRate = 0.0}.isValid());
	EXPECT_FALSE(ContinuousCameraSourceOptions{.frameRate = -1.0}.isValid());
	EXPECT_FALSE(ContinuousCameraSourceOptions{
		.frameRate = std::numeric_limits<double>::infinity()}.isValid());
}

TEST(CameraAcquisitionOptionsTest, ValidatesModeAndDeviceFrameRate) {
	using namespace visionRuntime::camera;

	EXPECT_TRUE(CameraAcquisitionOptions{}.isValid());
	EXPECT_TRUE((CameraAcquisitionOptions{
		.mode = AcquisitionMode::Continuous,
		.frameRate = 30.0,
	}.isValid()));
	EXPECT_FALSE((CameraAcquisitionOptions{
		.mode = AcquisitionMode::Continuous,
		.frameRate = 0.0,
	}.isValid()));
	EXPECT_FALSE((CameraAcquisitionOptions{
		.mode = AcquisitionMode::SoftwareTrigger,
		.frameRate = 30.0,
	}.isValid()));
	EXPECT_FALSE((CameraAcquisitionOptions{
		.mode = static_cast<AcquisitionMode>(-1),
	}.isValid()));
}

TEST(TimedTriggerSourceOptionsTest, ValidatesIntervalAndResponseTimeout) {
	using namespace std::chrono_literals;
	using visionRuntime::camera::TimedTriggerSourceOptions;

	EXPECT_TRUE(TimedTriggerSourceOptions{}.isValid());
	EXPECT_TRUE((TimedTriggerSourceOptions{
		.triggerInterval = 0ms,
		.responseTimeout = 1ms,
	}.isValid()));
	EXPECT_FALSE((TimedTriggerSourceOptions{
		.triggerInterval = -1ms,
		.responseTimeout = 1ms,
	}.isValid()));
	EXPECT_FALSE((TimedTriggerSourceOptions{
		.triggerInterval = 1ms,
		.responseTimeout = 0ms,
	}.isValid()));
}