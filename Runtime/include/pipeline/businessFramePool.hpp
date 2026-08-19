/**
 * @file businessFramePool.hpp
 * @author epifnne
 * @date 2026-08-10
 * @brief Defines a reusable frame pool for cropped, copied, and rendered business images.
 */

#pragma once

#include "core/tensorBufferPool.hpp"
#include "vision/frame.hpp"

#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace visionRuntime::pipeline {

class BusinessFramePool {
public:
	BusinessFramePool() = default;

	[[nodiscard]] static core::Result<BusinessFramePool> create(
		std::size_t bufferCount,
		std::size_t width,
		std::size_t height,
		vision::PixelFormat pixelFormat,
		std::size_t rowStride = 0) {
		if (width == 0 || height == 0) {
			return invalidArgument("business frame dimensions must be greater than zero");
		}
		const auto pixelBytes = vision::pixelFormatSize(pixelFormat);
		if (pixelBytes == 0) {
			return invalidArgument("business frame pixel format is invalid");
		}
		if (width > std::numeric_limits<std::size_t>::max() / pixelBytes) {
			return invalidArgument("business frame row size overflows size_t");
		}
		const auto rowBytes = width * pixelBytes;
		if (rowStride == 0) {
			rowStride = rowBytes;
		}
		if (rowStride < rowBytes || height >
			std::numeric_limits<std::size_t>::max() / rowStride) {
			return invalidArgument("business frame layout is invalid");
		}
		const auto capacity = height * rowStride;
		auto pool = core::TensorBufferPool::create(bufferCount, capacity);
		if (!pool) {
			return core::Result<BusinessFramePool>::failure(pool.status());
		}
		return core::Result<BusinessFramePool>::success(BusinessFramePool(
			std::move(pool).value(), width, height, rowStride, pixelFormat));
	}

	[[nodiscard]] core::Result<vision::Frame> acquire(
		vision::FrameMetadata metadata = {}) const {
		auto buffer = pool_.acquire();
		if (!buffer) {
			return core::Result<vision::Frame>::failure(buffer.status());
		}
		return vision::Frame::create(
			std::move(buffer).value(), width_, height_, pixelFormat_, rowStride_,
			std::move(metadata));
	}

	[[nodiscard]] std::size_t size() const noexcept { return pool_.size(); }
	[[nodiscard]] std::size_t available() const { return pool_.available(); }
	[[nodiscard]] std::size_t width() const noexcept { return width_; }
	[[nodiscard]] std::size_t height() const noexcept { return height_; }
	[[nodiscard]] std::size_t rowStride() const noexcept { return rowStride_; }
	[[nodiscard]] vision::PixelFormat pixelFormat() const noexcept { return pixelFormat_; }

private:
	BusinessFramePool(
		core::TensorBufferPool pool,
		std::size_t width,
		std::size_t height,
		std::size_t rowStride,
		vision::PixelFormat pixelFormat)
		: pool_(std::move(pool)),
		  width_(width),
		  height_(height),
		  rowStride_(rowStride),
		  pixelFormat_(pixelFormat) {}

	[[nodiscard]] static core::Result<BusinessFramePool> invalidArgument(
		std::string message) {
		return core::Result<BusinessFramePool>::failure(core::Status::error(
			core::StatusCode::InvalidArgument, std::move(message)));
	}

	core::TensorBufferPool pool_;
	std::size_t width_ = 0;
	std::size_t height_ = 0;
	std::size_t rowStride_ = 0;
	vision::PixelFormat pixelFormat_ = vision::PixelFormat::Gray8;
};

} // namespace visionRuntime::pipeline