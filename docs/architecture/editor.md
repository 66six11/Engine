# Editor 架构

更新日期：2026-05-20

本文记录当前 `apps/editor` 的真实架构边界。它描述已经落地的 editor host、ImGui
integration、panel/action/event、Scene View viewport、input/shortcut routing、ImGui texture registry 和验证入口。
阶段拆分见 `docs/planning/editor-development-plan.md`；脚本扩展和 C++/脚本协作边界见
`docs/architecture/editor-ui-scripting.md`。

## 目的

`apps/editor` 是 Dear ImGui editor host。它负责把窗口、Vulkan frame loop、ImGui shell、
editor panels 和 renderer sampled output 组合成一个可运行的 editor executable：
`asharia-editor`。

Editor 不是 engine core，也不是 renderer owner。Renderer、RHI 和 RenderGraph 不依赖 editor；
runtime app 不链接 editor UI；未来 `packages/editor-core` 只承载 backend-neutral editor state。

当前 editor 的目标是：

- 提供可启动、可 smoke 的 ImGui shell。
- 用 panel/action/event registry 固化 editor UI 状态流。
- 让 Scene View 成为 RenderView sampled output 的真实消费者。
- 让菜单和快捷键通过同一套 action registry 触发 editor 命令。
- 让 ImGui texture descriptor lifetime 留在 editor integration 层。
- 保持 panel 代码不直接录制 Vulkan commands。

## 当前 Target

`apps/editor/CMakeLists.txt` 生成 `asharia-editor` 和 `asharia::editor` alias。

允许依赖：

- `asharia::core`
- `asharia::window_glfw`
- `asharia::rhi_vulkan`
- `asharia::renderer_basic_vulkan`
- `imgui::imgui`
- ImGui GLFW/Vulkan backend source files from the Conan ImGui package

禁止方向：

- `engine/core`、runtime packages、renderer packages 不依赖 `apps/editor`。
- 未来 `packages/editor-core` 不 include ImGui、GLFW、Vulkan 或 renderer implementation headers。
- Editor panels 不 include 任何 package 的 `src/`，也不访问 Vulkan object ownership。

## 模块所有权

| 模块 | 拥有 | 不能拥有 |
| --- | --- | --- |
| `editor_i18n` | editor-local text catalog, locale selection and stable ImGui label formatting | runtime localization, asset text localization or renderer-facing strings |
| `editor_ui` | small editor-local ImGui style primitives, Deep Slate theme tokens and component preview helpers used by panels | a generic UI framework or runtime-facing widget abstraction |
| `editor_app` | startup、window/context/frame-loop wiring、main editor loop、frame order、smoke modes、shutdown order | panel widget details becoming feature-specific renderer logic |
| `imgui_runtime` | ImGui context、GLFW backend、Vulkan backend lifecycle | panel registry、editor state、viewport target ownership |
| `imgui_editor_shell` | dockspace、main menu、action menu binding | renderer command recording、panel object ownership |
| `editor_panel` | panel descriptor/state、singleton panel registry、focus/open/close lifecycle | ImGui backend setup、Vulkan resource lifetime |
| `editor_action` | action descriptor、enabled state、callback invocation、stable action ids | command transaction semantics before transaction exists |
| `editor_event` | frame-local typed event queue、diagnostics history sink | global EventBus、durable document storage |
| `editor_context` | references to current editor services passed to actions | GPU resources、long-lived document ownership |
| `editor_input_router` | ImGui capture snapshot、Scene View hover/focus state、derived viewport/shortcut input flags | raw GLFW callback ownership、camera/gizmo behavior |
| `editor_shortcut_router` | shortcut metadata parsing、ImGui shortcut polling、input-gated action invocation | command transaction semantics、raw GLFW callback ownership |
| `editor_viewport` | backend-neutral viewport request/result structs、Scene/debug viewport flags and panel-facing host interface | ImGui descriptor allocation、Vulkan command recording |
| `editor_viewport_coordinator` | viewport request collection、Scene-only flag filtering、explicit Game debug flag retention、RenderView recording bridge、pending/presented/retired viewport targets | panel widgets、ImGui backend lifecycle |
| `imgui_texture_registry` | `ImTextureID` / descriptor registration、Scene View flag metadata and delayed descriptor retirement | `VulkanRenderTarget`、`VkImage`、`VkImageView` ownership |
| `panels/*` | concrete ImGui panel controls | Vulkan commands、descriptor registration、renderer resource lifetime |

