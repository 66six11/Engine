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
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
```

`--smoke-editor-viewport` also validates Scene View flag defaults, verifies that Scene-only authoring flags are cleared from
Game/Preview, verifies that Game View can retain explicit debug overlay/debug gizmo intent, verifies that a flagged Scene View
texture is rendered and acquired back through the panel-facing texture result, and checks that the recorded RenderView exposes
a view-local diagnostics snapshot.

## 当前缺口

- Selection, transaction, dirty state, inspector and asset browser are blocked on scene/asset/schema ownership becoming
  concrete enough.
- World-space grid, transform gizmo, wire, selection outline, debug overlay and debug gizmo passes are still pending
  renderer-side view pass work. The editor currently owns only the view-local overlay intent and texture metadata loop.
- Renderer prerequisites for those passes are: view/camera params in render view data, explicit overlay pass load/store
  semantics, blend state or a dedicated composition path, and a debug/world-line draw route.
- `BasicRenderViewDiagnostics` is now available per recorded view, but the editor does not yet have a Frame Debug state
  machine or RG View panel to browse that snapshot interactively.
- Frame Debug, RG View and pass graph visualization are separate editor concepts:
  - Frame Debug owns capture, pause/resume and fixed-frame inspection.
  - RG View displays the compiled RenderGraph snapshot as pass/resource/dependency/lifetime data.
  - Pass graph visualization is a read-only node view derived from the same snapshot, not an editable graph authoring system.
- `recordEditorImguiFrame()` 当前位于 `editor_app.cpp`。作为 host integration 现在可以接受；如果它
  超出 swapchain ImGui pass recording，应移动到 `imgui_runtime` 或独立的 editor ImGui pass module。
- There is no `packages/editor-core` yet by design.
