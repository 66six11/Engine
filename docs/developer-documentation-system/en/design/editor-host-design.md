# Detailed Design: ImGui Editor Host, Panels, And Native Bridge

## Background

`apps/editor` is the native ImGui editor host. It connects GLFW windowing, Vulkan RHI, ImGui runtime, editor panels, asset catalog, viewport rendering, frame debugger, and native bridge exports. It is the C++ editor runtime and is not the same as the Avalonia Studio shell.

## Goals

- Use `runEditor(EditorRunConfig)` for interactive and smoke entry points.
- Use `EditorPanelRegistry` to manage panels, open/focus state, and lifecycle events.
- Use `EditorCommandHistory` for undo/redo transactions.
- Use `EditorRenderRuntime` to compose ImGui runtime, fullscreen renderer, and viewport coordinator.
- Use native bridge C ABI to expose viewport/frame debugger data to an external host.

## Non-Goals

- Do not let panel draw functions own Vulkan context directly.
- Do not let native bridge directly mutate editor command history.
- Do not put Avalonia view models into the C++ editor.
- Do not bypass asset pipeline to write product manifests directly.

## Current Constraints

- `apps/editor` depends on archive, asset-core, asset-pipeline, core, project-core, renderer-basic, scene-core, shader-slang, window-glfw, and rhi-vulkan.
- `editor-native` is a shared library that exports native bridge ABI.
- CLI supports interactive mode, project checks, JSON output, and editor smoke commands.
- Panel APIs use typed draw contexts instead of one unbounded global context.

## Overall Design

The editor app initializes window, Vulkan context/frame loop, ImGui runtime, and editor services. Each frame builds `EditorFrameContext`, containing UI, diagnostics, settings, tools, input, render graph snapshots, viewport host, selection, dirty state, asset catalog, and command history. Panel registry dispatches to typed draw contexts by panel type.

Viewport rendering uses `EditorRenderRuntime` and renderer-basic Vulkan render view to produce sampled textures or native present packets. Frame debugger and render graph panels consume diagnostics snapshots and do not directly record Vulkan commands.

## Module Breakdown

| Module/file | Responsibility |
|---|---|
| `apps/editor/src/main.cpp` | CLI parsing and mode dispatch |
| `editor_app.hpp` | editor run config and run entry |
| `editor_panel.hpp` | panel registry, panel state, typed panel contexts |
| `editor_command.hpp` | command, transaction, undo/redo history |
| `editor_render_runtime.hpp` | ImGui runtime, renderer, viewport coordinator ownership |
| `editor_asset_catalog.hpp` | editor asset catalog snapshot/report |
| `editor_frame_debugger.hpp` | frame debugger state |
| `editor_render_graph_snapshot.hpp` | RenderGraph diagnostics snapshots |
| `native_bridge/viewport_native_api.hpp` | viewport native C ABI |
| `native_bridge/frame_debugger_native_api.hpp` | frame debugger native C ABI |

## Data Structures

| Data | Key fields | Notes |
|---|---|---|
| `EditorRunConfig` | mode, assetCatalog config | app entry config |
| `EditorPanelDesc` | id, title, category, dock, singleton | panel registration contract |
| `EditorFrameContext` | ui, diagnostics, settings, tools, input, viewport, asset catalog | per-frame panel input |
| `EditorCommandHistory` | undoStack, redoStack, revision | transaction history |
| `EditorViewportNativePresentPacket` | handles, extent, format, frame index, status | native viewport output |

## API Design

- `runEditor(mode/config)` is process-level API.
- `ImGuiEditorPanel::draw()` receives typed context through `EditorPanelDrawContext`.
- Specialized panel base classes narrow context: scene view, scene tree, inspector, log, render graph, frame debugger, settings, asset browser.
- Native bridge functions use ABI headers with version/size and return explicit status codes.

## Key Flows

### Normal Flow

1. Parse CLI into `EditorRunConfig`.
2. Create window, Vulkan context, frame loop, and ImGui runtime.
3. Register editor panels and services.
4. Build `EditorFrameContext` each frame.
5. Panel registry draws open panels.
6. Viewport panel requests render view output.
7. Diagnostics snapshots feed render graph/frame debugger panels.

### Failure Flow

- Invalid CLI option: main returns failure and prints usage/error.
- Project check JSON without `--check-project`: argument parser rejects it.
- Panel duplicate id: registry returns `VoidResult` error.
- Native ABI size/version mismatch: native bridge returns unsupported ABI status.
- Viewport render failure: native packet status reports render/device/internal error.

### Boundary Flow

- Panel draw code may request commands but should not mutate persistent state directly.
- Native bridge packets must be released through the matching release function.
- Asset browser reimport uses command/log state and does not directly write product manifest.

## Lifetime

Editor app owns services for process lifetime. Panel registry owns panel instances and panel states. Command history owns transactions until undo stack depth expires or `clear()`. Native present packets use acquire/release lifetime across the ABI boundary. Vulkan resources remain owned by render runtime/RHI.

## Error Handling

Editor C++ APIs use `Result`/`VoidResult` internally. Native bridge uses explicit status enums and message buffers. CLI returns process exit code. Panel errors should go to diagnostics log instead of throwing through the UI frame.

## Test Plan

```powershell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-shell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-asset-browser
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-native-bridge
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

## Risks

- Panel context can degrade into a service locator if it grows too broad. Mitigation: use specialized typed contexts.
- Missing native packet release can leak external handles. Mitigation: ABI has paired acquire/release and runtime stats.
- Editor panels that directly mutate asset/product state bypass pipeline diagnostics. Mitigation: use commands and pipeline workflows.
