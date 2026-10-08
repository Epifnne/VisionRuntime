#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace visionRuntime::benchmark {

struct LatencyStatistics {
	std::size_t samples = 0;
	double p50Milliseconds = 0.0;
	double p95Milliseconds = 0.0;
	double p99Milliseconds = 0.0;
};

class LatencyWindow {
public:
	static constexpr std::size_t capacity = 4096;

	void observe(double milliseconds) noexcept {
		values_[next_] = milliseconds;
		next_ = (next_ + 1) % capacity;
		count_ = std::min(count_ + 1, capacity);
	}

	[[nodiscard]] LatencyStatistics snapshot() const noexcept {
		auto sorted = values_;
		std::ranges::sort(std::span<double>(sorted.data(), count_));
		const auto percentile = [&sorted, this](double quantile) {
			if (count_ == 0) {
				return 0.0;
			}
			const auto rank = static_cast<std::size_t>(
				std::ceil(quantile * static_cast<double>(count_)));
			return sorted[std::max<std::size_t>(rank, 1) - 1];
		};
		return {count_, percentile(0.50), percentile(0.95), percentile(0.99)};
	}

private:
	std::array<double, capacity> values_{};
	std::size_t next_ = 0;
	std::size_t count_ = 0;
};

} // namespace visionRuntime::benchmark
