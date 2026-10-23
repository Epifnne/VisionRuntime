# Event-driven, multi-view inspection

This shared CLI/Shell product simulates an infrared sensor and two cameras at different
positions on a conveyor. Sensor input, edge filtering, workpiece ID assignment,
and conveyor scheduling belong to **this sample/business project**, not Runtime.
No camera SDK, PLC, model, or image files are needed.

## Run

Double-click `Samples/eventInspection/build.cmd`, or run:

```powershell
.\Samples\buildShell.cmd eventInspection -DBUILD_TESTING=OFF
.\Publish\release\run-cli.cmd
.\Publish\release\run-cli.cmd --missing-view
.\Publish\release\design.cmd
```

The selected product replaces the design kit in root `Publish/release`, beside
Build, not inside it. CLI and Shell both call the same `createApplication()`;
neither launches the other process. Do not run both against the same real camera.
In Designer, use this product's profile/manifest to bind both `stream.*` views,
`workpiece.result` fields and `session.*` controls, then publish the final GUI.
The starter profile already provides these bindings. Manifest generation links
only the pure business contract and does not open cameras or run sensor code.

Four simulated workpieces enter 25 ms apart. Top/side captures are requested at
10/70 ms after each sensor edge, with a 200 ms capture tolerance and a 1 s
whole-inspection deadline. Multiple workpieces are in flight at once.

Normal output: four `workpiece.result` JSON snapshots, with final counts
**2 OK, 2 NG, 0 FAULT**. `--missing-view` deliberately omits the final side
capture; final counts become **1 OK, 2 NG, 1 FAULT**. Both are acceptance scenarios
and exit zero when the exact expected counts are observed.

To reuse an existing build tree instead of creating another:

```powershell
$env:VISION_SHELL_BUILD_DIR = "$PWD\Build\SampleShell\businessExports"
.\Samples\buildShell.cmd eventInspection -DBUILD_TESTING=OFF
```

The script detects MSVC/Qt and publishes required runtime dependencies.
The optional `publishEventInspectionSample` target also copies a CLI-only example
and its profile to root `Publish/examples/eventInspection`; invoke its `run.cmd`
to use the colocated profile.

## Framework API and identities

1. Create one `camera::EventTriggerSource` per physical `ICameraDevice`.
2. Configure `runtime::EventInspectionSession::addRoute<T>()` for each view.
   The view ID identifies its camera/pipeline route.
3. On a business event, call `beginInspection(workpieceId, requiredViews, timeout)`.
   Runtime assigns an inspection ID.
4. Business code schedules its own delays. When a view is due, call
   `requestCapture(inspectionId, viewId, optionalCaptureDeadline)`.
   Runtime assigns a capture ID and enqueues the request without waiting for a
   camera or inference result. The optional absolute deadline bounds queueing
   plus camera response, and is capped by the whole-inspection deadline.
5. Observe the immutable `InspectionResult`. It contains all required views and
   their identities/statuses. Completion is per inspection, not per frame count.

`FrameMetadata::capture` carries `workpieceId`, `inspectionId`, `captureId`, and
`viewId` through `PipelinePacket::identity()`, including after frame release.
Camera sequence/hardware timestamps and the executor's `executionId` retain their
existing meanings; no identity is guessed from sequence numbers or arrival time.
IDs are process-local, not durable PLC transaction IDs.

## Different pipeline result types

The top pipeline returns `Brightness`; the side pipeline returns `Edges`.
Both use real OpenCV operations. `addRoute<T>()` keeps each concrete result type
and accepts:

- the original `IVisionPipeline<T>` (including model or OpenCV pipelines);
- an explicit `T -> Result<ViewDecision>` mapping for aggregation;
- an optional typed observer for the original result and frame identity.

Only `OK/NG` is normalized. A failed pipeline/adapter is a **FAULT**, not NG or OK.
Each route has an independent serial executor, so a slow model does not block
other camera routes. Do not share an unprotected mutable pipeline between routes.
This is fixed camera/view routing, not a general DAG scheduler.

## Conservative completion and recovery

- Every required view must return. Any NG yields NG only after all required
  views finish; any error yields FAULT, with error taking precedence over NG.
- Missing/untriggered views and overdue pipeline results become FAULT when the
  inspection deadline expires, without requiring another incoming event.
