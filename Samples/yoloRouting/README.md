# YOLO routing sample

A model-free example of a custom VisionRuntime operator:

```text
FileSource -> RuntimeSession -> SerialPipelineExecutor -> OpenCvPipeline
  -> copy original image
  -> mockYoloDetect
  -> route boxes
       Object: collect detector class for the classification callback
       Defect: collect defect metadata and draw a red box
       Text: crop original ROI and call recognizeText
  -> InspectionResult -> completion callback -> annotated PNG
```

The detector emits three synthetic boxes on every image, regardless of its
content. Object classification is the mock detector's class, not a second
classification model. This sample does not load a YOLO model, run NMS, or measure
model accuracy. `recognizeText` is deliberately a placeholder returning
`Unsupported`, never a fabricated successful OCR result.

## Build and run

No camera SDK, inference plugin, model, or Qt installation is required.
Use an MSVC x64 developer PowerShell with CMake and Ninja on PATH:

```powershell
cmake -S Samples\yoloRouting -B Build\YoloRouting -G Ninja `
  -DCMAKE_BUILD_TYPE=Release
cmake --build Build\YoloRouting --target yoloRoutingSample yoloRoutingTest --parallel 4
ctest --test-dir Build\YoloRouting -R "^yoloRouting\." --output-on-failure
& .\Build\YoloRouting\bin\yoloRoutingSample.exe `
  .\Samples\anomalyDirectory\image .\Build\YoloRouting\annotations
```

Alternatively, enable `VISION_BUILD_SAMPLES=ON` in a root build and build the
same targets. Executables are under `bin` in the build directory.
TensorRT is only needed for the separate TensorRT sample.
For multi-configuration generators, add `--config Release` to the build,
`-C Release` to CTest, and use the executable under `Release`.

Arguments are the input image directory and output annotation directory.
Every input image produces `frame-<taskId>.png`. Green boxes indicate objects,
red boxes defects, and blue boxes text regions. The console prints object
classification callbacks, defect coordinates and OCR placeholder statuses.
PNG files are replaced if the same task IDs already exist in the output directory.
Keep input and output directories separate.

The process exits nonzero on source/pipeline/output errors or if no frames
completed. An OCR failure is kept explicitly in its text region; the other
regions still complete. Therefore the expected placeholder `Unsupported` does
not make the whole sample fail.

## Extension points

- Replace [mockYoloDetect](routingAlgorithm.cpp) through the `Detector` callback.
  It must return boxes in original-image pixel coordinates, labels, confidence
  in `[0, 1]`, and `Object`, `Defect`, or `Text` routing kinds. A real YOLO
  implementation must perform its own preprocessing, inference, output decode,
  NMS and inverse coordinate transformation, reusing a model Pipeline as needed.
- Replace [recognizeText](routingAlgorithm.cpp) through the `OcrRecognizer`
  callback. It receives an unannotated ROI from the original image, synchronously.
  The ROI is a potentially non-contiguous view, valid during the call. Clone it
  if the OCR implementation needs contiguous storage or asynchronous ownership.
  Its result remains associated with the box in original-image coordinates.
- Extend the object branch with a classification model if finer classification
  is required; the current [onClassification](yoloRoutingSample.cpp) only
  delivers the detector's object labels from the completion thread.
- [RoutingAlgorithm](routingAlgorithm.hpp) implements `IOpenCvAlgorithm` and
  is registered by `OpenCvPipelineBuilder`. This uses the existing single-stage
  extension interface rather than introducing fake inference tensors.
  All routing/OCR work runs serially, not as an automatically scheduled DAG.
- `InspectionResult` aggregates all three result types and owns its annotated
  image. The callback can retain the image safely after the packet is released.
  Read-only camera buffers are not modified; strided Gray8/BGR8/BGRA8 images
  are supported. Invalid boxes are rejected; partially outside boxes are clipped.

The tests cover routing, exact ROI clipping, explicit OCR failure, source
immutability, annotation ownership, pixel formats/strides, invalid inputs,
one-pixel images and asynchronous completion.
