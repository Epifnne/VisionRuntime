import argparse
import json
from pathlib import Path
import shutil

import numpy as np
import openvino as ov
from openvino import opset13 as ops


def write_manifest(root, backend, kind, device, filename):
    manifest = {
        "schemaVersion": {"major": 1, "minor": 0},
        "id": "dynamic-identity",
        "version": "1.0.0",
        "inputs": [{"name": "image", "elementType": "float32",
                    "layout": "nchw", "shape": [1, 1, 2, 3]}],
        "outputs": [{"name": "score", "elementType": "float32",
                     "layout": "nchw", "shape": [1, 1, 2, 3]}],
        "artifacts": [{"id": backend, "backendId": backend, "kind": kind,
                       "path": "artifacts/" + filename, "devices": [device], "options": {}}],
    }
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--tensorrt-engine", type=Path)
    args = parser.parse_args()
    root = args.output / "openvino"
    (root / "artifacts").mkdir(parents=True, exist_ok=True)
    parameter = ops.parameter(ov.PartialShape([ov.Dimension(1, 2), 1,
                                              ov.Dimension(1, 4), ov.Dimension(1, 5)]), np.float32)
    parameter.output(0).get_tensor().set_names({"image"})
    output = ops.add(parameter, ops.constant(np.float32(0)))
    output.output(0).get_tensor().set_names({"score"})
    ov.save_model(ov.Model([output], [parameter]), root / "artifacts/model.xml",
                  compress_to_fp16=False)
    write_manifest(root, "openvino", "ir", "CPU", "model.xml")
    if args.tensorrt_engine:
        root = args.output / "tensorrt"
        (root / "artifacts").mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.tensorrt_engine, root / "artifacts/model.engine")
        write_manifest(root, "tensorrt", "engine", "0", "model.engine")


if __name__ == "__main__":
    main()