## 数据流

### 启动

```text
main()
  parse --smoke-editor-* / --help / --version
  runEditor(mode)
    GlfwInstance::create()
    GlfwWindow::create()
    VulkanContext::create(required GLFW extensions)
    VulkanFrameLoop::create(context, framebuffer extent)
    ImGuiRuntime::create(window, context, frameLoop)
    BasicFullscreenTextureRenderer::create()
    EditorViewportCoordinator::create(context)
    register editor panels
    register editor actions
    runEditorLoop()
```

Editor 直接创建 Vulkan context 和 frame loop，因为它当前是 host application。这只发生在
app/integration 层，不会让 `rhi_vulkan` 反向依赖 editor。

### 每帧顺序

```text
poll window events
prepare/recreate frame loop extent

ImGui_ImplVulkan_NewFrame()
ImGui_ImplGlfw_NewFrame()
ImGui::NewFrame()

inputRouter.beginFrame(ImGui capture flags)
viewportCoordinator.beginImguiFrame(frame epochs)
panelRegistry.clearLifecycleEvents()
eventQueue.clear()

draw dockspace and main menu
panelRegistry.drawPanels(frameContext)
inputRouter.finalizeFrame()
shortcutRouter.beginFrame(inputRouter.snapshot())
shortcutRouter.routeImGuiShortcuts(actionRegistry, editorContext)

ImGui::Render()

frameLoop.renderFrame(callback):
  viewportCoordinator.recordRequestedViews(frame, fullscreenRenderer)
  record editor ImGui draw data to swapchain

diagnosticsLog.appendEvents(eventQueue.events())
eventQueue.clear()
panelRegistry.clearLifecycleEvents()
```

`VulkanFrameLoop` remains the owner of acquire, command buffer begin/end, submit, present, swapchain recreation,
fences and completed frame epochs.

### Scene View 纹理流

```text
SceneViewPanel::draw()
  compute content extent
  viewportHost.requestViewport(EditorViewportRequest with overlay flags)
  viewportHost.acquireViewportTextureForDraw(panel id)
  ImGui::Image(ImTextureID) if a completed texture exists

EditorViewportCoordinator::recordRequestedViews()
  keep effective viewport flags as view-local render intent
  ensure or reuse VulkanRenderTarget for requested extent
  BasicFullscreenTextureRenderer::recordViewFrame()
  ImGuiTextureRegistry::registerOrUpdate(sampled texture view + flag metadata)
  keep pending/presented/retired viewport texture state
```

The display is intentionally one frame delayed. This keeps panel drawing simple and avoids two-phase panel rendering until
same-frame presentation is required and measured.

## 生命周期

### Viewport target 生命周期

`EditorViewportCoordinator` owns the editor viewport target state:

- `presentedTexture_` is the last texture safe for panels to draw.
- `pendingTexture_` receives a newly rendered or resized target.
- `retiredTextures_` holds replaced targets until they are deferred through the frame loop.

旧 render target 通过 `VulkanFrameRecordContext::deferDeletion()` 销毁，让 frame loop 用 fence/epoch
约束 GPU 使用。Resize 不能立刻销毁当前正在呈现的 target。

### ImGui descriptor 生命周期

`ImGuiTextureRegistry` owns ImGui descriptor registration:

- `registerOrUpdate()` calls `ImGui_ImplVulkan_AddTexture()`.
- `acquireForDraw()` records the submitted frame epoch that may reference the descriptor.
- `collectGarbage(completedFrameEpoch)` calls `ImGui_ImplVulkan_RemoveTexture()` only after the frame loop reports the
  relevant submitted frame complete.

The registry does not own the underlying image or image view. It only owns ImGui's descriptor handle and retirement state.

### ImGui layout persistence

`ImGuiRuntime` owns Dear ImGui layout persistence. It resolves a user-local `imgui-layout.ini` path under the editor app
state directory, assigns `ImGuiIO::IniFilename` during ImGui context creation and flushes the layout before ImGui shutdown.
This state stores editor window/docking layout only; it is not scene data, asset data or runtime configuration.

### ImGui theme

`editor_ui` owns the editor-local Dear ImGui style tokens and applies the Unreal-Like Deep Slate palette during
`ImGuiRuntime::create()`. This is editor shell presentation state only; renderer, RHI and runtime packages do not depend on
theme colors, rounding values or component preview helpers.

### Editor i18n

