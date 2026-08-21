#include "preProcess/preprocessChain.hpp"

#include <utility>

namespace visionRuntime::preprocess {

core::Result<PreparedInput> PreprocessChain::process(
	pipeline::PipelinePacket packet) {
	PreprocessContext context(std::move(packet));
	for (auto& node : nodes_) {
		auto result = node->process(context);
		if (!result) {
			return core::Result<PreparedInput>::failure(result.status());
		}
	}
	return core::Result<PreparedInput>::success(PreparedInput(
		std::move(context.packet), std::move(context.tensors),
		std::move(context.transformContext)));
}

} // namespace visionRuntime::preprocess