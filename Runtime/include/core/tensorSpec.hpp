#pragma once

#include "core/dataType.hpp"
#include "core/device.hpp"
#include "core/tensorShape.hpp"

#include <optional>

namespace visonRuntime::core {

enum class TensorLayout {
	Any,
	Nchw,
	Nhwc
};

struct TensorSpec {
	std::optional<DataType> dataType;
	std::optional<TensorShape> shape;
	std::optional<Device> device;
	TensorLayout layout = TensorLayout::Any;

	[[nodiscard]] bool isValid() const noexcept {
		return (!shape || shape->isValid()) && (!device || device->isValid());
	}

	[[nodiscard]] bool accepts(const TensorSpec& produced) const noexcept {
		if (!isValid() || !produced.isValid()) {
			return false;
		}
		if (dataType && (!produced.dataType || dataType != produced.dataType)) {
			return false;
		}
		if (shape && (!produced.shape || !shape->accepts(*produced.shape))) {
			return false;
		}
		if (device && (!produced.device || device != produced.device)) {
			return false;
		}
		if (layout != TensorLayout::Any && layout != produced.layout) {
			return false;
		}
		return true;
	}
};

} // namespace visonRuntime::core