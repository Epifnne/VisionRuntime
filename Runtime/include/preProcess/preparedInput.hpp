#pragma once

#include "core/tensor.hpp"
#include "pipeline/pipelinePacket.hpp"
#include "vision/transformContext.hpp"

#include <string>
#include <unordered_map>
#include <utility>

namespace visionRuntime::preprocess {

using TensorMap = std::unordered_map<std::string, core::Tensor>;

class PreparedInput {
public:
	PreparedInput(
		pipeline::PipelinePacket packet,
		TensorMap tensors,
		vision::TransformContext transformContext = {})
		: packet_(std::move(packet)),
		  tensors_(std::move(tensors)),
		  transformContext_(std::move(transformContext)) {}

	PreparedInput(const PreparedInput&) = delete;
	PreparedInput& operator=(const PreparedInput&) = delete;
	PreparedInput(PreparedInput&&) noexcept = default;
	PreparedInput& operator=(PreparedInput&&) noexcept = default;

	[[nodiscard]] pipeline::PipelinePacket& packet() noexcept { return packet_; }
	[[nodiscard]] const pipeline::PipelinePacket& packet() const noexcept { return packet_; }
	[[nodiscard]] TensorMap& tensors() noexcept { return tensors_; }
	[[nodiscard]] const TensorMap& tensors() const noexcept { return tensors_; }
	[[nodiscard]] const vision::TransformContext& transformContext() const noexcept {
		return transformContext_;
	}

private:
	pipeline::PipelinePacket packet_;
	TensorMap tensors_;
	vision::TransformContext transformContext_;
};

} // namespace visionRuntime::preprocess