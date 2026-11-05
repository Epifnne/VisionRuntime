#include "preprocess/preprocessChain.hpp"

#include <chrono>
#include <exception>
#include <utility>

namespace visionRuntime::preprocess {

core::Result<PreparedInput> PreprocessChain::process(
	pipeline::PipelinePacket packet) {
	return processWithTiming(std::move(packet), {});
}

core::Result<PreparedInput> PreprocessChain::processWithTiming(
	pipeline::PipelinePacket packet, const NodeTimingObserver& observer) {
	PreprocessContext context(std::move(packet));
	for (std::size_t index = 0; index < nodes_.size(); ++index) {
		auto& node = nodes_[index];
		auto execute = [&]() {
			if (!observer) {
				return node->process(context);
			}
			using Clock = std::chrono::steady_clock;
			const auto started = Clock::now();
			auto report = [&](Clock::time_point ended, const core::Status& status) {
				try {
					observer(NodeTiming{index, std::string(node->name()),
						std::chrono::duration<double, std::milli>(ended - started).count(),
						status});
				} catch (...) {
					logs::error("preprocess node timing observer threw an exception");
				}
			};
			try {
				auto result = node->process(context);
				report(Clock::now(), result.status());
				return result;
			} catch (const std::exception& exception) {
				const auto ended = Clock::now();
				report(ended, core::Status::error(core::StatusCode::Internal,
					std::string("node threw an exception: ") + exception.what()));
				throw;
			} catch (...) {
				const auto ended = Clock::now();
				report(ended, core::Status::error(core::StatusCode::Internal, "unknown exception"));
				throw;
			}
		};
		auto result = execute();
		if (!result) {
			return core::Result<PreparedInput>::failure(result.status());
		}
	}
	return core::Result<PreparedInput>::success(PreparedInput(
		std::move(context.packet), std::move(context.tensors),
		std::move(context.transformContext)));
}

} // namespace visionRuntime::preprocess