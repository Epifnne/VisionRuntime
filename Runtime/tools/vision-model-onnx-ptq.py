#!/usr/bin/env python3
"""Quantize an ONNX model to INT8 QDQ format with representative images."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import onnx
from PIL import Image
from onnxruntime.quantization import (
	CalibrationDataReader,
	CalibrationMethod,
	QuantFormat,
	QuantType,
	quantize_static,
)


IMAGE_EXTENSIONS = {".bmp", ".jpeg", ".jpg", ".png", ".tif", ".tiff", ".webp"}


def file_sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def find_images(root: Path) -> list[Path]:
	paths = sorted(path for path in root.rglob("*") if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS)
	if not paths:
		raise ValueError(f"no images found under: {root}")
	return paths


def preprocess_image(path: Path) -> np.ndarray:
	with Image.open(path) as source:
		image = source.convert("L")
		width, height = image.size
		if width <= height:
			resized = (256, round(height * 256 / width))
		else:
			resized = (round(width * 256 / height), 256)
		image = image.resize(resized, Image.Resampling.BILINEAR)
		left = round((image.width - 224) / 2)
		top = round((image.height - 224) / 2)
		image = image.crop((left, top, left + 224, top + 224))
		values = np.asarray(image, dtype=np.float32) / 255.0
	return ((values - 0.449) / 0.226)[np.newaxis, np.newaxis, :, :]


class ImageCalibrationReader(CalibrationDataReader):
	def __init__(self, input_name: str, paths: list[Path]) -> None:
		self.input_name = input_name
		self.paths = paths
		self.iterator = iter(paths)

	def get_next(self) -> dict[str, np.ndarray] | None:
		try:
			path = next(self.iterator)
		except StopIteration:
			return None
		return {self.input_name: preprocess_image(path)}

	def rewind(self) -> None:
		self.iterator = iter(self.paths)


def validate_models(original_path: Path, quantized_path: Path, paths: list[Path]) -> dict[str, float | int]:
	import openvino as ov

	core = ov.Core()
	original = core.compile_model(str(original_path), "CPU")
	quantized = core.compile_model(str(quantized_path), "CPU")
	original_values: list[float] = []
	quantized_values: list[float] = []
	for path in paths:
		input_tensor = preprocess_image(path)
		original_values.extend(np.asarray(original([input_tensor])[0]).reshape(-1).tolist())
		quantized_values.extend(np.asarray(quantized([input_tensor])[0]).reshape(-1).tolist())
	original_array = np.asarray(original_values, dtype=np.float64)
	quantized_array = np.asarray(quantized_values, dtype=np.float64)
	absolute_error = np.abs(original_array - quantized_array)
	correlation = float(np.corrcoef(original_array, quantized_array)[0, 1]) if original_array.size > 1 else 1.0
	return {
		"image_count": len(paths),
		"mean_absolute_error": float(np.mean(absolute_error)),
		"max_absolute_error": float(np.max(absolute_error)),
		"pearson_correlation": correlation,
	}


def parse_arguments() -> argparse.Namespace:
	parser = argparse.ArgumentParser(description="Run static PTQ and save an INT8 QDQ ONNX model.")
	parser.add_argument("model", type=Path, help="source ONNX model")
	parser.add_argument("--calibration-dir", type=Path, required=True, help="representative image directory")
	parser.add_argument("--validation-dir", type=Path, help="images used to compare FP32 and INT8 outputs")
	parser.add_argument("-o", "--output", type=Path, required=True, help="output INT8 .onnx path")
	parser.add_argument("--record", type=Path, help="build record path (default: <output>.build.json)")
	return parser.parse_args()


def main() -> int:
	arguments = parse_arguments()
	source = arguments.model.resolve()
	calibration_root = arguments.calibration_dir.resolve()
	validation_root = arguments.validation_dir.resolve() if arguments.validation_dir else None
	output = arguments.output.resolve()
	if source.suffix.lower() != ".onnx" or not source.is_file():
		raise ValueError(f"ONNX model was not found: {source}")
	if output.suffix.lower() != ".onnx":
		raise ValueError("output path must have the .onnx extension")
	if not calibration_root.is_dir():
		raise ValueError(f"calibration directory was not found: {calibration_root}")
	if validation_root is not None and not validation_root.is_dir():
		raise ValueError(f"validation directory was not found: {validation_root}")

	model = onnx.load(source, load_external_data=False)
	if len(model.graph.input) != 1:
		raise ValueError(f"only single-input models are supported; found {len(model.graph.input)} inputs")
	input_name = model.graph.input[0].name
	calibration_paths = find_images(calibration_root)
	output.parent.mkdir(parents=True, exist_ok=True)
	quantize_static(
		model_input=source,
		model_output=output,
		calibration_data_reader=ImageCalibrationReader(input_name, calibration_paths),
		quant_format=QuantFormat.QDQ,
		activation_type=QuantType.QInt8,
		weight_type=QuantType.QInt8,
		per_channel=True,
		calibrate_method=CalibrationMethod.MinMax,
		op_types_to_quantize=["Conv"],
		extra_options={"ActivationSymmetric": True, "WeightSymmetric": True},
	)
	quantized_model = onnx.load(output, load_external_data=False)
	q_node_count = sum(node.op_type == "QuantizeLinear" for node in quantized_model.graph.node)
	dq_node_count = sum(node.op_type == "DequantizeLinear" for node in quantized_model.graph.node)
	int8_initializer_count = sum(initializer.data_type in {onnx.TensorProto.INT8, onnx.TensorProto.UINT8} for initializer in quantized_model.graph.initializer)
	if q_node_count == 0 or int8_initializer_count == 0:
		raise RuntimeError("quantized ONNX does not contain QDQ nodes and INT8 initializers")

	validation = None
	if validation_root is not None:
		validation = validate_models(source, output, find_images(validation_root))
	record_path = (arguments.record or output.with_name(output.name + ".build.json")).resolve()
	record = {
		"schema_version": 1,
		"source": {"path": str(source), "sha256": file_sha256(source)},
		"quantization": "ONNX Runtime static INT8 QDQ",
		"calibration": {"path": str(calibration_root), "image_count": len(calibration_paths)},
		"quantized_graph": {
			"quantize_linear_nodes": q_node_count,
			"dequantize_linear_nodes": dq_node_count,
			"int8_initializers": int8_initializer_count,
		},
		"artifact": {"path": str(output), "sha256": file_sha256(output)},
	}
	if validation is not None:
		record["validation"] = {"path": str(validation_root), **validation}
	record_path.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
	print(f"Calibration images: {len(calibration_paths)}")
	print(f"INT8 QDQ ONNX: {output}")
	print(f"Q/DQ nodes: {q_node_count}/{dq_node_count}; INT8 initializers: {int8_initializer_count}")
	if validation is not None:
		print(
			f"Validation: {validation['image_count']} images, "
			f"MAE={validation['mean_absolute_error']:.6g}, "
			f"max={validation['max_absolute_error']:.6g}, "
			f"correlation={validation['pearson_correlation']:.6g}"
		)
	return 0


if __name__ == "__main__":
	sys.exit(main())