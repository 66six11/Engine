# Architecture: Editor And Runtime Boundaries

## Purpose

This document describes current boundaries between the C++ runtime, native ImGui editor host, shared native bridge, and Avalonia Studio shell. It answers who owns editor state, who owns GPU/runtime state, and how managed UI code crosses into native rendering.

It does not describe panel layout details or single-feature implementation plans. Future work is labeled with `future`.

## Layers

| Layer | Current paths | Current responsibility |
|---|---|---|
| Runtime packages | `engine/*`, `packages/*` | Engine data structures, rendering backend, asset/model packages, scene/resource state. |
| Native editor executable | `apps/editor/src` | ImGui host, panel registration, actions, shortcuts, asset browser, frame debugger, viewport runtime, editor smokes. |
| Native bridge library | `apps/editor/src/native_bridge`, `editor-native` target | C ABI exported by the native runtime for Studio viewport and frame debugger interop. |
| Managed Studio shell | `apps/studio/Core`, `apps/studio/Shell`, `apps/studio/Features` | Avalonia shell, command/dock/panel models, viewport composition models, managed interop adapters. |
| Managed tests | `apps/studio/Tests` | Studio architecture, model, interop, feature, and XAML tests. |

## State Owners

| State | Owner | Access direction |
|---|---|---|
| Vulkan context, frame loop, shared image handles, present packets | native runtime in `apps/editor` plus `packages/rhi-vulkan` | Studio requests snapshots or packets through C ABI; it does not own native GPU objects. |
| Native viewport producer and packet lifetime | `editor-native` implementation | Studio must release acquired present packets through the bridge. |
| Frame debugger capture state and JSON snapshot buffer | `apps/editor` native frame debugger bridge | Studio copies UTF-8 JSON payloads, then releases native buffers. |
| ImGui panels, native menu/actions/tools, native editor smokes | `apps/editor` | Avalonia Studio does not mutate this state directly. |
| Dock workspace, command palette, contribution descriptors, lifecycle events | `apps/studio/Shell` and `apps/studio/Core` | Native runtime is not a managed shell dependency. |
| Scene View scheduling plans and viewport status snapshots | `apps/studio/Core/Models/Viewports`, `apps/studio/Core/Services`, and Scene View feature code | `ViewportScheduler` is a pure planner; live Scene View cadence also flows through panel frame scheduling, lifecycle, native present presenter, and present drain. |
| Studio feature panel view models | `apps/studio/Features/*` | Feature code consumes shell/core abstractions, not native C++ package internals. |

## Dependency Direction

- Runtime packages do not depend on `apps/editor` or `apps/studio`.
- `apps/editor` may aggregate runtime packages because it is a host application.
- `editor-native` exports a C ABI from the native side. It links selected runtime packages but does not expose C++ classes to Studio.
- `apps/studio` talks to native code through `IViewportNativeApi`, `ViewportNativeBridge`, `IFrameDebuggerNativeApi`, and `FrameDebuggerNativeBridge`.
- Studio managed models store snapshots and statuses, not native pointers beyond short-lived interop structs.
- C++ editor panels can inspect runtime state through native services. They should not become public package APIs.

## Native Bridge Flow

Viewport composition flow:

1. Studio collects composition capabilities such as supported handle types and device identity.
2. `ViewportNativeBridge.QueryCompositionCompatibility()` builds an `EditorViewportNativeCompatibilityRequest`.
3. `editor_viewport_query_composition_compatibility()` validates ABI version, struct size, handle type, and device compatibility.
4. Studio copies the returned UTF-8 message, maps native status into `ViewportNativePresentStatus`, and releases the compatibility result when needed.
5. `ViewportNativeBridge.AcquirePresentPacket()` requests a packet for a viewport extent.
6. Native code returns image/semaphore handles, format, memory size, frame index, and status.
7. Studio converts the packet to a managed snapshot, then calls `editor_viewport_release_present_packet()`.

