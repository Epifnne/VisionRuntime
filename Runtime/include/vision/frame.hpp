/**
 * @file frame.hpp
 * @author epifnne
 * @date 2026-08-10
 * @brief Defines zero-copy image frame views over shared memory buffers.
 */

#pragma once

#include "core/result.hpp"
#include "core/tensorBuffer.hpp"
#include "vision/frameMetadata.hpp"
#include "vision/frameSpec.hpp"

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace visonRuntime::vision {

class Frame {
public:
	Frame() = default;
	Frame(const Frame&) = delete;
	Frame& operator=(const Frame&) = delete;
	Frame(Frame&& other) noexcept
		: buffer_(std::move(other.buffer_)),
		  byteOffset_(std::exchange(other.byteOffset_, 0)),
		  byteSize_(std::exchange(other.byteSize_, 0)),
		  width_(std::exchange(other.width_, 0)),
		  height_(std::exchange(other.height_, 0)),
		  rowStride_(std::exchange(other.rowStride_, 0)),
		  pixelFormat_(other.pixelFormat_),
		  metadata_(std::move(other.metadata_)) {}

	Frame& operator=(Frame&& other) noexcept {
		if (this != &other) {
			buffer_ = std::move(other.buffer_);
			byteOffset_ = std::exchange(other.byteOffset_, 0);
			byteSize_ = std::exchange(other.byteSize_, 0);
			width_ = std::exchange(other.width_, 0);
			height_ = std::exchange(other.height_, 0);
			rowStride_ = std::exchange(other.rowStride_, 0);
			pixelFormat_ = other.pixelFormat_;
			metadata_ = std::move(other.metadata_);
		}
		return *this;
	}

	[[nodiscard]] static core::Result<Frame> create(
		core::TensorBuffer buffer,
		std::size_t width,
		std::size_t height,
		PixelFormat pixelFormat,
		std::size_t rowStride = 0,
		FrameMetadata metadata = {},
		std::size_t byteOffset = 0) {
		if (!buffer) {
			return invalidArgument("frame buffer must not be empty");
		}
		if (width == 0 || height == 0) {
			return invalidArgument("frame dimensions must be greater than zero");
		}

		const auto pixelBytes = pixelFormatSize(pixelFormat);
		if (pixelBytes == 0) {
			return invalidArgument("frame pixel format is invalid");
		}
		if (width > std::numeric_limits<std::size_t>::max() / pixelBytes) {
			return invalidArgument("frame row size overflows size_t");
		}
		const auto rowBytes = width * pixelBytes;
		if (rowStride == 0) {
			rowStride = rowBytes;
		}
		if (rowStride < rowBytes) {
			return invalidArgument("frame row stride is smaller than one pixel row");
		}
		if (height - 1 >
			(std::numeric_limits<std::size_t>::max() - rowBytes) / rowStride) {
			return invalidArgument("frame byte size overflows size_t");
		}
		const auto byteSize = (height - 1) * rowStride + rowBytes;
		if (byteOffset > buffer.capacity() ||
			byteSize > buffer.capacity() - byteOffset) {
			return invalidArgument("frame view exceeds its buffer capacity");
		}

		return core::Result<Frame>::success(Frame(
			std::move(buffer), byteOffset, byteSize, width, height, rowStride,
			pixelFormat, std::move(metadata)));
	}

	[[nodiscard]] bool empty() const noexcept { return !buffer_; }
	[[nodiscard]] explicit operator bool() const noexcept { return !empty(); }
	[[nodiscard]] void* data() noexcept {
		return empty() || !buffer_.isWritable()
			? nullptr
			: static_cast<std::byte*>(buffer_.data()) + byteOffset_;
	}
	[[nodiscard]] const void* data() const noexcept {
		return empty()
			? nullptr
			: static_cast<const std::byte*>(buffer_.data()) + byteOffset_;
	}
	[[nodiscard]] std::size_t width() const noexcept { return width_; }
	[[nodiscard]] std::size_t height() const noexcept { return height_; }
	[[nodiscard]] std::size_t rowStride() const noexcept { return rowStride_; }
	[[nodiscard]] std::size_t byteSize() const noexcept { return byteSize_; }
	[[nodiscard]] std::size_t byteOffset() const noexcept { return byteOffset_; }
	[[nodiscard]] PixelFormat pixelFormat() const noexcept { return pixelFormat_; }
	[[nodiscard]] const FrameMetadata& metadata() const noexcept { return metadata_; }
	[[nodiscard]] const core::TensorBuffer& buffer() const noexcept { return buffer_; }

	[[nodiscard]] FrameSpec spec() const {
		return {{pixelFormat_}, width_, height_, buffer_.device()};
	}

	[[nodiscard]] std::span<std::byte> bytes() noexcept {
		if (!buffer_.isHostAccessible() || !buffer_.isWritable()) {
			return {};
		}
		return {static_cast<std::byte*>(data()), byteSize_};
	}

	[[nodiscard]] std::span<const std::byte> bytes() const noexcept {
		if (!buffer_.isHostAccessible()) {
			return {};
		}
		return {static_cast<const std::byte*>(data()), byteSize_};
	}

private:
	Frame(
		core::TensorBuffer buffer,
		std::size_t byteOffset,
		std::size_t byteSize,
		std::size_t width,
		std::size_t height,
		std::size_t rowStride,
		PixelFormat pixelFormat,
		FrameMetadata metadata)
		: buffer_(std::move(buffer)),
		  byteOffset_(byteOffset),
		  byteSize_(byteSize),
		  width_(width),
		  height_(height),
		  rowStride_(rowStride),
		  pixelFormat_(pixelFormat),
		  metadata_(std::move(metadata)) {}

	[[nodiscard]] static core::Result<Frame> invalidArgument(std::string message) {
		return core::Result<Frame>::failure(
			core::Status::error(core::StatusCode::InvalidArgument, std::move(message)));
	}

	core::TensorBuffer buffer_;
	std::size_t byteOffset_ = 0;
	std::size_t byteSize_ = 0;
	std::size_t width_ = 0;
	std::size_t height_ = 0;
	std::size_t rowStride_ = 0;
	PixelFormat pixelFormat_ = PixelFormat::Gray8;
	FrameMetadata metadata_;
};

} // namespace visonRuntime::vision