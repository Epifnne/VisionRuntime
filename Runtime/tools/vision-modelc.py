#!/usr/bin/env python3
"""Convert an ONNX model to OpenVINO IR and record reproducibility metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any


def file_sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def port_description(port: Any) -> dict[str, Any]:
	try:
		name = port.get_any_name()
	except RuntimeError:
		name = ""
	return {
		"name": name,
		"element_type": port.get_element_type().get_type_name(),
		"partial_shape": str(port.get_partial_shape()),
	}


def parse_arguments() -> argparse.Namespace:
	parser = argparse.ArgumentParser(
		prog="vision-modelc",
		description="Convert an ONNX model to OpenVINO IR (.xml + .bin).",
	)
	parser.add_argument("model", type=Path, help="source ONNX model")
	parser.add_argument("-o", "--output", type=Path, required=True, help="output IR .xml path")
	parser.add_argument("--input-name", help="required model input tensor name")
	parser.add_argument("--output-name", help="required model output tensor name")
	parser.add_argument("--compress-to-fp16", action="store_true", help="compress floating-point constants to FP16")
	parser.add_argument("--record", type=Path, help="build record path (default: <output>.build.json)")
	return parser.parse_args()


def require_port(ports: list[dict[str, Any]], expected: str | None, kind: str) -> None:
	if expected is None:
		return
	names = {port["name"] for port in ports}
	if expected not in names:
		available = ", ".join(sorted(name for name in names if name)) or "<unnamed>"
		raise ValueError(f"{kind} tensor '{expected}' was not found; available: {available}")


def main() -> int:
	arguments = parse_arguments()
	source = arguments.model.resolve()
	output_xml = arguments.output.resolve()
	if source.suffix.lower() != ".onnx" or not source.is_file():
		raise ValueError(f"ONNX model was not found: {source}")
	if output_xml.suffix.lower() != ".xml":
		raise ValueError("output path must have the .xml extension")
	try:
		import openvino as ov
	except ImportError as error:
		raise RuntimeError("OpenVINO Python is required; install it with 'python -m pip install openvino'") from error
	model = ov.convert_model(source)
	inputs = [port_description(port) for port in model.inputs]
	outputs = [port_description(port) for port in model.outputs]
	require_port(inputs, arguments.input_name, "input")
	require_port(outputs, arguments.output_name, "output")
	output_xml.parent.mkdir(parents=True, exist_ok=True)
	ov.save_model(model, output_xml, compress_to_fp16=arguments.compress_to_fp16)
	output_bin = output_xml.with_suffix(".bin")
	if not output_bin.is_file():
		raise RuntimeError(f"OpenVINO did not produce the expected weights file: {output_bin}")
	record_path = (arguments.record or output_xml.with_suffix(".build.json")).resolve()
	record_path.parent.mkdir(parents=True, exist_ok=True)
	record = {"schema_version": 1, "source": {"path": str(source), "sha256": file_sha256(source)}, "openvino_version": ov.__version__, "compress_to_fp16": arguments.compress_to_fp16, "inputs": inputs, "outputs": outputs, "artifacts": {"xml": {"path": str(output_xml), "sha256": file_sha256(output_xml)}, "bin": {"path": str(output_bin), "sha256": file_sha256(output_bin)}}}
	record_path.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
	print(f"OpenVINO IR: {output_xml}")
	print(f"Weights: {output_bin}")
	return 0


if __name__ == "__main__":
	sys.exit(main())