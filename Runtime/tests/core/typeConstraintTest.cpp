#include "core/tensorSpec.hpp"
#include "pipeline/port.hpp"
#include "vision/frameSpec.hpp"

#include <gtest/gtest.h>

namespace visonRuntime::core {

class Tensor;

} // namespace visonRuntime::core

namespace visonRuntime::vision {

class Frame;

} // namespace visonRuntime::vision

namespace {

template<typename Output, typename Input>
concept Connectable = requires(const Output& output, const Input& input) {
	output.canConnect(input);
};

using TensorInput = visonRuntime::pipeline::InputPort<
	visonRuntime::core::Tensor, visonRuntime::core::TensorSpec>;
using TensorOutput = visonRuntime::pipeline::OutputPort<
	visonRuntime::core::Tensor, visonRuntime::core::TensorSpec>;
using FrameInput = visonRuntime::pipeline::InputPort<
	visonRuntime::vision::Frame, visonRuntime::vision::FrameSpec>;

static_assert(Connectable<TensorOutput, TensorInput>);
static_assert(!Connectable<TensorOutput, FrameInput>);

} // namespace

TEST(TypeConstraintTest, AcceptsCompatibleTensorPorts) {
	using namespace visonRuntime;

	const core::TensorSpec modelInput{
		core::DataType::Float32,
		core::TensorShape{1, 3, 640, 640},
		core::Device::cuda(),
		core::TensorLayout::Nchw
	};
	const pipeline::InputPort<core::Tensor, core::TensorSpec> input(modelInput);

	const pipeline::OutputPort<core::Tensor, core::TensorSpec> compatible({
		core::DataType::Float32,
		core::TensorShape{1, 3, 640, 640},
		core::Device::cuda(),
		core::TensorLayout::Nchw
	});
	EXPECT_TRUE(compatible.canConnect(input));
}

TEST(TypeConstraintTest, RejectsMismatchedTensorShape) {
	using namespace visonRuntime;

	const pipeline::InputPort<core::Tensor, core::TensorSpec> input({
		core::DataType::Float32,
		core::TensorShape{1, 3, 640, 640},
		core::Device::cuda(),
		core::TensorLayout::Nchw
	});

	const pipeline::OutputPort<core::Tensor, core::TensorSpec> wrongShape({
		core::DataType::Float32,
		core::TensorShape{1, 3, 320, 320},
		core::Device::cuda(),
		core::TensorLayout::Nchw
	});
	EXPECT_FALSE(wrongShape.canConnect(input));
}