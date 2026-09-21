"""Generates the dynamic-batch benchmark model for the rubberRingBatch4 product.

The rubberRing IR cannot serve batched inference: its internal Reshape node
hardcodes batch=1, so ov.reshape on the input is rejected at inference time.
This script instead exports a small convolutional scorer whose input batch
dimension is dynamic from the start ([-1, 3, 224, 224] -> [N, 1]), which the
OpenVINO plugin can compile once and run with any N.

Usage: .venv/Scripts/python.exe Tools/createBatchBenchModel.py
Output: Shell/products/rubberRingBatch4/model/artifacts/model-dynbatch-fp32.{xml,bin}
"""

from pathlib import Path

import numpy as np
import openvino as ov
from openvino import opset13 as ops

OUTPUT = (
	Path(__file__).resolve().parent.parent
	/ "Shell/products/rubberRingBatch4/model/artifacts/model-dynbatch-fp32.xml"
)


def conv(data, in_channels, out_channels, kernel, stride, seed, name):
	rng = np.random.RandomState(seed)
	scale = np.float32(1.0 / np.sqrt(in_channels * kernel * kernel))
	weights = rng.randn(out_channels, in_channels, kernel, kernel).astype(np.float32) * scale
	return ops.convolution(
		data,
		ops.constant(weights),
		strides=[stride, stride],
		pads_begin=[kernel // 2, kernel // 2],
		pads_end=[kernel // 2, kernel // 2],
		dilations=[1, 1],
		name=name,
	)


def main():
	images = ops.parameter(
		ov.PartialShape([-1, 3, 224, 224]), ov.Type.f32, name="images")

	x = ops.relu(conv(images, 3, 32, kernel=5, stride=2, seed=1, name="conv1"))
	x = ops.relu(conv(x, 32, 64, kernel=3, stride=2, seed=2, name="conv2"))
	x = ops.relu(conv(x, 64, 128, kernel=3, stride=2, seed=3, name="conv3"))
	x = ops.relu(conv(x, 128, 128, kernel=3, stride=1, seed=5, name="conv4"))

	pooled = ops.reduce_mean(x, reduction_axes=[2, 3], keep_dims=True)
	score4d = conv(pooled, 128, 1, kernel=1, stride=1, seed=4, name="score_conv")
	squeezed = ops.squeeze(score4d, ops.constant(np.array([2, 3], dtype=np.int64)))
	score = ops.result(squeezed, name="score")

	model = ov.Model([score], [images], "rubber_ring_batch_bench")
	OUTPUT.parent.mkdir(parents=True, exist_ok=True)
	ov.save_model(model, OUTPUT)

	core = ov.Core()
	compiled = core.compile_model(str(OUTPUT), "CPU")
	for batch in (1, 2, 4):
		request = compiled.create_infer_request()
		request.infer({"images": np.zeros((batch, 3, 224, 224), dtype=np.float32)})
		out = request.get_tensor("score")
		assert out.shape == (batch, 1), out.shape
	print(f"saved {OUTPUT}; verified batches 1/2/4 -> [N, 1] named 'score'")


if __name__ == "__main__":
	main()
