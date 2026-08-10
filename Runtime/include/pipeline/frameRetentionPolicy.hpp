/**
 * @file frameRetentionPolicy.hpp
 * @author epifnne
 * @date 2026-08-10
 * @brief Defines configurable release stages for camera and business frames.
 */

#pragma once

namespace visonRuntime::pipeline {

enum class FrameReleaseStage {
	AfterImagePreparation,
	AfterPostprocess
};

struct PipelineOwnershipOptions {
	FrameReleaseStage cameraFrameRelease = FrameReleaseStage::AfterImagePreparation;
	FrameReleaseStage businessFrameRelease = FrameReleaseStage::AfterPostprocess;
};

} // namespace visonRuntime::pipeline