`editor_i18n` owns the first editor-local text catalog. The catalog is key-based and currently covers `en-US` and
`zh-Hans` for menus, panel titles and the core Scene View / Log / RG View / Frame Debug labels. It is deliberately scoped to
`apps/editor`; runtime, renderer and asset text localization are separate future concerns.

Dear ImGui labels must preserve stable IDs when visible text changes. Editor UI code should use `EditorI18n::label()` for
menus, actions, panel windows and other stateful controls so labels are emitted as `translated text###stable-id`. This keeps
layout ini, docking state and widget identity stable across locale changes.

Interactive locale selection currently reads `ASHARIA_EDITOR_LOCALE` (`en-US` by default, `zh-Hans` supported). When
`zh-Hans` is active, `ImGuiRuntime` requests CJK glyph coverage from `ASHARIA_EDITOR_CJK_FONT` or a small list of common
system font locations. This keeps the first localization path usable during development, but bundled editor font assets and
license-reviewed packaging remain a later distribution task.

### 关闭

关闭顺序必须显式：

```text
viewportCoordinator.shutdown()
imguiRuntime.shutdown()
window.requestClose()
renderer / frameLoop / context destructors run after local owners leave scope
```

Queue wait 只允许出现在 ImGui backend shutdown、viewport texture shutdown 这类 editor teardown 路径。
不要把它加进交互式 render loop。

## 扩展点

### Panels 扩展

Add built-in panels by implementing `ImGuiEditorPanel` under `apps/editor/src/panels/` and registering the factory from
`registerEditorPanels()`.

Panel rules:

- Use `EditorFrameContext` services.
- Keep persistent scene/asset edits out of `draw()` until transactions exist.
- Do not allocate ImGui Vulkan textures directly.
- Do not record Vulkan commands.
- Report hover/focus state to `EditorInputRouter` instead of making global input routing decisions locally.
- Reuse `editor_ui` helpers for repeated editor styling primitives, but do not hide raw ImGui behind a broad widget clone.
- Use `editor_i18n` keys for user-facing labels and keep technical names such as pass, resource and shader identifiers
  untranslated.

### Actions 扩展

Add menu or shortcut commands through `EditorActionRegistry`. Disabled actions should remain registered when a feature is
planned but unavailable, so menus and diagnostics stay stable.

Action rules:

- Use stable action ids such as `view.scene-view`.
- Keep `shortcut` strings in action descriptors; `EditorShortcutRouter` is the only per-frame ImGui shortcut poller.
- Emit `ActionInvoked` through the event queue.
- 未来状态修改必须通过 command/transaction services。

### Input 扩展

`EditorInputRouter` 是 editor host 的输入归属事实源。它当前记录 ImGui capture flags、
Scene View hover/focus state、`sceneViewCanReceiveMouse` 和 `shortcutsEnabled`。

`EditorShortcutRouter` 消费 input router snapshot。它只在 `shortcutsEnabled` 为 true 时把
registered action shortcuts 转为 ImGui key chord，并调用 `EditorActionRegistry::invoke()`。
菜单、快捷键和未来 command palette 必须共享 action id，不要各自实现命令语义。

后续 viewport camera、gizmo 和 selection picking 也应先消费 input router snapshot，不要在各自模块里
重新读取全局 ImGui/GLFW 状态。

### Viewports 扩展

Add new viewport consumers through `EditorViewportKind` and the `EditorViewportPanelHost` request/result API. Scene View,
Game View and Preview View should share renderer/RHI caches but own view-local request state.

`EditorViewportOverlayFlags` currently carries grid、transform gizmo、wire、selection outline、debug overlay and debug gizmo intent.
The Scene View panel requests the default Scene authoring flags. `EditorViewportCoordinator` strips Scene-only authoring
flags from Game/Preview requests, but Game View may retain explicitly requested debug overlay/debug gizmo flags for future
runtime diagnostics.

Game View 不能隐式包含 grid、transform gizmo、wire overlay、selection outline 这类 Scene View authoring pass；如果用户需要在 Game View 里看 runtime debug gizmo，必须通过明确的 debug overlay/debug gizmo flag 进入 graph。

### 未来 `editor-core`

Do not extract `packages/editor-core` just to move files. Extract only when there is durable backend-neutral state with real
consumers, such as selection, transaction, inspector data model or editor service facade.

`editor-core` may own:

- `EditorId`
- action/event metadata
- selection model
- transaction and dirty-state model
- inspector data model

