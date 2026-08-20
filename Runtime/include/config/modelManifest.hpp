#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace visionRuntime::config {

enum class TensorElementType {
	Float32
};

enum class TensorLayout {
	Nchw,
	Scalar
};

struct TensorManifest {
	std::string name;
	TensorElementType elementType = TensorElementType::Float32;
	TensorLayout layout = TensorLayout::Nchw;
	std::vector<std::size_t> shape;
};

struct ModelManifest {
	std::vector<TensorManifest> inputs;
	std::vector<TensorManifest> outputs;
};

} // namespace visionRuntime::config