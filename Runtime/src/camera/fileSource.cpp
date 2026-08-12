/**
 * @file fileSource.cpp
 * @author epifnne
 * @date 2026-08-11
 * @brief Implements an asynchronous directory-backed image frame source.
 */

#include "camera/fileSource.hpp"

#include "core/status.hpp"
#include "core/tensorBuffer.hpp"
#include "vision/frame.hpp"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace visonRuntime::camera {
namespace {

[[nodiscard]] core::Status error(core::StatusCode code, std::string message) {
	return core::Status::error(code, std::move(message));
}

[[nodiscard]] std::string normalizedExtension(std::string extension) {
	if (!extension.empty() && extension.front() != '.') {
		extension.insert(extension.begin(), '.');
	}
	std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return extension;
}

[[nodiscard]] bool hasAcceptedExtension(
	const std::filesystem::path& path,
	const std::vector<std::string>& extensions) {
	auto extension = path.extension().string();
	std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return std::ranges::find(extensions, extension) != extensions.end();
}

[[nodiscard]] core::Result<vision::Frame> decodeFrame(
	const std::filesystem::path& path,
	std::uint64_t sequenceNumber) {
	auto image = std::make_shared<cv::Mat>(
		cv::imread(path.string(), cv::IMREAD_UNCHANGED));
	if (image->empty()) {
		return core::Result<vision::Frame>::failure(
			error(core::StatusCode::DataLoss,
				"failed to decode image: " + path.string()));
	}

	vision::PixelFormat pixelFormat;
	if (image->type() == CV_8UC1) {
		pixelFormat = vision::PixelFormat::Gray8;
	} else if (image->type() == CV_16UC1) {
		pixelFormat = vision::PixelFormat::Gray16;
	} else if (image->type() == CV_32FC1) {
		pixelFormat = vision::PixelFormat::Float32Gray;
	} else if (image->type() == CV_8UC3) {
		pixelFormat = vision::PixelFormat::Bgr8;
	} else if (image->type() == CV_8UC4) {
		pixelFormat = vision::PixelFormat::Bgra8;
	} else {
		return core::Result<vision::Frame>::failure(
			error(core::StatusCode::Unsupported,
				"unsupported decoded image format: " + path.string()));
	}

	const auto rowStride = image->step[0];
	const auto rowBytes = static_cast<std::size_t>(image->cols) * image->elemSize();
	const auto byteSize =
		(static_cast<std::size_t>(image->rows) - 1) * rowStride + rowBytes;
	std::shared_ptr<void> owner = image;
	auto buffer = core::TensorBuffer::share(
		std::move(owner), image->data, byteSize);
	if (!buffer) {
		return core::Result<vision::Frame>::failure(buffer.status());
	}

	vision::FrameMetadata metadata;
	metadata.sequenceNumber = sequenceNumber;
	metadata.capturedAt = std::chrono::steady_clock::now();
	return vision::Frame::create(
		std::move(buffer).value(),
		static_cast<std::size_t>(image->cols),
		static_cast<std::size_t>(image->rows),
		pixelFormat,
		rowStride,
		metadata);
}

} // namespace

class FileSource::Impl {
public:
	Impl(FileSourceOptions options, std::vector<std::filesystem::path> imagePaths)
		: options_(std::move(options)), imagePaths_(std::move(imagePaths)) {}

	~Impl() {
		static_cast<void>(stop());
	}

	[[nodiscard]] core::Result<void> start(FrameCallback callback) {
		if (!callback) {
			return core::Result<void>::failure(
				error(core::StatusCode::InvalidArgument, "frame callback must not be empty"));
		}

		std::scoped_lock lock(mutex_);
		if (running_) {
			return core::Result<void>::failure(
				error(core::StatusCode::InvalidState, "file source is already running"));
		}
		running_ = true;
		worker_ = std::jthread(
			[this, callback = std::move(callback)](std::stop_token stopToken) mutable {
				run(stopToken, callback);
			});
		return core::Result<void>::success();
	}

	[[nodiscard]] core::Result<void> stop() {
		std::jthread worker;
		{
			std::scoped_lock lock(mutex_);
			if (!worker_.joinable()) {
				running_ = false;
				return core::Result<void>::success();
			}
			worker_.request_stop();
			if (worker_.get_id() == std::this_thread::get_id()) {
				return core::Result<void>::success();
			}
			worker = std::move(worker_);
		}
		worker.join();
		running_ = false;
		return core::Result<void>::success();
	}

	[[nodiscard]] bool isRunning() const noexcept {
		return running_;
	}

