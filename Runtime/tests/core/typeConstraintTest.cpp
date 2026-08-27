#include "camera/iCameraDevice.hpp"
#include "camera/iFrameSource.hpp"
#include "core/tensorSpec.hpp"
#include "pipeline/port.hpp"
#include "vision/frameSpec.hpp"

#include <gtest/gtest.h>

#include <concepts>

namespace visionRuntime::core {

class Tensor;

} // namespace visionRuntime::core

namespace visionRuntime::vision {

class Frame;

} // namespace visionRuntime::vision

namespace {

template<typename Output, typename Input>
concept Connectable = requires(const Output& output, const Input& input) {
	output.canConnect(input);
};

using TensorInput = visionRuntime::pipeline::InputPort<
	visionRuntime::core::Tensor, visionRuntime::core::TensorSpec>;
using TensorOutput = visionRuntime::pipeline::OutputPort<
	visionRuntime::core::Tensor, visionRuntime::core::TensorSpec>;
using FrameInput = visionRuntime::pipeline::InputPort<
	visionRuntime::vision::Frame, visionRuntime::vision::FrameSpec>;

static_assert(Connectable<TensorOutput, TensorInput>);
static_assert(!Connectable<TensorOutput, FrameInput>);
static_assert(!std::derived_from<
	visionRuntime::camera::ICameraDevice,
	visionRuntime::camera::IFrameSource>);

} // namespace

TEST(TypeConstraintTest, AcceptsCompatibleTensorPorts) {
	using namespace visionRuntime;

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
	using namespace visionRuntime;

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