/**
 * @file fileSource.hpp
 * @author epifnne
 * @date 2026-08-11
 * @brief Declares an image-sequence frame source backed by a directory.
 */

#pragma once

#include "camera/cameraSourceOptions.hpp"
#include "camera/iFrameSource.hpp"
#include "core/result.hpp"

#include <memory>

namespace visionRuntime::camera {

class FileSource final : public IFrameSource {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<FileSource>> create(
		FileSourceOptions options);

	~FileSource() override;

	FileSource(const FileSource&) = delete;
	FileSource& operator=(const FileSource&) = delete;
	FileSource(FileSource&&) = delete;
	FileSource& operator=(FileSource&&) = delete;

	core::Result<void> start(FrameCallback callback) override;
	void requestStop() noexcept override;
	void wait() noexcept override;
	[[nodiscard]] bool isRunning() const noexcept override;
	[[nodiscard]] FrameSourceInfo info() const override;

	[[nodiscard]] const FileSourceOptions& options() const noexcept;

private:
	class Impl;

	explicit FileSource(std::unique_ptr<Impl> impl) noexcept;

	std::unique_ptr<Impl> impl_;
};

} // namespace visionRuntime::camera