#include "config/buildProfile.hpp"

#include <gtest/gtest.h>

#include <type_traits>

namespace {

using visionRuntime::config::BuildProfile;
using visionRuntime::config::CameraSdk;
using visionRuntime::config::InferencePlatform;
using visionRuntime::config::HikMvsCamera;
using visionRuntime::config::NoCamera;
using visionRuntime::config::NoInferencePlatform;
using visionRuntime::config::OpenVinoIntelPlatform;
using visionRuntime::config::SelectedCamera;
using visionRuntime::config::SelectedPlatform;

static_assert(!BuildProfile::cameraCapabilities.supportsHardwareTrigger);
static_assert(
	BuildProfile::cameraCapabilities.supportsSdkBufferLease ==
	(BuildProfile::cameraSdk == CameraSdk::HikMvs));
static_assert(!BuildProfile::cameraCapabilities.supportsUserBuffers);
static_assert(
	BuildProfile::inferenceCapabilities.supportsCpu ==
	(BuildProfile::inferencePlatform == InferencePlatform::OpenVinoIntel));
static_assert(
	BuildProfile::inferenceCapabilities.supportsGpu ==
	(BuildProfile::inferencePlatform == InferencePlatform::OpenVinoIntel));
static_assert(
	BuildProfile::inferenceCapabilities.supportsNpu ==
	(BuildProfile::inferencePlatform == InferencePlatform::OpenVinoIntel));
static_assert(
	std::is_same_v<SelectedCamera, HikMvsCamera> ==
	(BuildProfile::cameraSdk == CameraSdk::HikMvs));
static_assert(
	std::is_same_v<SelectedCamera, NoCamera> ==
	(BuildProfile::cameraSdk == CameraSdk::None));
static_assert(
	std::is_same_v<SelectedPlatform, OpenVinoIntelPlatform> ==
	(BuildProfile::inferencePlatform == InferencePlatform::OpenVinoIntel));
static_assert(
	std::is_same_v<SelectedPlatform, NoInferencePlatform> ==
	(BuildProfile::inferencePlatform == InferencePlatform::None));

} // namespace

TEST(BuildProfileTest, ExposesConfiguredNames) {
	if constexpr (BuildProfile::cameraSdk == CameraSdk::HikMvs) {
		EXPECT_EQ(BuildProfile::cameraName, "hik-mvs");
	} else {
		EXPECT_EQ(BuildProfile::cameraName, "none");
	}
	if constexpr (
		BuildProfile::inferencePlatform == InferencePlatform::OpenVinoIntel) {
		EXPECT_EQ(BuildProfile::inferencePlatformName, "openvino-intel");
	} else {
		EXPECT_EQ(BuildProfile::inferencePlatformName, "none");
	}
}