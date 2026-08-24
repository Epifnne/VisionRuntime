#!/usr/bin/env python3
"""Quantize an ONNX model to an OpenVINO INT8 IR with representative images."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image


IMAGE_EXTENSIONS = {".bmp", ".jpeg", ".jpg", ".png", ".tif", ".tiff", ".webp"}


def file_sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def image_paths(root: Path) -> list[Path]:
	paths = sorted(path for path in root.rglob("*") if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS)
	if not paths:
		raise ValueError(f"no calibration images found under: {root}")
	return paths


def preprocess_image(path: Path, resize: int, image_size: int, mean: float, std: float) -> np.ndarray:
	with Image.open(path) as source:
		image = source.convert("L")
		width, height = image.size
		if width <= height:
			resized = (resize, round(height * resize / width))
		else:
			resized = (round(width * resize / height), resize)
		image = image.resize(resized, Image.Resampling.BILINEAR)
		left = round((image.width - image_size) / 2)
		top = round((image.height - image_size) / 2)
		image = image.crop((left, top, left + image_size, top + image_size))
		values = np.asarray(image, dtype=np.float32) / 255.0
	return ((values - mean) / std)[np.newaxis, np.newaxis, :, :]


def port_description(port: Any) -> dict[str, str]:
	try:
		name = port.get_any_name()
	except RuntimeError:
		name = ""
	return {
		"name": name,
		"element_type": port.get_element_type().get_type_name(),
		"partial_shape": str(port.get_partial_shape()),
	}


def validate_models(
	original_model: Any,
	quantized_model: Any,
	paths: list[Path],
	resize: int,
	image_size: int,
	mean: float,
	std: float,
) -> dict[str, float | int]:
	import openvino as ov

	core = ov.Core()
	original = core.compile_model(original_model, "CPU")
	quantized = core.compile_model(quantized_model, "CPU")
	original_values: list[float] = []
	quantized_values: list[float] = []
	for path in paths:
		input_tensor = preprocess_image(path, resize, image_size, mean, std)
		original_values.extend(np.asarray(original([input_tensor])[0]).reshape(-1).tolist())
		quantized_values.extend(np.asarray(quantized([input_tensor])[0]).reshape(-1).tolist())
	original_array = np.asarray(original_values, dtype=np.float64)
	quantized_array = np.asarray(quantized_values, dtype=np.float64)
	absolute_error = np.abs(original_array - quantized_array)
	correlation = float(np.corrcoef(original_array, quantized_array)[0, 1]) if original_array.size > 1 else 1.0
	return {
		"image_count": len(paths),
		"output_value_count": int(original_array.size),
		"mean_absolute_error": float(np.mean(absolute_error)),
		"max_absolute_error": float(np.max(absolute_error)),
		"pearson_correlation": correlation,
	}


def parse_arguments() -> argparse.Namespace:
	parser = argparse.ArgumentParser(
		prog="vision-model-ptq",
		description="Run NNCF post-training quantization and save an OpenVINO INT8 IR.",
	)
	parser.add_argument("model", type=Path, help="source ONNX model")
	parser.add_argument("--calibration-dir", type=Path, required=True, help="representative image directory")
	parser.add_argument("--validation-dir", type=Path, help="images used to compare original and quantized outputs")
	parser.add_argument("-o", "--output", type=Path, required=True, help="output OpenVINO IR .xml path")
	parser.add_argument("--resize", type=int, default=256, help="resize shorter side")
	parser.add_argument("--image-size", type=int, default=224, help="center crop size")
	parser.add_argument("--mean", type=float, default=0.449, help="single-channel normalization mean")
	parser.add_argument("--std", type=float, default=0.226, help="single-channel normalization standard deviation")
	parser.add_argument("--record", type=Path, help="build record path (default: <output>.build.json)")
	return parser.parse_args()


def main() -> int:
	arguments = parse_arguments()
	source = arguments.model.resolve()
	calibration_root = arguments.calibration_dir.resolve()
	validation_root = arguments.validation_dir.resolve() if arguments.validation_dir else None
	output_xml = arguments.output.resolve()
	if source.suffix.lower() != ".onnx" or not source.is_file():
		raise ValueError(f"ONNX model was not found: {source}")
	if not calibration_root.is_dir():
		raise ValueError(f"calibration directory was not found: {calibration_root}")
	if validation_root is not None and not validation_root.is_dir():
		raise ValueError(f"validation directory was not found: {validation_root}")
	if output_xml.suffix.lower() != ".xml":
		raise ValueError("output path must have the .xml extension")
	if arguments.resize < arguments.image_size or arguments.image_size <= 0 or arguments.std == 0.0:
		raise ValueError("resize/image-size/std values are invalid")

	import nncf
	import openvino as ov

	paths = image_paths(calibration_root)
	model = ov.convert_model(source)
	if len(model.inputs) != 1:
		raise ValueError(f"only single-input models are supported; found {len(model.inputs)} inputs")
	input_name = model.input().get_any_name()
	expected_shape = model.input().get_partial_shape()
	first_input = preprocess_image(paths[0], arguments.resize, arguments.image_size, arguments.mean, arguments.std)
	if expected_shape.is_static and list(expected_shape.get_shape()) != list(first_input.shape):
		raise ValueError(f"preprocessed shape {list(first_input.shape)} does not match model input {list(expected_shape.get_shape())}")

	dataset = nncf.Dataset(
		paths,
		lambda path: {input_name: preprocess_image(path, arguments.resize, arguments.image_size, arguments.mean, arguments.std)},
	)
	quantized_model = nncf.quantize(model, dataset, subset_size=len(paths))
	validation = None
	if validation_root is not None:
		validation_paths = image_paths(validation_root)
		validation = validate_models(
			model,
			quantized_model,
			validation_paths,
			arguments.resize,
			arguments.image_size,
			arguments.mean,
			arguments.std,
		)
	output_xml.parent.mkdir(parents=True, exist_ok=True)
	ov.save_model(quantized_model, output_xml, compress_to_fp16=False)
	output_bin = output_xml.with_suffix(".bin")
	if not output_bin.is_file():
		raise RuntimeError(f"OpenVINO did not produce the expected weights file: {output_bin}")

	record_path = (arguments.record or output_xml.with_name(output_xml.name + ".build.json")).resolve()
	record = {
		"schema_version": 1,
		"source": {"path": str(source), "sha256": file_sha256(source)},
		"openvino_version": ov.__version__,
		"nncf_version": nncf.__version__,
		"quantization": "NNCF post-training INT8",
		"int8_constant_count": sum(
			1
			for operation in quantized_model.get_ops()
			if operation.get_type_name() == "Constant" and operation.get_output_element_type(0).get_type_name() in {"i8", "u8"}
		),
		"calibration": {
			"path": str(calibration_root),
			"image_count": len(paths),
			"images": [str(path.relative_to(calibration_root)) for path in paths],
		},
		"preprocess": {
			"color": "grayscale",
			"resize_short_side": arguments.resize,
			"center_crop": arguments.image_size,
			"scale": 1.0 / 255.0,
			"mean": [arguments.mean],
			"standard_deviation": [arguments.std],
			"layout": "NCHW",
		},
		"inputs": [port_description(port) for port in quantized_model.inputs],
		"outputs": [port_description(port) for port in quantized_model.outputs],
		"artifacts": {
			"xml": {"path": str(output_xml), "sha256": file_sha256(output_xml)},
			"bin": {"path": str(output_bin), "sha256": file_sha256(output_bin)},
		},
	}
	if validation is not None:
		record["validation"] = {"path": str(validation_root), **validation}
	record_path.parent.mkdir(parents=True, exist_ok=True)
	record_path.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
	print(f"Calibration images: {len(paths)}")
	print(f"INT8 OpenVINO IR: {output_xml}")
	print(f"Weights: {output_bin}")
	print(f"Build record: {record_path}")
	if validation is not None:
		print(
			"Validation: "
			f"{validation['image_count']} images, "
			f"MAE={validation['mean_absolute_error']:.6g}, "
			f"max={validation['max_absolute_error']:.6g}, "
			f"correlation={validation['pearson_correlation']:.6g}"
		)
	return 0


if __name__ == "__main__":
	sys.exit(main())