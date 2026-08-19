#pragma once

#include "preprocess/preparedInput.hpp"

#include <utility>

namespace visionRuntime::pipeline {

class InferenceOutput {
public:
	InferenceOutput(
		PipelinePacket packet,
		preprocess::TensorMap tensors,
		vision::TransformContext transformContext)
		: packet_(std::move(packet)),
		  tensors_(std::move(tensors)),
		  transformContext_(std::move(transformContext)) {}

	InferenceOutput(const InferenceOutput&) = delete;
	InferenceOutput& operator=(const InferenceOutput&) = delete;
	InferenceOutput(InferenceOutput&&) noexcept = default;
	InferenceOutput& operator=(InferenceOutput&&) noexcept = default;

	[[nodiscard]] PipelinePacket& packet() noexcept { return packet_; }
	[[nodiscard]] const PipelinePacket& packet() const noexcept { return packet_; }
	[[nodiscard]] const preprocess::TensorMap& tensors() const noexcept { return tensors_; }
	[[nodiscard]] const vision::TransformContext& transformContext() const noexcept {
		return transformContext_;
	}

private:
	PipelinePacket packet_;
	preprocess::TensorMap tensors_;
	vision::TransformContext transformContext_;
};

} // namespace visionRuntime::pipeline