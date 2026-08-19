/**
 * @file pipelinePacket.hpp
 * @author epifnne
 * @date 2026-08-10
 * @brief Defines move-only packets that transfer frame ownership between pipeline stages.
 */

#pragma once

#include "pipeline/frameRetentionPolicy.hpp"
#include "vision/frame.hpp"

#include <optional>
#include <utility>

namespace visionRuntime::pipeline {

class PipelinePacket {
public:
	explicit PipelinePacket(
		vision::Frame cameraFrame,
		PipelineOwnershipOptions ownershipOptions = {})
		: cameraFrame_(std::move(cameraFrame)),
		  ownershipOptions_(ownershipOptions) {}

	PipelinePacket(const PipelinePacket&) = delete;
	PipelinePacket& operator=(const PipelinePacket&) = delete;
	PipelinePacket(PipelinePacket&& other) noexcept
		: cameraFrame_(std::exchange(other.cameraFrame_, std::nullopt)),
		  businessFrame_(std::exchange(other.businessFrame_, std::nullopt)),
		  ownershipOptions_(other.ownershipOptions_) {}

	PipelinePacket& operator=(PipelinePacket&& other) noexcept {
		if (this != &other) {
			cameraFrame_ = std::exchange(other.cameraFrame_, std::nullopt);
			businessFrame_ = std::exchange(other.businessFrame_, std::nullopt);
			ownershipOptions_ = other.ownershipOptions_;
		}
		return *this;
	}

	[[nodiscard]] const PipelineOwnershipOptions& ownershipOptions() const noexcept {
		return ownershipOptions_;
	}

	[[nodiscard]] bool hasCameraFrame() const noexcept {
		return cameraFrame_.has_value();
	}

	[[nodiscard]] const vision::Frame* cameraFrame() const noexcept {
		return cameraFrame_ ? &*cameraFrame_ : nullptr;
	}

	[[nodiscard]] bool hasBusinessFrame() const noexcept {
		return businessFrame_.has_value();
	}

	[[nodiscard]] const vision::Frame* businessFrame() const noexcept {
		return businessFrame_ ? &*businessFrame_ : nullptr;
	}

	void completeImagePreparation(
		std::optional<vision::Frame> businessFrame = std::nullopt) noexcept {
		businessFrame_ = std::move(businessFrame);
		if (ownershipOptions_.businessFrameRelease ==
			FrameReleaseStage::AfterImagePreparation) {
			businessFrame_.reset();
		}
		if (ownershipOptions_.cameraFrameRelease ==
			FrameReleaseStage::AfterImagePreparation) {
			cameraFrame_.reset();
		}
	}

	void finishPostprocess() noexcept {
		cameraFrame_.reset();
		businessFrame_.reset();
	}

	[[nodiscard]] std::optional<vision::Frame> takeCameraFrame() noexcept {
		return std::exchange(cameraFrame_, std::nullopt);
	}

	[[nodiscard]] std::optional<vision::Frame> takeBusinessFrame() noexcept {
		return std::exchange(businessFrame_, std::nullopt);
	}

private:
	std::optional<vision::Frame> cameraFrame_;
	std::optional<vision::Frame> businessFrame_;
	PipelineOwnershipOptions ownershipOptions_;
};

} // namespace visionRuntime::pipeline