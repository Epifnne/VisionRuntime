#pragma once

#include "preprocess/iPreprocessor.hpp"
#include "preprocess/preprocessNode.hpp"
#include "preprocess/preprocessContext.hpp"
#include "vision/frame.hpp"

#include <concepts>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace visionRuntime::preprocess {

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
	template<PreprocessNode Node>
		requires (Node::inputState == State && !(HasMaterialized && Node::materializes))
	[[nodiscard]] auto then(Node node) && {
		if (status_.isOk()) {
			auto builtNode = std::move(node).build(buildContext_);
			if (builtNode) {
				nodes_.push_back(std::move(builtNode).value());
			} else {
				status_ = builtNode.status();
			}
		}
		return PreprocessChainBuilder<
			Node::outputState, HasMaterialized || Node::materializes>(
			std::move(nodes_), std::move(status_), std::move(buildContext_));
	}

	[[nodiscard]] core::Result<std::unique_ptr<IPreprocessor>> build() &&
		requires (State == PreprocessDataState::Tensor && HasMaterialized) {
		if (!status_.isOk()) {
			return core::Result<std::unique_ptr<IPreprocessor>>::failure(
				std::move(status_));
		}
		std::unique_ptr<IPreprocessor> preprocessor =
			std::make_unique<PreprocessChain>(std::move(nodes_));
		return core::Result<std::unique_ptr<IPreprocessor>>::success(
			std::move(preprocessor));
	}

private:
	friend class PreprocessBuilder;
	template<PreprocessDataState, bool>
	friend class PreprocessChainBuilder;

	PreprocessChainBuilder() = default;
	PreprocessChainBuilder(
		std::vector<std::unique_ptr<IPreprocessNode>> nodes,
		core::Status status,
		PreprocessBuildContext buildContext)
		: nodes_(std::move(nodes)),
		  status_(std::move(status)),
		  buildContext_(std::move(buildContext)) {}

	std::vector<std::unique_ptr<IPreprocessNode>> nodes_;
	core::Status status_;
	PreprocessBuildContext buildContext_;
};

class PreprocessBuilder {
public:
	template<typename Input>
		requires std::same_as<std::remove_cvref_t<Input>, vision::Frame>
	[[nodiscard]] static auto start() {
		return PreprocessChainBuilder<PreprocessDataState::CameraFrame, false>();
	}
};

} // namespace visionRuntime::preprocess