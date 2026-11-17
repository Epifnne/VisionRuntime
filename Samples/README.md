# Profile-driven business samples

Every business sample has a `profile.json` shared by its CLI and product Shell:

| Sample | C++ pipeline | Product-specific profile fields |
| --- | --- | --- |
| anomalyDirectory | Single-source preprocess / backend / anomaly threshold | `model`, `pipeline` |
| anomalyHikMvs | Single-source anomaly pipeline | `model`, `pipeline` |
| anomalyHikMvsMulti | Multi-source shared batch pipeline | `model`, `pipeline`, `durationMilliseconds` |
| anomalyTensorRtMulti | Multi-source batch pipeline, TensorRT tuning | `model`, `pipeline`, `backend`, `durationMilliseconds` |
| businessExports | OpenCV mean brightness | `measurement.minimumBrightness` |
| eventInspection | Event-driven top brightness and side edge routes | `inspection`, `simulation` (business sensor schedule) |
| yoloRouting | Synthetic detections / object-defect-text routing / placeholder OCR | `routing.minimumConfidence`, `routing.annotationDirectory` |
| openCvCuda | GPU threshold and bright-fraction measurement | `cuda.device`, `cuda.threshold`, `cuda.minimumBrightFraction` |

Pipeline operators and connections remain C++; profiles do not load arbitrary
code or dynamically define a graph. The local experimental A77 DAG acceptance
tool is not part of this business-product interface.

## CLI and GUI

From the repository root, configure with `VISION_BUILD_SAMPLES=ON` (also enables
Service), build `<sample>Sample`, then run:

```powershell
.\Build\YourBuild\bin\businessExportsSample.exe .\Samples\businessExports\profile.json
.\Build\YourBuild\bin\yoloRoutingSample.exe .\Samples\yoloRouting\profile.json
```

All eight business CLIs accept `<profile.json>`, not positional camera IPs,
input directories, model packages or deployment files. The explicit CUDA
`--self-test [device-index]` and event simulation acceptance options remain.
`eventInspectionSample --verify` checks the original synthetic 2 OK / 2 NG
scenario; normal execution accepts profile-defined algorithm decisions.

For GUI, use `Samples\buildShell.cmd <sample>` on Windows or
`sh Samples/buildShell.sh <sample>` on Linux. Both publish the dedicated Shell,
CLI, Designer and starter profile under `Publish/release`. Configure hardware
requirements with additional CMake arguments. Do not run CLI and Shell against
the same physical camera simultaneously.

## Sources and paths

Keep a source's `id` when switching its type so UI `stream.<id>` bindings remain
valid. Example directory and camera entries:

```json
{"id":"files","type":"directory","directory":"images","extensions":[".pgm"],"loop":false}
```

```json
{"id":"files","type":"camera","ipAddress":"192.168.1.10","pixelFormat":"gray8","mode":"continuous"}
```

Camera sources require a build with `VISION_CAMERA_SDK=HIK_MVS`; directory replay
does not. `timedTrigger` cameras also accept `triggerIntervalMilliseconds` and
`responseTimeoutMilliseconds`. Exposure, gain, frame rate and buffer limits use
the existing Service profile schema. Single-source products require exactly one
entry; batch products accept multiple entries. Anomaly, brightness, event and
CUDA pipelines require Gray8; YOLO also supports BGR8/BGRA8.

Relative directories, model/plugin locations and annotation outputs resolve
against the profile's directory, including when launched from another working
directory. Set paths for the actual deployment; no installed model or backend
location is guessed. Missing files, unsupported SDKs and invalid parameters
fail explicitly. No simulated or CPU fallback replaces requested hardware.
YOLO output and input directories must differ; `frame-<sequence>.png` is
overwritten on a subsequent run.

For event inspection, keep source IDs `top` and `side`. A camera with
`vendor:"simulated"` explicitly requests the original synthetic camera.
Remove that vendor and set `ipAddress`, `mode:"eventTrigger"` for a real camera.
A directory source instead supplies one image per requested capture; use
`loop:true` to replay a short sequence across the four scheduled workpieces.
Finite/exhausted replay faults explicitly. The example sensor schedule remains
business-owned; configuring cameras does not implement a real PLC/infrared sensor.
`inspection.minimumBrightness` and `inspection.expectedTransitions` configure
the two route decisions. Edge measurement compares the first two image columns.

`ui.pages` configures layout and endpoint bindings without changing C++.
Declaration-based products generate their custom endpoint manifests offline.
Anomaly templates expose the shared Service endpoints.

## Hardware and tests

Vendor anomaly inference requires the selected backend plugin and a compatible
model. CUDA additionally requires matching contrib sources, Toolkit, driver and
`VISION_WITH_OPENCV_CUDA=ON` with `VISION_OPENCV_CUDA_ARCH_BIN` explicitly set.
See the [CUDA guide](openCvCuda/README.md). CUDA is not silently tested on CPU.

CTest `profile.*` tests replay all seven CPU-capable products from profile-relative
temporary directories, exercise configured algorithms and missing-input errors,
and use the existing fake backend only in anomaly tests. CI also builds/publishes
the YOLO product Shell. Real camera and GPU validation remain hardware-specific.
