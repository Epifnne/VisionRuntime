/**
 * @file fileSource.hpp
 * @author epifnne
 * @date 2026-08-11
 * @brief Declares an image-sequence frame source backed by a directory.
 */

#pragma once

#include "camera/iFrameSource.hpp"
#include "core/result.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace visonRuntime::camera {

enum class FileOrder {
	Lexicographical,
	LastWriteTime
};

struct FileSourceOptions {
	std::filesystem::path directory;
	std::vector<std::string> extensions{
		".bmp", ".jpeg", ".jpg", ".png", ".tif", ".tiff"};
	FileOrder order = FileOrder::Lexicographical;
	std::chrono::milliseconds frameInterval{0};
	bool recursive = false;
	bool loop = false;
};

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
	core::Result<void> stop() override;
	[[nodiscard]] bool isRunning() const noexcept override;

	[[nodiscard]] const FileSourceOptions& options() const noexcept;
	[[nodiscard]] std::size_t imageCount() const noexcept;

private:
	class Impl;

	explicit FileSource(std::unique_ptr<Impl> impl) noexcept;

	std::unique_ptr<Impl> impl_;
};

} // namespace visonRuntime::camera