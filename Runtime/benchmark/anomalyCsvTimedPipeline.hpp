#pragma once

#include "benchmark/timedPipeline.hpp"
#include "core/result.hpp"
#include "pipeline/iVisionPipeline.hpp"
#include "pipeline/pipeline.hpp"
#include "vision/anomalyResult.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <sstream>
#include <utility>

namespace visionRuntime::benchmark {

class TimingOutputPath {
public:
	[[nodiscard]] static TimingOutputPath standardOutput() {
		return {};
	}

	[[nodiscard]] static TimingOutputPath file(std::filesystem::path path) {
		TimingOutputPath outputPath;
		outputPath.filePath_ = std::move(path);
		return outputPath;
	}

	[[nodiscard]] const std::optional<std::filesystem::path>& filePath() const noexcept {
		return filePath_;
	}

private:
	std::optional<std::filesystem::path> filePath_;
};

struct AnomalyCsvTimingOptions {
	bool activate = false;
	TimingOutputPath outputPath = TimingOutputPath::standardOutput();
};

namespace detail {

class AnomalyCsvTimingOutput {
public:
	[[nodiscard]] static core::Result<std::shared_ptr<AnomalyCsvTimingOutput>> create(
		const TimingOutputPath& outputPath) {
		auto output = std::shared_ptr<AnomalyCsvTimingOutput>(
			new AnomalyCsvTimingOutput());
		if (outputPath.filePath()) {
			output->file_.open(*outputPath.filePath());
			if (!output->file_) {
				return core::Result<std::shared_ptr<AnomalyCsvTimingOutput>>::failure(
					core::Status::error(core::StatusCode::Unavailable,
						"benchmark output file could not be opened"));
			}
			output->stream_ = &output->file_;
		}
		*output->stream_ << "sequence,score,threshold,result,preprocess_ms,"
			"inference_ms,postprocess_ms,pipeline_ms\n";
		return core::Result<std::shared_ptr<AnomalyCsvTimingOutput>>::success(
			std::move(output));
	}

	void write(
		std::uint64_t sequenceNumber,
		const core::Result<vision::AnomalyResult>& result,
		const PipelineDurations& durations) {
		if (!result) {
			return;
		}

		std::ostringstream line;
		line << sequenceNumber << ',' << std::setprecision(9)
			<< result->score << ',' << result->threshold << ','
			<< vision::anomalyDecisionName(result->decision)
			<< ',' << std::fixed << std::setprecision(3)
			<< durations.preprocessMilliseconds << ','
			<< durations.inferenceMilliseconds << ','
			<< durations.postprocessMilliseconds << ','
			<< durations.totalMilliseconds << '\n';
		std::scoped_lock lock(mutex_);
		*stream_ << line.str();
	}

private:
	AnomalyCsvTimingOutput() = default;

	std::ofstream file_;
	std::ostream* stream_ = &std::cout;
	std::mutex mutex_;
};

} // namespace detail

[[nodiscard]] inline core::Result<std::unique_ptr<
	pipeline::IVisionPipeline<vision::AnomalyResult>>> makeAnomalyCsvTimedPipeline(
	pipeline::Pipeline<vision::AnomalyResult> pipeline,
	const AnomalyCsvTimingOptions& options) {
	using VisionPipeline = pipeline::IVisionPipeline<vision::AnomalyResult>;
	if (!options.activate) {
		return core::Result<std::unique_ptr<VisionPipeline>>::success(
			std::make_unique<pipeline::Pipeline<vision::AnomalyResult>>(
				std::move(pipeline)));
	}

	auto output = detail::AnomalyCsvTimingOutput::create(options.outputPath);
	if (!output) {
		return core::Result<std::unique_ptr<VisionPipeline>>::failure(output.status());
	}
	auto sharedOutput = std::move(output).value();
	return core::Result<std::unique_ptr<VisionPipeline>>::success(
		std::make_unique<TimedPipeline<vision::AnomalyResult>>(
			std::move(pipeline),
			[sharedOutput = std::move(sharedOutput)](
				std::uint64_t sequenceNumber,
				const core::Result<vision::AnomalyResult>& result,
				const PipelineDurations& durations) {
				sharedOutput->write(sequenceNumber, result, durations);
			}));
}

} // namespace visionRuntime::benchmark