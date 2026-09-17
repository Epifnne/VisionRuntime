# Backend Plugins

The core loads vendor plugins through `backendPluginApi.h`. OpenVINO and TensorRT
implement that API directly, without wrapping the old public C++ backend classes.
Neither plugin links `visionRuntime`.

## ABI Contract

- ABI 1.0 targets the same OS, architecture and native C calling convention as
  the host. Use normal platform packing and enum sizes; short-enum flags are not
  supported. No STL, exceptions or C++ virtual objects cross the boundary.
- The sole export is `visionRuntimeBackendQuery`. Its table and ID remain valid
  until unload. The host destroys all handles before unloading the library.
- `create` receives UTF-8 artifact path, kind, device, I/O names and a JSON options
  object. On failure the handle remains null. Only the creating plugin destroys it.
- `infer` is synchronous. Input views are borrowed for that call only: contiguous
  host Float32, concrete positive dimensions, matching byte size. Both vendor
  plugins support exactly one input and one output.
- Output allocation runs synchronously on the calling thread. The host owns the
  storage. Plugins may write until `infer` returns, but never retain or free it.
  Returned tensors survive plugin destruction.
- Calls on one vendor handle are serialized internally. Destruction must not
  overlap an active call. No asynchronous completion or hot replacement is exposed.
- C entry points and host callbacks contain exceptions. Errors use status codes
  and UTF-8 bytes in a bounded caller buffer. The host reserves a trailing zero
  outside the writable capacity.

Creation fields form the ABI 1.0 baseline. The host sends the structure size for
its compile-time minor version, and plugins accept any size at or above the V1
baseline. Fields added by a future ABI minor version are read only by plugins built
for that minor; no old-options adapter exists.

## Vendor Contracts

| Backend ID | Artifact Kind | Device | Options |
| --- | --- | --- | --- |
| `openvino` | `ir`, `onnx` | `CPU`, `GPU`, `NPU` | `inferenceThreads`, default 0; `dynamicBatch`, default false |
| `tensorrt` | `engine` | Nonnegative CUDA index, e.g. `0` | `optimizationProfile`, default 0; `maxBatchSize`, default 0 (unchecked) |

I/O names must match actual model ports. TensorRT requires linear device Float32
execution tensors, not shape I/O. Input shapes must fit the selected optimization
profile and resolve the output shape before execution. Engines must match the
deployed GPU/TensorRT/CUDA environment. No ONNX conversion or device fallback.

## Dynamic Batch

Batch inference passes NCHW tensors with N > 1 through the same `infer` entry;
the ABI places no constraint on the batch dimension. The vendor contracts are:

- OpenVINO: set `"dynamicBatch": true` in the options JSON to reshape the input's
  batch dimension to dynamic before compiling. One compiled model then serves any
  N. Without the flag the model compiles as-is and batch N must match the model's
  static input shape. The flag only reshapes the input port: models whose internal
  Reshape nodes hardcode batch=1 (some PatchCore exports) fail at inference with a
  shape conflict — export such models with a natively dynamic batch dimension
  instead (see `Tools/createBatchBenchModel.py` for a generator example).
- TensorRT: build the engine with an optimization profile whose input max batch
  covers the intended batch size. Set `"maxBatchSize": N` to make `create` reject
  engines whose profile max batch is smaller than N. Dynamic-shape input per call
  is handled by `setInputShape` against the selected profile.

IR packages must contain matching weights beside the XML. The package loader
checks the primary artifact; OpenVINO validates weights when compiling the model.

## Build and Deployment

Enable independently with `VISION_BUILD_OPENVINO_PLUGIN` and
`VISION_BUILD_TENSORRT_PLUGIN`. The vendor-neutral host has no inference platform
cache variable. Windows TensorRT requires MSVC and `CUDAToolkit_ROOT`.
SDK paths are selected through `TENSORRT_ROOT` and `OpenVINO_DIR`.

Targets: `visionBackendOpenVino`, `visionBackendTensorRt`.
Output: `plugins/<backend-id>/vision-backend-<backend-id>.dll` on Windows or
`.so` on Linux. Multi-config generators add a build configuration subdirectory.

Vendor libraries are deployed beside plugins. Linux uses `$ORIGIN`, not absolute
SDK build paths. Install rules preserve the plugin layout. Hardware drivers and
system C/C++ runtimes remain prerequisites; plugins do not alter global PATH.
MSVC plugins emit PDBs into a separate `pdb/` directory and link with
`/INCREMENTAL:NO`, so `plugins/<backend-id>/` contains only runtime files.

The directory sample ships a `publish` target that assembles a self-contained
release root; the release root doubles as the model package:

```text
release/
├─ anomalyDirectorySample.exe
├─ plugins/tensorrt/vision-backend-tensorrt.dll + vendor DLLs
├─ manifest.json       # model package manifest
├─ artifacts/          # model-fp32.engine
├─ image/              # sample input images
└─ deployment.json
```

The release root is the run root: a relative `pluginDirectory` in
`deployment.json` resolves against the deployment file, the model package is
`.` itself, and the sample's hardcoded `image/` directory ships with the
release. Override the output location with
`-DANOMALY_DIRECTORY_PUBLISH_DIR=<path>` (default `<build>/release`).

## Verification

`openvinoPluginTest` and `tensorrtPluginTest` load real shared libraries and cover
missing/unsupported artifacts, port mismatch, invalid options, dynamic identity
inference and output lifetime. TensorRT also checks shape/profile bounds.

Generate fixtures in a development Python environment with OpenVINO and NumPy:

```text
python Runtime/tests/backends/createPluginTestModels.py Build/PluginTestData --tensorrt-engine <dynamicIdentity.engine>
```

The optional TensorRT identity engine must expose `image` and `score`, Float32
I/O, supporting `[1,1,2,3]` and `[2,1,4,5]` but rejecting `[2,1,5,5]`. Set
`VISION_OPENVINO_TEST_PACKAGE` and `VISION_TENSORRT_TEST_PACKAGE` to the respective
absolute package directories. Unconfigured model tests explicitly skip; loader
failure tests still execute. Python is not part of the deployed runtime.

## Integration

The old inference BuildProfile and public vendor C++ classes are removed. Presets
resolve the plugin path from deployment, select the unique model-package artifact
and construct the core plugin adapter. Applications declare deliverable plugins
with `vision_target_runtime(... BACKEND_PLUGINS ...)`; tests use the package/C ABI
path only and provide no compatibility bridge.