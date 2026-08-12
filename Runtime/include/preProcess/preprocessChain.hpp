#pragma once

#include "preprocess/iPreprocessor.hpp"
#include "preprocess/preprocessContext.hpp"

#include <concepts>
#include <memory>
#include <utility>
#include <vector>

namespace visonRuntime::preprocess {

template<typename Node>
concept PreprocessNode = std::derived_from<Node, IPreprocessNode> && requires {
	{ Node::inputState } -> std::convertible_to<PreprocessDataState>;
	{ Node::outputState } -> std::convertible_to<PreprocessDataState>;
	{ Node::materializes } -> std::convertible_to<bool>;
};

class PreprocessChain final : public IPreprocessor {
public:
	explicit PreprocessChain(std::vector<std::unique_ptr<IPreprocessNode>> nodes)
		: nodes_(std::move(nodes)) {}

	[[nodiscard]] core::Result<PreparedInput> process(
		pipeline::PipelinePacket packet) override;

private:
	std::vector<std::unique_ptr<IPreprocessNode>> nodes_;
};

template<PreprocessDataState State, bool HasMaterialized = false>
class PreprocessChainBuilder {
public:
	[[nodiscard]] static auto start()
		requires (State == PreprocessDataState::CameraFrame) {
		return PreprocessChainBuilder<PreprocessDataState::CameraFrame, false>();
	}

	template<PreprocessNode Node>
		requires (Node::inputState == State && !(HasMaterialized && Node::materializes))
	[[nodiscard]] auto then(Node node) && {
		nodes_.push_back(std::make_unique<Node>(std::move(node)));
		return PreprocessChainBuilder<
			Node::outputState, HasMaterialized || Node::materializes>(std::move(nodes_));
	}

	[[nodiscard]] std::unique_ptr<IPreprocessor> build() &&
		requires (State == PreprocessDataState::Tensor && HasMaterialized) {
		return std::make_unique<PreprocessChain>(std::move(nodes_));
	}

private:
	template<PreprocessDataState, bool>
	friend class PreprocessChainBuilder;

	PreprocessChainBuilder() = default;
	explicit PreprocessChainBuilder(
		std::vector<std::unique_ptr<IPreprocessNode>> nodes)
		: nodes_(std::move(nodes)) {}

	std::vector<std::unique_ptr<IPreprocessNode>> nodes_;
};

using CameraFramePreprocessBuilder =
	PreprocessChainBuilder<PreprocessDataState::CameraFrame, false>;

} // namespace visonRuntime::preprocess