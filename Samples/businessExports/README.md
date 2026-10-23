# Declarative business exports (CLI + product Shell)

This model-free example uses OpenCV to measure mean brightness. Its CLI and
product Shell link the **same** `visionProductApplication` business library.
There is no IPC, source-code scanning, second runtime, or second camera owner.

## One-click workflow (recommended)

Double-click `build.cmd` in this directory, or run:

```powershell
.\Samples\buildShell.cmd businessExports
```

The shared script detects a Qt6 MSVC kit in common install locations (or uses
`VISION_SHELL_QT_DIR`), discovers MSVC, builds Release, and stages a complete kit
before replacing `Publish\release`. For custom installations, use the environment
overrides documented by `Samples\buildShell.cmd --help`.
The script uses UTF-8 compiler output and prefers Visual Studio's bundled CMake
when available so localized MSVC header dependencies are tracked consistently.
After changing public C++
interfaces, do not reuse objects from a build whose Ninja dependency records
are missing; rebuild the affected targets cleanly.
For an affected existing build tree, clean it first and rerun the shared script
with `--fresh` to regenerate the compiler metadata as well.

The kit contains:

```text
Publish\release\
  design.cmd                              Open Designer with this product
  run-cli.cmd                             Run the same business from a console
  profiles\businessExports.json            Starter UI/configuration
  profiles\businessExports.manifest.json   Automatically generated bindings
  profiles\images\                        Bundled synthetic replay images
  runtimeTemplates\windows\                Product Shell, CLI and dependencies
  visionDesigner\                         Designer and dependencies
```

Open `design.cmd`: the manifest is discovered automatically, and
`measure.result` fields are immediately available for control binding. No
manual export/import and no business startup are needed. Edit the UI and publish
the final product through Designer. `run-cli.cmd` can validate the bundled
algorithm immediately. Do not store your own projects in the replaceable kit.

## Build and run directly

Configure the repository with `VISION_BUILD_SERVICE=ON` and
`VISION_BUILD_SAMPLES=ON`, then build `businessExportsSample`:

```powershell
.\Build\MSVC-2026\bin\businessExportsSample.exe .\Samples\businessExports\profile.json
```

Normal builds automatically generate `bin\businessExports.manifest.json`.
The dedicated generator links only the declaration library, not the business
implementation. It reads profile metadata but does not open image directories,
initialize cameras, load models, construct algorithms, or start a session.

The bundled Gray8 images produce mean brightness values **20** and **200**.
At the default minimum of 100 the decisions are respectively false and true.

For the product Shell, configure the same repository with:

```powershell
cmake --preset windows-msvc-2026 `
  -DVISION_BUILD_SERVICE=ON -DVISION_BUILD_SHELL=ON `
  -DVISION_PRODUCT_SAMPLE= `
  -DVISION_PRODUCT_APPLICATION="E:\Work\VisionRuntime\Samples\businessExports"