	[[nodiscard]] const FileSourceOptions& options() const noexcept {
		return options_;
	}

	[[nodiscard]] std::size_t imageCount() const noexcept {
		return imagePaths_.size();
	}

private:
	void run(std::stop_token stopToken, FrameCallback& callback) noexcept {
		std::uint64_t sequenceNumber = 0;
		try {
			do {
				for (const auto& imagePath : imagePaths_) {
					if (stopToken.stop_requested()) {
						running_ = false;
						return;
					}
					callback(decodeFrame(imagePath, sequenceNumber++));
					if (options_.frameInterval.count() > 0) {
						std::this_thread::sleep_for(options_.frameInterval);
					}
				}
			} while (options_.loop && !stopToken.stop_requested());
		} catch (...) {
		}
		running_ = false;
	}

	FileSourceOptions options_;
	std::vector<std::filesystem::path> imagePaths_;
	mutable std::mutex mutex_;
	std::jthread worker_;
	std::atomic_bool running_ = false;
};

core::Result<std::unique_ptr<FileSource>> FileSource::create(
	FileSourceOptions options) {
	if (options.directory.empty()) {
		return core::Result<std::unique_ptr<FileSource>>::failure(
			error(core::StatusCode::InvalidArgument, "image directory must not be empty"));
	}
	if (options.frameInterval.count() < 0) {
		return core::Result<std::unique_ptr<FileSource>>::failure(
			error(core::StatusCode::InvalidArgument, "frame interval must not be negative"));
	}

	std::error_code fileError;
	if (!std::filesystem::is_directory(options.directory, fileError)) {
		return core::Result<std::unique_ptr<FileSource>>::failure(
			error(core::StatusCode::NotFound,
				"image directory does not exist: " + options.directory.string()));
	}

	for (auto& extension : options.extensions) {
		extension = normalizedExtension(std::move(extension));
	}
	std::erase(options.extensions, std::string{});
	std::ranges::sort(options.extensions);
	options.extensions.erase(
		std::ranges::unique(options.extensions).begin(), options.extensions.end());
	if (options.extensions.empty()) {
		return core::Result<std::unique_ptr<FileSource>>::failure(
			error(core::StatusCode::InvalidArgument,
				"at least one image extension is required"));
	}

	std::vector<std::filesystem::path> imagePaths;
	auto appendEntry = [&](const std::filesystem::directory_entry& entry) {
		if (entry.is_regular_file(fileError) &&
			hasAcceptedExtension(entry.path(), options.extensions)) {
			imagePaths.push_back(entry.path());
		}
	};
	if (options.recursive) {
		for (std::filesystem::recursive_directory_iterator iterator(options.directory, fileError), end;
			 iterator != end && !fileError; iterator.increment(fileError)) {
			appendEntry(*iterator);
		}
	} else {
		for (std::filesystem::directory_iterator iterator(options.directory, fileError), end;
			 iterator != end && !fileError; iterator.increment(fileError)) {
			appendEntry(*iterator);
		}
	}
	if (fileError) {
		return core::Result<std::unique_ptr<FileSource>>::failure(
			error(core::StatusCode::Unavailable,
				"failed to enumerate image directory: " + fileError.message()));
	}
	if (imagePaths.empty()) {
		return core::Result<std::unique_ptr<FileSource>>::failure(
			error(core::StatusCode::NotFound,
				"image directory contains no matching files"));
	}

	if (options.order == FileOrder::LastWriteTime) {
		std::ranges::sort(imagePaths, [](const auto& left, const auto& right) {
			return std::filesystem::last_write_time(left) <
				std::filesystem::last_write_time(right);
		});
	} else {
		std::ranges::sort(imagePaths);
	}

	auto impl = std::make_unique<Impl>(std::move(options), std::move(imagePaths));
	return core::Result<std::unique_ptr<FileSource>>::success(
		std::unique_ptr<FileSource>(new FileSource(std::move(impl))));
}

FileSource::FileSource(std::unique_ptr<Impl> impl) noexcept
	: impl_(std::move(impl)) {}

FileSource::~FileSource() = default;

core::Result<void> FileSource::start(FrameCallback callback) {
	return impl_->start(std::move(callback));
}

core::Result<void> FileSource::stop() {
	return impl_->stop();
}

bool FileSource::isRunning() const noexcept {
	return impl_->isRunning();
}

const FileSourceOptions& FileSource::options() const noexcept {
	return impl_->options();
}

std::size_t FileSource::imageCount() const noexcept {
	return impl_->imageCount();
}

} // namespace visonRuntime::camera