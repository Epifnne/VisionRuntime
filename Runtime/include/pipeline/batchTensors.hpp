#pragma once

#include "core/result.hpp"
#include "core/status.hpp"
#include "core/tensor.hpp"
#include "core/tensorShape.hpp"
#include "preProcess/preparedInput.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

namespace visionRuntime::pipeline {

/**
 * Concatenates per-frame prepared tensors (each with batch dimension 1) into
 * one batch tensor map. Shared by Pipeline::inferBatch and by test doubles
 * that emulate batched backends, so the concatenation contract lives in
 * exactly one place.
 */
[[nodiscard]] inline core::Result<preprocess::TensorMap> concatBatchTensors(
	const std::vector<preprocess::PreparedInput>& inputs) {
	if (inputs.empty()) {
		return core::Result<preprocess::TensorMap>::failure(core::Status::error(
			core::StatusCode::InvalidArgument,
			"batch inference requires at least one input"));
	}
	preprocess::TensorMap batch;
	for (const auto& [name, firstTensor] : inputs.front().tensors()) {
		const auto& shape = firstTensor.shape().dimensions();
		if (shape.empty() || shape[0] != 1) {
			return core::Result<preprocess::TensorMap>::failure(
				core::Status::error(core::StatusCode::InvalidArgument,
					"batch inference requires per-frame tensors with batch dimension 1"));
		}
		const auto sampleBytes = firstTensor.byteSize();
		const auto batchBytes = sampleBytes * inputs.size();
		auto storage = std::shared_ptr<std::byte[]>(
			new std::byte[batchBytes], std::default_delete<std::byte[]>());
		for (std::size_t index = 0; index < inputs.size(); ++index) {
			const auto it = inputs[index].tensors().find(name);
			if (it == inputs[index].tensors().end() ||
				it->second.byteSize() != sampleBytes) {
				return core::Result<preprocess::TensorMap>::failure(
					core::Status::error(core::StatusCode::InvalidArgument,
						"batch members must share identical tensor names and shapes"));
			}
			std::memcpy(storage.get() + index * sampleBytes,
				it->second.data(), sampleBytes);
		}

		auto batchDims = shape;
		batchDims[0] = static_cast<std::int64_t>(inputs.size());
		auto tensor = core::Tensor::share(
			std::move(storage), batchBytes, firstTensor.dataType(),
			core::TensorShape(std::move(batchDims)), core::Device::cpu(),
			firstTensor.layout());
		if (!tensor) {
			return core::Result<preprocess::TensorMap>::failure(tensor.status());
		}
		batch.emplace(name, std::move(tensor).value());
	}
	return core::Result<preprocess::TensorMap>::success(std::move(batch));
}

/**
 * Slices a batch output tensor map back into per-sample tensor maps along the
 * batch dimension; slice i is a zero-copy subview of the batch tensor.
 */
[[nodiscard]] inline core::Result<std::vector<preprocess::TensorMap>>
splitBatchOutputs(
	const preprocess::TensorMap& batchOutputs, std::size_t batchSize) {
	std::vector<preprocess::TensorMap> perSample(batchSize);
	for (const auto& [name, batchTensor] : batchOutputs) {
		const auto& dims = batchTensor.shape().dimensions();
		if (dims.empty() ||
			dims[0] != static_cast<std::int64_t>(batchSize) ||
			batchTensor.byteSize() % batchSize != 0) {
			return core::Result<std::vector<preprocess::TensorMap>>::failure(
				core::Status::error(core::StatusCode::InvalidArgument,
					"batch inference output does not match the batch size"));
		}
		const auto sampleBytes = batchTensor.byteSize() / batchSize;
		auto sampleDims = dims;
		sampleDims[0] = 1;
		for (std::size_t index = 0; index < batchSize; ++index) {
			auto view = batchTensor.subview(
				index * sampleBytes, batchTensor.dataType(),
				core::TensorShape(sampleDims));
			if (!view) {
				return core::Result<std::vector<preprocess::TensorMap>>::failure(
					view.status());
			}
			perSample[index].emplace(name, std::move(view).value());
		}
	}
	return core::Result<std::vector<preprocess::TensorMap>>::success(
		std::move(perSample));
}

} // namespace visionRuntime::pipeline
