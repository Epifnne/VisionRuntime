#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace visionRuntime::core {

class TensorShape {
public:
	TensorShape() = default;
	TensorShape(std::initializer_list<std::int64_t> dimensions)
		: dimensions_(dimensions) {}
	explicit TensorShape(std::vector<std::int64_t> dimensions)
		: dimensions_(std::move(dimensions)) {}

	[[nodiscard]] std::size_t rank() const noexcept {
		return dimensions_.size();
	}

	[[nodiscard]] const std::vector<std::int64_t>& dimensions() const noexcept {
		return dimensions_;
	}

	[[nodiscard]] bool isValid() const noexcept {
		for (const auto dimension : dimensions_) {
			if (dimension == 0 || dimension < -1) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] bool isConcrete() const noexcept {
		for (const auto dimension : dimensions_) {
			if (dimension <= 0) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] bool accepts(const TensorShape& produced) const noexcept {
		if (!isValid() || !produced.isValid() || rank() != produced.rank()) {
			return false;
		}

		for (std::size_t index = 0; index < rank(); ++index) {
			const auto requiredDimension = dimensions_[index];
			const auto producedDimension = produced.dimensions_[index];
			if (requiredDimension != -1 && requiredDimension != producedDimension) {
				return false;
			}
		}
		return true;
	}

	friend bool operator==(const TensorShape& left, const TensorShape& right) noexcept {
		return left.dimensions_ == right.dimensions_;
	}

private:
	std::vector<std::int64_t> dimensions_;
};

} // namespace visionRuntime::core