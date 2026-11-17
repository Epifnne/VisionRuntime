# Profile-driven anomaly inference

One executable and product Shell cover directory replay, camera acquisition,
single-source serial inference and multi-source batch inference. The historical
`anomalyDirectory` target name remains; the backend is chosen by the profile.

- `profile.json`: single-source starter, with model and preprocessing settings.
- `profile-batch.json`: two-source batch starter, TensorRT stream/dynamic-batch
  tuning, and a 20-second duration. Set duration to 0 for no duration limit.

```powershell
.\Build\YourBuild\bin\anomalyDirectorySample.exe .\Samples\anomalyDirectory\profile.json
.\Build\YourBuild\bin\anomalyDirectorySample.exe .\Samples\anomalyDirectory\profile-batch.json
```

One source and `maxBatchSize` omitted or 1 use the serial runtime. Multiple
sources or `maxBatchSize > 1` use `MultiCameraSession` and the batch executor.
Batch size and flush timeout are configured in `pipeline`; the model must
support the requested batch shapes. All anomaly inputs must be Gray8.

For TensorRT dynamic batching, export a compatible model and build an engine
with matching min/opt/max batch profiles. Configure `model.backendId:"tensorrt"`,
`backend.dynamicBatch:true`, and optionally `backend.streams`. Other backends
are selected through `model.backendId` and a compatible model artifact.

Models and large replay datasets are not bundled. The single-source starter
expects `model` and `image`; the batch starter expects `model-batch` and
`image/camera1`, `image/camera2`, all relative to its profile.

```bat
Samples\buildShell.cmd anomalyDirectory
```

The design kit contains `profiles/anomalyDirectory.json` and
`profiles/anomalyDirectory-batch.json`, both using the same CLI and Shell.
Copy the chosen model/images beside the profiles and set the target backend,
device and camera IPs before execution. Build real camera support with
`-DVISION_CAMERA_SDK=HIK_MVS`.

The removed `anomalyTensorRtMulti` product is represented by the batch profile.
See [the common guide](../README.md) for shared source, UI and publishing rules.
