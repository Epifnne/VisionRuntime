#include "config/buildProfile.hpp"

#include <gtest/gtest.h>

#include <type_traits>

namespace {

using visionRuntime::config::BuildProfile;
using visionRuntime::config::CameraSdk;
using visionRuntime::config::HikMvsCamera;
using visionRuntime::config::NoCamera;
using visionRuntime::config::SelectedCamera;

static_assert(!BuildProfile::cameraCapabilities.supportsHardwareTrigger);
static_assert(
BuildProfile::cameraCapabilities.supportsSdkBufferLease ==
(BuildProfile::cameraSdk == CameraSdk::HikMvs));
static_assert(!BuildProfile::cameraCapabilities.supportsUserBuffers);
static_assert(
std::is_same_v<SelectedCamera, HikMvsCamera> ==
(BuildProfile::cameraSdk == CameraSdk::HikMvs));
static_assert(
std::is_same_v<SelectedCamera, NoCamera> ==
(BuildProfile::cameraSdk == CameraSdk::None));

} // namespace

TEST(BuildProfileTest, ExposesConfiguredCameraName) {
if constexpr (BuildProfile::cameraSdk == CameraSdk::HikMvs) {
EXPECT_EQ(BuildProfile::cameraName, "hik-mvs");
} else {
EXPECT_EQ(BuildProfile::cameraName, "none");
}
}