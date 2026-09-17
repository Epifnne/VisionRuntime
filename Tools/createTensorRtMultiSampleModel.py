"""Converts the PatchCore anomaly ONNX (static batch 1) to a dynamic-batch
variant for the anomalyTensorRtMulti sample, and builds the TensorRT engine.

The PatchCore export hardcodes batch=1 on the `images` input and `score`
output, and its exported Reshape nodes carry allowzero=0 with runtime-computed
shape tensors — TensorRT's shape machine rejects that combination under
dynamic batch, so this script flips every Reshape to allowzero=1 (0 in the
shape tensor copies the corresponding input dimension) in addition to making
the `images`/`score` batch dimensions symbolic.

Usage: .venv/Scripts/python.exe Tools/createTensorRtMultiSampleModel.py
Outputs:
  Samples/anomalyTensorRtMulti/model/artifacts/patchcore-dynbatch.onnx
  Samples/anomalyTensorRtMulti/model/artifacts/patchcore-dynbatch.engine
"""

import subprocess
import sys
from pathlib import Path

import onnx

SOURCE_ONNX = Path(
	"E:/Work/0417A05/Code/python/PatchCore/models/"
	"class01_ok_front_raw_mild_aug_v11_gray/model.onnx")
ARTIFACTS = (
	Path(__file__).resolve().parent.parent
	/ "Samples/anomalyTensorRtMulti/model/artifacts")
TRTEXEC = (
	Path(__file__).resolve().parent.parent
	/ "Thirdparty/tensorrt/10.16.1.11/windows-x86_64"
	/ "TensorRT-10.16.1.11/bin/trtexec.exe")


def main():
	onnx_path = ARTIFACTS / "patchcore-dynbatch.onnx"
	engine_path = ARTIFACTS / "patchcore-dynbatch.engine"
	ARTIFACTS.mkdir(parents=True, exist_ok=True)

	model = onnx.load(str(SOURCE_ONNX))
	for tensor in list(model.graph.input) + list(model.graph.output):
		shape = tensor.type.tensor_type.shape
		if shape.dim and shape.dim[0].dim_value == 1:
			shape.dim[0].ClearField("dim_value")
			shape.dim[0].dim_param = "N"
	reshapes = 0
	for node in model.graph.node:
		if node.op_type != "Reshape":
			continue
		for attribute in node.attribute:
			if attribute.name == "allowzero":
				attribute.i = 1
				reshapes += 1
				break
		else:
			node.attribute.append(onnx.helper.make_attribute("allowzero", 1))
			reshapes += 1
	onnx.checker.check_model(model)
	onnx.save(model, str(onnx_path))
	print(f"saved {onnx_path} (reshapes allowzero=1: {reshapes})")

	if not TRTEXEC.is_file():
		sys.exit(f"trtexec not found: {TRTEXEC}")
	command = [
		str(TRTEXEC),
		f"--onnx={onnx_path}",
		"--minShapes=images:1x1x224x224",
		"--optShapes=images:4x1x224x224",
		"--maxShapes=images:8x1x224x224",
		f"--saveEngine={engine_path}",
		"--skipInference",
	]
	subprocess.run(command, check=True)
	print(f"saved {engine_path}")


if __name__ == "__main__":
	main()