它不能拥有：

- `ImGuiContext*`
- `ImTextureID`
- `GLFWwindow*`
- `VkImage`, `VkImageView`, `VkDescriptorSet`
- renderer implementation objects

## 验证

Baseline gates for editor architecture changes:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1
git diff --check
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

Editor shell 相关改动必须运行：

```powershell
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-shell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-shell
```

Viewport、descriptor lifetime 或 resize 相关改动还必须运行：

```powershell
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
```

`--smoke-editor-viewport` also validates Scene View flag defaults, verifies that Scene-only authoring flags are cleared from
Game/Preview, verifies that Game View can retain explicit debug overlay/debug gizmo intent, verifies that a flagged Scene View
texture is rendered and acquired back through the panel-facing texture result, and checks that the recorded RenderView exposes
a view-local diagnostics snapshot. It also verifies idle Scene View on-demand reuse by checking that UI frames can reuse the
last completed texture without incrementing `viewportFramesRendered` every frame.
`--smoke-editor-frame-debugger` validates the editor-controlled `Running -> CaptureRequested -> CapturingFrame ->
WaitingGpuFence -> PausedFrameDebug -> Resume -> Running` flow. While waiting/paused, the editor keeps ImGui rendering alive
but skips normal RenderView recording, so the captured diagnostics snapshot stays frozen until Resume. The same smoke also
verifies that the Frame Debug RG View consumes the captured snapshot, requests a selected image resource preview, records only
the debug replay/copy path, and displays the resulting sampled preview texture.

## 当前缺口

- Selection, transaction, dirty state, inspector and asset browser are blocked on scene/asset/schema ownership becoming
  concrete enough.
- World-space grid, transform gizmo, wire, selection outline, debug overlay and debug gizmo passes are still pending
  renderer-side view pass work. The editor currently owns only the view-local overlay intent and texture metadata loop.
- Renderer prerequisites for those passes are: view/camera params in render view data, explicit overlay pass load/store
  semantics, blend state or a dedicated composition path, and a debug/world-line draw route.
- `EditorFrameDebugger` now owns capture/pause/resume state and freezes the captured `BasicRenderViewDiagnostics` snapshot.
- `RenderGraphPanel` is the Live RG View: it browses the latest compiled RenderView diagnostics snapshot as
  pass/resource/access/dependency/transition data without requiring Frame Debug capture.
- `FrameDebuggerPanel` owns the Frame Debug RG View: it browses the frozen captured snapshot, selects a graph-local image
  resource for v1 preview, and remains the future owner for pass/event replay selection.
- Scene View uses an editor-owned on-demand refresh policy. The panel still submits a viewport request every UI frame, but
  `EditorViewportCoordinator` only records a new RenderView when it derives a repaint reason such as initial texture,
  resize, overlay flag change, frame-debug event or `AlwaysRefresh`; otherwise ImGui redraws the previous texture.
- Frame Debug, Live RG View, Frame Debug RG View and pass graph visualization are separate editor concepts:
  - Frame Debug owns capture, pause/resume and fixed-frame inspection. It does not use `vkDeviceWaitIdle` for normal capture
    and does not read transient GPU resources after normal execution.
  - Live RG View displays the latest diagnostics snapshot derived after RenderGraph compile. The graph topology, dependency
    order, culling result, transition plan and resource lifetime are known at compile time; panel `draw()` does not record
    Vulkan commands or infer graph structure from GPU execution.
  - Frame Debug RG View displays the frozen diagnostics snapshot owned by Frame Debug and can request a debug-owned sampled
    preview texture for a selected image resource through `EditorViewportCoordinator`.
  - Pass graph visualization is a read-only node view derived from one of those snapshots, not an editable graph authoring
    system.
- Intermediate texture preview v1 is GPU-side only: Frame Debug records a controlled replay/copy into a debug-owned sampled
  image and registers that image through the existing `ImGuiTextureRegistry`. It supports color images with matching
  extent/format/mip/layer shape and reports `preview unavailable` for depth, buffer or unsupported resources. CPU readback,
  export and pass/event-precise replay remain deferred.
- `recordEditorImguiFrame()` 当前位于 `editor_app.cpp`。作为 host integration 现在可以接受；如果它
  超出 swapchain ImGui pass recording，应移动到 `imgui_runtime` 或独立的 editor ImGui pass module。
- There is no `packages/editor-core` yet by design.