- Duplicate captures and unknown/unplanned views are rejected explicitly.
- Active inspections, acquisition queues, executor queues and result delivery
  are bounded. A full queue is rejected, never silently dropped or blocked
  indefinitely at the sensor entry point.
- Each camera has at most one outstanding software trigger. Expired requests
  still waiting in its queue are not triggered.
  Do not call the raw device's `softwareTrigger()` alongside this source.
- Once an issued trigger times out or the camera reports an error/unsolicited
  response, that source faults permanently. Accepted queued requests fail too.
  Recreate the source/session and re-arm the hardware before resuming; never
  reuse a possibly late image for the next workpiece.
- Cameras must deliver exactly one response per accepted trigger. A vendor that
  can emit delayed duplicate frames during later triggers needs hardware trigger
  IDs/chunk correlation in its device adapter; generic untagged frames cannot
  distinguish that case.
- Inspection timeouts do not interrupt a running model call. Late completions
  cannot change an already published decision. `wait()` still drains execution.
- Result observers are serialized. Keep them short; they may request stop, but
  must not call `wait()` or destroy the session. Typed per-view observers are
  raw execution telemetry and can run concurrently or after aggregate timeout;
  only the final inspection callback is an authoritative release decision.
- Shutdown cancels incomplete inspections and publishes FAULT rather than OK.
- `wait()` does not stop the session: request stop explicitly or let business
  completion/terminal acquisition failure request it. Graceful stop drains
  accepted execution; Immediate additionally requests executor cancellation.
  Both stop modes fault unfinished inspections and stop acquisition.
- `addObservers()` adds frame, neutral per-view outcome and source-fault
  observers without replacing typed business callbacks. Register before start.
  `currentSummary()` exposes frame counts and separate started/in-flight/
  completed/OK/NG/FAULT workpiece counts. A successfully executed NG is a
  completed frame, not an execution failure.

Conveyor delay triggering is not hard real-time and assumes known stable motion.
Variable speed, belt stops/slip, close spacing, or multiple entrance sensors
require a business-specific encoder/position tracker and rejection policy.

## Service, business exports and Designer

The sample binds `view.top`, `view.side`, and `workpiece.result` through
its pure `BusinessContract` declarations, and subscribes to immutable
summary snapshots. Raw states include their workpiece/inspection/capture IDs.
`Summary.views` is a serialized detail field because exported schemas currently
support flat scalar fields.

Export `workpiece.result`, not an individual view's latest raw state, as the
authoritative UI/PLC result.

`BusinessSession` implements `IInspectionSession`, owns the sensor scheduler and
the Runtime adapter returned by `adoptEventInspectionSession()`, and forwards
lifecycle, statistics, observers and camera-device access. `isRunning()` remains
true while waiting for an external event; event sources do not pretend to be
continuous `IFrameSource`s. Service's `session.start/stop/recover`, image streams,
`inspection.result`, `source.fault` and frame statistics therefore work unchanged.
Service source indices are contiguous in sorted view-ID order; the original
configured `capture.viewId` is preserved even for sparse IDs.

Exposure/gain access remains available. `camera.*.softwareTrigger` explicitly
returns Unsupported for event-owned cameras: an uncorrelated GUI trigger could
consume an image belonging to another workpiece. Business capture commands must
create/reference an inspection and call `requestCapture()`.

Profiles may use camera mode `eventTrigger` only with a custom `runtimeProduct`.
The default assembler does not infer event schedules or routes from JSON.
Existing directory/continuous/timed-trigger behavior remains supported.
`session.recover` reconstructs the business application, discarding timers,
pending work and counters, as required for restart-from-empty operation.
This demonstration deliberately finishes after four workpieces; production sensor
listeners normally remain running until the operator stops or a fault occurs.

## PLC closed loop

The final inspection callback is the result outlet, **not a PLC protocol**:

```text
sensor/PLC request -> workpiece identity -> delayed camera captures
 -> per-view pipelines -> final OK/NG/FAULT
 -> business PLC adapter writes result + transaction ID + valid
 -> PLC acknowledges that ID -> release/reject action -> reset/ready
```

PLC communication must add acknowledgment timeouts, reconnect/resend rules,
deduplication and a safe fault action. Do not assume a successful write means
the PLC executed a reject action, or map missing/FAULT results to OK.
No PLC transport, register map, or safety-rated control is implemented here.
