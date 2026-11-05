# OpenCV CUDA inspection (no model)

This optional NVIDIA GPU sample replaces model inference with an
`IOpenCvAlgorithm<InspectionResult>` in the existing `OpenCvPipeline` and Runtime.
It uploads Gray8 frames, thresholds pixels strictly above 127 on the GPU, counts
nonzero pixels on the GPU, and accepts frames whose bright fraction is at least
0.5. Only the count returns to the CPU. No inference backend or model is required.

## Prerequisites

- NVIDIA GPU and compatible driver, CUDA Toolkit including NPP.
- MSVC on Windows, or a CUDA-supported GCC toolchain on Linux. MinGW is rejected.
- OpenCV contrib **4.14.0**, matching the bundled OpenCV; do not use contrib HEAD.
- Explicit GPU compute capability, for example **8.6** for RTX 3060 Laptop.

CUDA support defaults to OFF. Existing CPU products keep their current behavior.
When enabled, configuration fails if CUDA modules are unavailable; initialization
fails for a missing driver, invalid device, or incompatible GPU. There is no
automatic CPU fallback. This does not enable OpenCV DNN or replace TensorRT.

## Build on Windows

Run in an x64 MSVC developer terminal, from the repository root:

```powershell
chcp 65001
cmake "-DGIT_EXECUTABLE=$((Get-Command git).Source)" `
  "-DTHIRDPARTY_DIRECTORY=$PWD\Thirdparty" -P cmake\bootstrapDependencies.cmake
cmake -S . -B Build\OpenCvCuda -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build Build\OpenCvCuda --target bootstrapOpenCvCudaDependencies
cmake -S . -B Build\OpenCvCuda -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DVISION_WITH_OPENCV_CUDA=ON `
  -DVISION_OPENCV_CUDA_ARCH_BIN=8.6 `
  -DCUDAToolkit_ROOT=D:\cuda `
  -DVISION_BUILD_SAMPLES=ON
cmake --build Build\OpenCvCuda --target openCvCudaSample openCvCudaTest --parallel 4
```

For multiple deployed architectures, quote the list:
`"-DVISION_OPENCV_CUDA_ARCH_BIN=8.6;8.9"`. Only request architectures supported by
your Toolkit. Use a separate build directory from CPU-only products.
Enabling CUDA changes OpenCV core for the whole build tree, so even CPU-only
algorithms in that tree can require CUDA runtime DLLs.

If Git downloads are unavailable, extract the archive for the pinned contrib
commit `a8e9acd62cabd30419dba83007f2ac0d07de5e2c` into
`Thirdparty\opencv_contrib\4.14.0`, or set `VISION_OPENCV_CONTRIB_ROOT` to your
matching checkout. The bootstrap target verifies Git checkouts at that commit.
For a fully manual setup, also extract OpenCV commit
`0654a42e19215ef25b1d367d822f3c630447e7c7` into
`Thirdparty\opencv\4.14.0` and skip the bootstrap commands for the dependencies
you already supplied as archives. Do not mix 4.12 headers with 4.14 libraries;
reconfigure and rebuild existing products after the dependency upgrade.

## Run and verify

CUDA's runtime DLL directory must be on PATH. Recent Toolkits can place DLLs in
`bin\x64` rather than `bin`; use the directory containing the NPP DLLs:

```powershell
$env:PATH = "D:\cuda\bin\x64;D:\cuda\bin;$env:PATH"
.\Build\OpenCvCuda\bin\openCvCudaSample.exe --self-test
.\Build\OpenCvCuda\bin\openCvCudaSample.exe E:\images\gray8 0
ctest --test-dir Build\OpenCvCuda -L opencv-cuda --output-on-failure
```

The self-test compares an actual GPU result with a CPU reference, including the
127/128 threshold boundary. Tests additionally cover padded/read-only input,
changing sizes, invalid configuration, unsupported formats, and calls from
multiple host threads. Hardware tests require a usable GPU and fail, not skip,
when it is unavailable. The directory command runs the normal asynchronous
Runtime and returns a nonzero exit code for source/processing failures or no
completed frames. Directory images must be 8-bit grayscale, not BGR or Gray16.

For a standalone sample build, use `-S Samples\openCvCuda` with the same CUDA
options. Executable output follows the selected OpenCV/CMake generator layout;
multi-config builds require `--config Release` and `ctest -C Release`.

## Reuse in your product

Link the opt-in `Vision::OpenCvCuda` interface target from the business algorithm
library. It supplies headers and static libraries for `cudaarithm`, `cudaimgproc`,
`cudafilters`, `cudawarping`, and `cudev`. Implement `IOpenCvAlgorithm<MyResult>`,
then assemble it with `OpenCvPipelineBuilder` and `RuntimeFactory`, as this sample
does. No change to the vendor model plugin ABI is needed.

The sample reuses GPU buffers, selects the configured CUDA device on every worker
thread, and serializes calls sharing those buffers. Operations are blocking:
borrowed camera storage is no longer in use when `process()` returns. If adding
streams, ensure all work finishes before returning, including error paths.
Do not pass device-only frame memory to a host `cv::Mat`.

This is not camera-to-GPU zero copy and makes no performance guarantee. Measure
upload plus processing latency on real images before choosing GPU over CPU.
For deployment, ship the CUDA/NPP runtime libraries required by your linked
OpenCV modules under NVIDIA's redistribution terms, plus the MSVC runtime.
This sample does not bundle CUDA DLLs or install drivers.

OpenCV and contrib 4.14.0 include the CUDA 13 device API and CCCL namespace
updates missing from 4.12.0. MSVC CUDA builds enable the conforming host
preprocessor required by recent CCCL headers.
