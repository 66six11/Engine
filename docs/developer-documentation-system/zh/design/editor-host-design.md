# 详细设计：ImGui Editor Host、Panels 与 Native Bridge

## 背景

`apps/editor` 是 native ImGui editor host。它连接 GLFW window、Vulkan RHI、ImGui runtime、editor panels、asset catalog、viewport rendering、frame debugger 和 native bridge exports。它是 C++ editor runtime，不等同于 Avalonia Studio shell。

## 目标

- 用 `runEditor(EditorRunConfig)` 提供 interactive 和 smoke entrypoints。
- 用 `EditorPanelRegistry` 管理 panels、open/focus state 和 lifecycle events。
- 用 `EditorCommandHistory` 管理 undo/redo transaction。
- 用 `EditorRenderRuntime` 组合 ImGui runtime、fullscreen renderer 和 viewport coordinator。
- 用 native bridge C ABI 暴露 viewport/frame debugger 数据给外部 host。

## 非目标

- 不在 panel draw 函数中直接拥有 Vulkan context。
- 不让 native bridge 直接修改 editor command history。
- 不把 Avalonia view model 放进 C++ editor。
- 不绕过 asset pipeline 直接写 product manifest。

## 当前约束

- `apps/editor` 依赖 archive、asset-core、asset-pipeline、core、project-core、renderer-basic、scene-core、shader-slang、window-glfw、rhi-vulkan。
- `editor-native` 是 shared library，导出 native bridge ABI。
- CLI 支持 interactive、project checks、JSON output 和 editor smoke commands。
- Panel APIs 使用 typed draw contexts，而不是一个无限扩张的 global context。

## 总体方案

Editor app 初始化 window、Vulkan context/frame loop、ImGui runtime 和 editor services。每帧构造 `EditorFrameContext`，包含 UI、diagnostics、settings、tools、input、render graph snapshots、viewport host、selection、dirty state、asset catalog 和 command history。Panel registry 负责按 panel 类型分发到 typed draw context。

Viewport rendering 通过 `EditorRenderRuntime` 和 renderer-basic Vulkan render view 生成 sampled texture 或 native present packet。Frame debugger 和 render graph panels 消费 diagnostics snapshots，不直接录制 Vulkan commands。

## 模块划分

| 模块/文件 | 职责 |
|---|---|
| `apps/editor/src/main.cpp` | CLI parsing 和 mode dispatch |
| `editor_app.hpp` | editor run config 和 run entry |
| `editor_panel.hpp` | panel registry、panel state、typed panel contexts |
| `editor_command.hpp` | command、transaction、undo/redo history |
| `editor_render_runtime.hpp` | ImGui runtime、renderer、viewport coordinator ownership |
| `editor_asset_catalog.hpp` | editor asset catalog snapshot/report |
| `editor_frame_debugger.hpp` | frame debugger state |
| `editor_render_graph_snapshot.hpp` | RenderGraph diagnostics snapshots |
| `native_bridge/viewport_native_api.hpp` | viewport native C ABI |
| `native_bridge/frame_debugger_native_api.hpp` | frame debugger native C ABI |

## 数据结构

| 数据 | 关键字段 | 说明 |
|---|---|---|
| `EditorRunConfig` | mode、assetCatalog config | app entry config |
| `EditorPanelDesc` | id、title、category、dock、singleton | panel registration contract |
| `EditorFrameContext` | ui、diagnostics、settings、tools、input、viewport、asset catalog | per-frame panel input |
| `EditorCommandHistory` | undoStack、redoStack、revision | transaction history |
| `EditorViewportNativePresentPacket` | handles、extent、format、frame index、status | native viewport output |

## API 设计

- `runEditor(mode/config)` is process-level API.
- `ImGuiEditorPanel::draw()` receives typed context through `EditorPanelDrawContext`.
- Specialized panel base classes narrow context: scene view, scene tree, inspector, log, render graph, frame debugger, settings, asset browser.
- Native bridge functions use ABI headers with version/size and return explicit status codes.

## 关键流程

### 正常流程

1. Parse CLI into `EditorRunConfig`.
2. Create window, Vulkan context, frame loop and ImGui runtime.
3. Register editor panels and services.
4. Each frame builds `EditorFrameContext`.
5. Panel registry draws open panels.
6. Viewport panel requests render view output.
7. Diagnostics snapshots feed render graph/frame debugger panels.

### 失败流程

- invalid CLI option：main returns failure and prints usage/error。
- project check JSON without `--check-project`：argument parser rejects。
- panel duplicate id：registry returns `VoidResult` error。
- native ABI size/version mismatch：native bridge returns unsupported ABI status。
- viewport render failure：native packet status reports render/device/internal error。

### 边界流程

- Panel draw code can request commands but should not mutate persistent state directly.
- Native bridge packets must be released through matching release function.
- Asset browser reimport uses command/log state and does not directly write product manifest.

## 生命周期

Editor app owns services for process lifetime. Panel registry owns panel instances and panel states. Command history owns transactions until undo stack depth expires or `clear()`. Native present packets use acquire/release lifetime across ABI boundary. Vulkan resources remain owned by render runtime/RHI.

## 错误处理

Editor C++ APIs use `Result`/`VoidResult` internally. Native bridge uses explicit status enums and message buffers. CLI returns process exit code. Panel errors should go to diagnostics log instead of throwing through UI frame.

## 测试方案

```powershell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-shell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-asset-browser
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-native-bridge
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

## 风险

- Panel context 过宽会退化为 service locator。缓解：使用 specialized typed contexts。
- Native packet release 若遗漏，会泄漏 external handles。缓解：ABI 成对 acquire/release，并暴露 runtime stats。
- Editor panel 直接改 asset/product state 会绕过 pipeline diagnostics。缓解：通过 commands 和 pipeline workflows。