cmake --build --preset windows-msvc-2026 --target visionShell businessExportsSample
.\Build\MSVC-2026\bin\visionShell.exe .\Samples\businessExports\profile.json
```

Use your checkout's absolute sample path for `VISION_PRODUCT_APPLICATION`.
Backend plugins are not required by this product. `session.recover` recreates
the finite directory runtime before another run.

In Designer, open the profile. The build-directory Designer discovers the
manifest next to its executable; packaged profiles discover their sibling
manifest. Bind a
`statNumber` to `measure.result`; the field selector offers `brightness`,
`accepted`, `sequence`, `valid`, and `error`. Unknown declared-schema fields
fail validation. `resultTable` displays the complete result.

Designer preview remains a simulation, not an execution of business code.
Custom result fields are shown as null with `simulation: true`; custom commands
report that they cannot execute in preview. Real binding runs in the product
Shell. The optional CLI/Shell `--export-manifest` command is also offline now,
but is unnecessary during the normal build/design workflow.

## Implementing another product

1. Put pure declarations in a `visionProductContract` library, separate from
   the algorithm/device implementation in `visionProductApplication`.
2. Implement `describeApplication(profile)` and `createApplication(profile)` from
   [productEntry.hpp](../../Service/include/exports/productEntry.hpp).
   The first must only describe the contract; the second binds its definitions
   to live business callbacks and returns an unstarted session.
3. Set `VISION_PRODUCT_ID` and call:

   ```cmake
   vision_export_product(visionProductApplication
       CONTRACT visionProductContract
       PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/profile.json"
       CLI myInspectionApp)
   ```

   The normal CLI/Shell build now depends on manifest generation. Changes to
   declarations or the profile regenerate it, and generation errors fail the
   build instead of publishing a stale interface.
4. Use `adoptProductRuntime<MyResult>()` to adopt a standard single- or
   multi-camera Runtime session without converting `MyResult` to `AnomalyResult`.
5. Give the profile a matching `runtimeProduct`. A model-free custom product
   may omit `model`; the generic anomaly host still requires it.

Example pure declaration (see `businessContract.cpp`):

```cpp
visionService::exports::StateSchema<MyResult> schema;
schema.field("width", &MyResult::width, "measured width", "mm")
      .field("accepted", &MyResult::accepted);

auto measurement = contract.state(
    {.name = "measure.result"}, std::move(schema));
```

Runtime binding uses that same definition, without repeating field metadata:

```cpp
auto result = measurement.bind(application.exports, MyResult{});
if (!result) {
    return core::Result<visionService::ProductApplication>::failure(result.status());
}
// Capture result.value() in the business observer and check state->set(value).
```

The same schema serializes snapshots and produces manifest field metadata.
Set `application.expectedManifest` from the pure description: Service checks
the actual runtime endpoints against it before allowing startup.
Only declared fields are exported. State fields currently support flat scalar
numbers, bool and `std::string`; nested objects/arrays and parameterized commands
are not part of this initial API. Use strings for identifiers requiring exact
64-bit representation in JavaScript.

- `parameter<T>(descriptor, read, write)` derives the scalar type and writability.
  Supported parameter types: `double`, `std::int64_t`, `bool`, `std::string`.
  Existing `ParameterEndpoint` declarations also work.
- `command(endpoint)` binds an existing `Result<void>()` callback.
- `state(descriptor, schema, initial)` returns a thread-safe state handle.
  `set(value)` stores a local snapshot before attachment (useful for headless
  business code); after registration it also publishes immutable updates.
- `stream(endpoint)` returns a handle for publishing owned `Frame` values.
  Profile source streams are additionally registered automatically.

Service registers the declarations before applying profile endpoint exposure.
Duplicate names, malformed schemas and failed registration are errors, not
silent omissions. Registration rolls back its own partial additions. Stop and
recovery disconnect old state/stream handles; they cannot publish into a new
product instance with the same endpoint names.

Optional target properties:

- `VISION_PRODUCT_BACKEND`: the backend plugin included in a design kit; absent
  for a model-free application.
- `VISION_PRODUCT_PROFILE_ASSETS`: explicitly selected directories copied next
  to the starter profile (their basenames must match its relative paths).

The generator requires a native build. Cross-compilation currently reports an
explicit error rather than trying to execute a target-platform binary.

Handlers must capture owned/shared business state, not stack references.
Parameter and command handlers execute on the caller's thread: use atomics,
locks or the business control queue as appropriate. State/stream delivery uses
the existing bounded dispatcher and its explicit error/backpressure semantics.
Subscriptions are monitoring, not a durable PLC result channel.

Service preserves lifecycle, frame/fault observation and generic outcome
metadata/counters. Custom domain values travel through declared state exports;
the legacy anomaly-specific `VisionService::resultObserver` is only used by
the legacy runtime factory/default anomaly assembly.