Frame debugger flow:

Frame Debugger v0 is read-only and fixture-backed in the default Workbench wiring. The native bridge exists as an injectable path:

1. Studio calls `FrameDebuggerNativeBridge.RequestCapture()` or `RequestResume()`.
2. Studio calls `TryAcquireSnapshot()` when it needs current data.
3. Native returns an `EditorFrameDebuggerNativeSnapshotBuffer` with `JsonUtf8` payload.
4. Studio copies the bytes into `NativeFrameDebuggerSnapshotPayload`, then releases the native buffer.
5. Selecting an execution event passes a UTF-8 string view into native code for selection.

## Render Flow

- Native editor viewport rendering owns Vulkan resources and frame-level synchronization.
- `apps/studio` owns only managed viewport request/scheduling state and composition host/presenter state.
- Present packets are short-lived. The managed layer must release packets after snapshot conversion to avoid leaking native handles.
- Device mismatch, unsupported composition interop, unsupported handle type, device lost, render failure, and internal error are represented as native status codes and mapped into managed statuses.

## Lifecycle

1. Studio starts and builds shell/core/feature services.
2. Scene View reads composition capabilities and asks native code whether shared composition is supported.
3. On each render request, Studio requests a present packet for a concrete viewport extent.
4. Studio copies the present metadata and releases the native packet.
5. Frame debugger snapshots are acquired, copied, and released with the same ownership rule.
6. When Studio shuts down or the native backend is no longer usable, `ViewportNativeBridge.Shutdown()` calls native shutdown once and treats later calls as unavailable.
7. Native runtime destroys Vulkan resources according to RHI/editor ownership order.

## Error Handling

| Failure | Owner | Expected behavior |
|---|---|---|
| Native DLL missing, entry point missing, or bad image | Studio interop adapter | Catch binding exception and return unavailable managed status. |
| Frame debugger native DLL binding failure | Studio frame debugger bridge | This bridge currently does not wrap DLL or entry-point binding exceptions the same way as `ViewportNativeBridge`; callers should keep fixture/read-only fallback paths available. |
| Unsupported ABI or struct size | Native bridge and managed model validation | Return unsupported ABI status; do not dereference incompatible buffers. |
| Unsupported composition handle type | Native bridge | Return unsupported composition or unsupported handle type status. |
| Device mismatch | Native bridge | Return device mismatch; Studio should keep managed state inspectable. |
| Render failure or device lost | Native runtime | Return status and message; Studio maps to render failed/device lost. |
| Snapshot buffer format mismatch | Studio frame debugger bridge | Reject snapshot and release buffer if needed. |

## Future Work

`future`: Studio can own a generated native ABI compatibility report when the C structs become generated from a single schema. The current source of truth is still the C header plus C# mirror structs and tests.

`future`: if Studio gains multiple native viewport backends, the backend selection must remain below `IViewportNativeApi`; feature view models should continue to consume snapshots and statuses.

## Validation

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\pre-pr.ps1 -IncludeUntracked -RunCheapGates
dotnet build apps\studio\Editor.csproj -c Release
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "SceneView|ViewportNative|Composition|FrameDebuggerNative"
```

Native editor smoke gates when C++ editor or bridge behavior changes:

```powershell
foreach ($preset in @("clangcl-debug", "msvc-debug")) {
    $exe = "build\cmake\$preset\apps\editor\asharia-editor.exe"
    & $exe --smoke-editor-shell
    & $exe --smoke-editor-viewport-native
    & $exe --smoke-editor-frame-debugger
}
```

Checkpoints:

- Studio copies native buffers before release and does not store native packet pointers as durable state.
- Native ABI structs keep explicit `abiVersion` and `structSize` checks.
- Runtime packages remain independent from `apps/studio`.
- Native bridge errors are visible as managed statuses and messages, not swallowed as successful empty frames.
