#pragma once

#include <cstddef>

namespace visionRuntime::core {

enum class DataType {
	UInt8,
	Int8,
	UInt16,
	Int16,
	Int32,
	Int64,
	Float16,
	Float32,
	Float64,
	Bool
};

[[nodiscard]] constexpr std::size_t dataTypeSize(DataType dataType) noexcept {
	switch (dataType) {
	case DataType::UInt8:
	case DataType::Int8:
	case DataType::Bool:
		return 1;
	case DataType::UInt16:
	case DataType::Int16:
	case DataType::Float16:
		return 2;
	case DataType::Int32:
	case DataType::Float32:
		return 4;
	case DataType::Int64:
	case DataType::Float64:
		return 8;
	}

	return 0;
}

} // namespace visionRuntime::core