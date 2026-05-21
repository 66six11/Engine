# Editor 开发方案

更新日期：2026-05-19

本文是 Asharia Editor 的独立开发文档，覆盖 editor host、ImGui integration、panel/action/event、
viewport texture registry、输入路由和后续阶段拆分。全局阶段顺序仍以
`docs/planning/next-development-plan.md` 为准；本文细化 editor 相关阶段，不改变全局阶段编号。
当前 editor 架构、模块所有权和 frame flow 见 `docs/architecture/editor.md`。Editor UI 的 C++
主实现、脚本扩展面、transaction 和 safe point 边界见 `docs/architecture/editor-ui-scripting.md`。

## 目标

让 `apps/editor` 尽早成为 RenderView sampled output 的真实消费者，同时保持 package-first 边界：

- ImGui、GLFW backend、Vulkan backend 和 texture descriptor registration 只属于 editor host/integration。
- 未来 `packages/editor-core` 只拥有 backend-neutral editor state，例如 action、event、selection、
  transaction 和 panel metadata。
- Renderer、RHI、RenderGraph 不依赖 ImGui 或 editor。
- 不抽象一套通用 UI，不做 `asharia::ui::button()` / `asharia::ui::image()` 这类 ImGui clone。
- 第一版 editor shell、dockspace、viewport、Inspector shell 和 Asset Browser shell 由 C++ 实现；脚本后续只通过
  action、transaction、declarative panel model 和 public facade 扩展 editor。

## 一手资料约束

- Dear ImGui 把 core、platform backend 和 renderer backend 分开。Asharia 只在 `apps/editor` 接入
  GLFW platform backend 和 Vulkan renderer backend。
  资料：https://github.com/ocornut/imgui/blob/master/docs/BACKENDS.md
- Dear ImGui 输入路径应先把鼠标键盘事件交给 ImGui，再用 `io.WantCaptureMouse` /
  `io.WantCaptureKeyboard` 判断是否继续传给 viewport camera、gizmo、shortcut 或 runtime input。
  资料：https://github.com/ocornut/imgui/blob/master/docs/FAQ.md
- Unity `EditorWindow.GetWindow()`、Unreal `FTabManager` 和 Godot custom dock 都支持按 type/id 注册、
  复用、dock 和生命周期管理 panel/window。
  资料：https://docs.unity.cn/Manual/editor-EditorWindows.html
  资料：https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Slate/Framework/Docking/FTabManager
  资料：https://docs.godotengine.org/en/stable/tutorials/plugins/editor/making_plugins.html#a-custom-dock
- 当前仓库 pin `imgui/1.92.7-docking`。此版本 Vulkan backend user texture API 是
  `ImGui_ImplVulkan_AddTexture(VkSampler, VkImageView, VkImageLayout)`；后续 ImGui API 变化必须被
  `ImGuiTextureRegistry` 封装隔离。

## 分层边界

```text
apps/editor
  editor app loop
  ImGui runtime and GLFW/Vulkan backend setup
  ImGui dockspace/menu/shell
  ImGui panel implementations
  ImGui texture descriptor registry
  Vulkan sampled target registration
  viewport render coordination

future packages/editor-core
  editor ids and owned metadata strings
  action registry
  event queue
  panel registry metadata and lifecycle state
  selection model
  transaction / command model
  inspector data model

renderer / RHI packages
  RenderView target contract
  sampled output image/view/layout/format/extent
  GPU resource lifetime and frame deferred deletion
```

Rules:

- `apps/editor` may link `imgui`, `window-glfw`, `rhi-vulkan` and `renderer_basic_vulkan`.
- `editor-core` must not include ImGui headers, Vulkan headers, GLFW headers or renderer implementation headers.
- Runtime apps must not link editor packages.
- Editor panels consume services and public package APIs; they do not include another package's `src/`.
- Scene View panel does not record Vulkan commands.

## First Module Split

第一阶段先把 `apps/editor/src/main.cpp` 拆成 app 内部模块，不新增 `packages/editor-core`：

```text
apps/editor/src/
  editor_app.hpp/.cpp
  editor_context.hpp/.cpp
  editor_action.hpp/.cpp
  editor_event.hpp/.cpp
  editor_panel.hpp/.cpp
  editor_viewport.hpp/.cpp
  editor_viewport_coordinator.hpp/.cpp
  imgui_runtime.hpp/.cpp
  imgui_editor_shell.hpp/.cpp
  imgui_texture_registry.hpp/.cpp
  panels/
    scene_view_panel.hpp/.cpp
    log_panel.hpp/.cpp
```

| Module | Owns | Must not own |
| --- | --- | --- |
| `editor_app` | startup, loop, frame order, smoke modes, shutdown order | panel drawing details |
| `editor_context` | per-frame/editor services passed to panels | GPU objects |
| `editor_action` | action descriptors, callbacks, invocation, shortcut metadata | menu widget code |
| `editor_event` | small typed queue for editor facts | global EventBus semantics |
| `editor_panel` | panel descriptors, state, registry, singleton reuse | ImGui backend setup |
| `editor_input_router` | ImGui capture snapshot, Scene View hover/focus state and derived input routing flags | raw GLFW callback ownership or camera/gizmo behavior |
| `editor_shortcut_router` | shortcut metadata parsing, ImGui shortcut polling and input-gated action invocation | transaction semantics or raw GLFW callback ownership |
| `editor_viewport` | backend-neutral viewport request/result model | ImGui descriptor allocation or Vulkan command recording |
| `editor_viewport_coordinator` | request collection, RenderView recording, render target lifetime, texture registry publication | panel widgets or ImGui backend setup |
| `imgui_runtime` | ImGui context and GLFW/Vulkan backend lifecycle | panel registry or editor state |
| `imgui_editor_shell` | dockspace, main menu, panel host windows | renderer command recording |
| `imgui_texture_registry` | `ImTextureID` creation, descriptor retirement, sampler choice | RenderTarget ownership |
| `panels/*` | concrete ImGui controls for each panel | Vulkan commands or resource lifetime |

## Core Types

Registry-owned data must use owned strings. `std::string_view` is only for lookup/call-site convenience.

```cpp
struct EditorId {
    std::string value;
};

struct EditorPanelDesc {
    EditorId id;
    std::string title;
    bool defaultOpen{true};
    bool singleton{true};
};

struct EditorPanelState {
    bool open{true};
    bool focused{false};
    std::uint32_t contentWidth{1};
    std::uint32_t contentHeight{1};
};

class ImGuiEditorPanel {
public:
    virtual ~ImGuiEditorPanel() = default;
    virtual const EditorPanelDesc& desc() const = 0;
    virtual void draw(EditorFrameContext& context, EditorPanelState& state) = 0;
};
```

Future `editor-core` may keep `EditorId`, action/event types, panel descriptor/state and registry metadata. It must not keep
an interface that mentions ImGui-specific drawing.

## Panel Lifecycle

Panel lifecycle follows registered spawner semantics:

```text
registerPanel(desc, factory)
openPanel(id)
closePanel(id)
focusPanel(id)
drawPanels(context)
```

Rules:

- Singleton panels such as Scene View and Log reuse the existing instance when opened again.
- Menu actions invoke `openPanel("scene-view")`; they do not call `ImGui::Begin()` or construct panel objects directly.
- `PanelOpened`, `PanelClosed` and `PanelFocused` events are emitted by the registry after state changes.
- Layout persistence initially uses ImGui docking ini or a later explicit editor layout storage stage.

## Action Registry

```cpp
struct EditorActionDesc {
    EditorId id;
    std::string menuPath;
    std::string label;
    std::string shortcut;
    bool enabled{true};
};

using EditorActionCallback = std::function<void(EditorContext&)>;
```

Rules:

- Menus, shortcuts and future command palette invoke the same action ids.
- Actions mutate editor state only through explicit services such as panel registry, selection, transaction or app commands.
- `ActionInvoked` is an editor event, not the mutation itself. Menu, shortcut, command palette and script are invocation
  sources, not separate action meanings.
- Disabled actions remain registered so menu/shortcut UI can show stable layout and diagnostics.

## Event Queue

Use a narrow typed queue, not a global EventBus:

```cpp
enum class EditorEventKind {
    PanelOpened,
    PanelClosed,
    PanelFocused,
    ActionInvoked,
    ViewportResized,
    SelectionChanged,
};

struct EditorEvent {
    EditorEventKind kind;
    EditorId sourceId;
};
```

Rules:

- Events describe facts that already happened in the editor frame.
- Events are drained at deterministic points in `EditorApp`.
- UI diagnostics that need history consume events through a diagnostics sink; panels should not treat the frame-local
  event queue as durable storage.
- Events do not carry owning pointers to panels, world objects or Vulkan resources.
- Add typed payload variants only when a real consumer exists.

## Viewport Requests

Scene View panel submits a request and draws the latest available texture:

```cpp
struct Extent2D {
    std::uint32_t width{1};
    std::uint32_t height{1};
};

enum class EditorViewportKind {
    Scene,
    Game,
    Preview,
};

struct EditorViewportRequest {
    EditorId panelId;
    EditorViewportKind kind{EditorViewportKind::Scene};
    Extent2D extent;
};
```

`apps/editor` may translate `Extent2D` to `VkExtent2D`; future `editor-core` must keep the neutral type.

Default behavior is one-frame delayed:

1. Panel draws and submits request for the current content size.
2. Panel displays the last completed texture if one exists.
3. Host records the requested RenderView outside panel draw.
4. Renderer outputs sampled image/view/layout/format/extent.
5. Texture registry publishes the result for the next UI frame.

Do not add two-phase panel drawing until same-frame viewport presentation is required and measured.

Current Stage 17.1 implementation keeps this model in `apps/editor/src/editor_viewport.hpp/.cpp`. `EditorExtent2D`
remains backend-neutral; `apps/editor` translates it to `VkExtent2D` only inside the viewport host. Scene View submits an
`EditorViewportRequest`, acquires the last completed viewport texture for ImGui drawing, and reserves the requested
content area while no completed texture exists.

## ImGui Texture Registry

`ImGuiTextureRegistry` is an `apps/editor` integration object:

```text
beginFrame(completedFrameEpoch)
registerOrUpdate(panelId, VulkanSampledTextureView, submittedFrameEpoch) -> ImTextureID
retire(panelId, submittedFrameEpoch)
collectGarbage(completedFrameEpoch)
shutdown(queueIdleRequired)
```

Rules:

- The registry owns ImGui descriptor sets returned by `ImGui_ImplVulkan_AddTexture()`.
- The registry does not own `VulkanRenderTarget`, `VkImage` or `VkImageView`.
- Resize creates or updates a pending texture; the currently presented texture remains valid until no submitted ImGui draw
  data can reference it.
- `ImGui_ImplVulkan_RemoveTexture()` must be delayed until the frame loop reports the relevant submitted frame completed.
  The backend frees the descriptor set immediately.
- Shutdown may wait for the graphics queue before final descriptor release. This is allowed in shutdown, not in the render loop.
- Descriptor pool capacity must account for font atlas descriptors plus maximum concurrently live viewport/panel textures and
  delayed retired descriptors.
- Current Stage 17.2 implementation uses `apps/editor/src/imgui_texture_registry.hpp/.cpp`. The editor ImGui backend
  descriptor pool size is `kEditorImGuiDescriptorPoolSize = 128`; viewport texture descriptors are checked against
  `kEditorViewportTextureDescriptorBudget = 32` in viewport smoke.

## Input Routing

First stage can use `ImGui_ImplGlfw_InitForVulkan(window, true)`. When editor/game input needs raw event ownership, switch to
manual callback chaining:

```text
GLFW callback
  -> ImGui_ImplGlfw_*Callback(...)
  -> EditorRawInputQueue.push(...)

Frame:
  glfwPollEvents()
  ImGui_ImplVulkan_NewFrame()
  ImGui_ImplGlfw_NewFrame()
  ImGui::NewFrame()
  capture = { io.WantCaptureMouse, io.WantCaptureKeyboard, io.WantTextInput }
  route raw input to viewport camera / gizmo / shortcuts when capture allows
```

Rules:

- Always feed ImGui first.
- Shortcut routing should also consider focused panel and modal state, not only `WantCaptureKeyboard`.
- Text input goes to ImGui when `WantTextInput` is true.
- Viewport camera/gizmo should only receive mouse events when the target Scene View is hovered/focused and ImGui does not
  capture mouse.

## Frame Order

```text
poll window events
prepare swapchain extent

ImGui_ImplVulkan_NewFrame()
ImGui_ImplGlfw_NewFrame()
ImGui::NewFrame()

textureRegistry.beginFrame(frameLoop.completedFrameEpoch())
inputRouter.snapshotCaptureFlags(ImGui::GetIO())

shell.drawDockSpace()
shell.drawMainMenu(actionRegistry)
panelRegistry.drawPanels(frameContext)
viewportCoordinator.collectRequests()

ImGui::Render()

frameLoop.renderFrame(callback):
  viewportCoordinator.recordRequestedViews(renderer)
  textureRegistry.publishCompletedTargets()
  record ImGui draw data into swapchain dynamic-rendering pass

present
diagnosticsLog.appendEvents(eventQueue)
eventQueue.clear()
```

`VulkanFrameLoop` remains the owner of acquire, command buffer begin/end, submit, present, swapchain recreation, fences and
completed frame accounting.

## Validation

Minimum validation for editor host changes:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

When touching editor shell, panel/action/event state, viewport rendering, sampled texture registration, swapchain pass
recording or descriptor lifetime, also run the relevant editor smoke commands:

```text
--smoke-editor-shell
--smoke-editor-viewport
--smoke-editor-viewport-resize
```

`--smoke-editor-shell` covers shell/menu/panel/action/event state. `--smoke-editor-viewport` is the stricter sampled
viewport texture smoke. `--smoke-editor-viewport-resize` performs a real window resize and verifies resized viewport
texture presentation plus old descriptor/render-target retirement.

## Stage Status Rules

| Status | Meaning |
| --- | --- |
| Done | Implemented, documented and validated. |
| In progress | Partially implemented, missing split, validation or docs. |
| Next | Recommended next implementation slice. |
| Blocked | Requires another package, API or smoke first. |
| Deferred | Captured as design constraint, not scheduled soon. |

Every sub-stage must:

- Be independently committable and revertible.
- Preserve package-first dependencies.
- Update docs, smoke and review gates when behavior changes.
- Keep C/C++ files UTF-8 with BOM and Markdown UTF-8 without BOM.

## Stage Overview

| Global stage | Editor sub-stage | Status | Goal |
| --- | --- | --- | --- |
| 15 | 15.1 | Done | Lock ImGui sampled texture contract and reject generic UI layer. |
| 16 | 16.1-16.7 Done | Done | Split editor shell from one file into host/runtime/panel/action/event modules. |
| 17 | 17.1-17.7 Done | Done | Convert Scene View viewport to request/result + delayed texture registry, input capture and shortcut routing. |
| 20 | 20.1-20.5 | Blocked | Add editor-core selection and transaction after scene/object baseline. |
| 21 | 21.1-21.2 Done; 21.3-21.5 Next/Blocked | In progress | Close editor overlay intent loop, then add Scene View grid, gizmo, selection outline and debug overlay after render prerequisites. |
| 24 | 24.1-24.5 | Deferred | Add Asset Browser and Material Editor on asset/material public APIs. |
| 28 | 28.1-28.5 | Deferred | Add Edit/Game Play Session and multi-view diagnostics. |

## Phase 15: ImGui Sampled Texture Contract

### 15.1 Contract Documentation

Status: Done.

Scope:

- Renderer outputs sampled target image/view/layout/format/extent.
- Editor ImGui integration owns `ImGui_ImplVulkan_AddTexture()` / `RemoveTexture()`.
- No `packages/ui`, `UiTextureHandle` or engine-level ImGui clone.

Validation:

- `docs/planning/next-development-plan.md` records pinned `imgui/1.92.7-docking` API.
- `docs/rendergraph/roadmap.md` records that ImGui texture registration does not enter RenderGraph.
- This document records descriptor retirement rules.

## Phase 16: Editor App Shell

### 16.1 No-op Main Split

Status: Done.

Scope:

- Move startup, frame loop and shutdown into `editor_app`.
- Move per-frame service references into `editor_context`.
- Keep rendered output and smoke behavior unchanged.
- Current implementation keeps per-frame service references inside `editor_app` until panel registry work creates a real
  `editor_context` consumer.

Validation:

- `asharia-editor` starts.
- No new package dependencies.
- Encoding check and both debug builds pass.
- Implemented by splitting CLI entrypoint into `apps/editor/src/main.cpp` and app lifecycle into
  `apps/editor/src/editor_app.cpp`.

### 16.2 ImGui Runtime Isolation

Status: Done.

Scope:

- Move ImGui context, GLFW backend, Vulkan backend init/shutdown into `imgui_runtime`.
- Keep dynamic rendering setup in the runtime wrapper.
- Keep shutdown queue wait commented and limited to teardown.

Validation:

- Editor shell starts and shuts down without validation-layer lifetime errors.
- ImGui backend sources remain `SKIP_LINTING`.
- Implemented in `apps/editor/src/imgui_runtime.hpp/.cpp`; shutdown queue wait remains limited to teardown.

### 16.3 ImGui Shell Module

Status: Done.

Scope:

- Move dockspace and main menu rendering into `imgui_editor_shell`.
- Keep shell responsible for ImGui window chrome only.
- Do not let shell call renderer or own panel objects directly.

Validation:

- Dockspace and menu render as before.
- `imgui_editor_shell` only depends on editor action/panel interfaces and ImGui.
- Implemented as `apps/editor/src/imgui_editor_shell.hpp/.cpp`; panel window contents remain in `editor_app`
  until the panel registry baseline.

### 16.4 Panel Registry Baseline

Status: Done.

Scope:

- Add `editor_panel` with `EditorPanelDesc`, `EditorPanelState`, registry and singleton reuse.
- Convert Scene View and Log into registered panels under `panels/`.
- Emit panel lifecycle events.

Validation:

- View menu opens/focuses existing Scene View and Log panels.
- Closing/reopening singleton panels does not create duplicate state.
- Implemented as `apps/editor/src/editor_panel.hpp/.cpp` plus `apps/editor/src/panels/scene_view_panel.*`
  and `apps/editor/src/panels/log_panel.*`; View menu now routes through the panel registry.
- Lifecycle events are emitted by the registry and kept frame-local until the dedicated event queue lands in 16.6.

### 16.5 Action Registry Baseline

Status: Done.

Scope:

- Add `editor_action` with owned descriptors and callbacks.
- Convert File/View menu items to actions.
- Add disabled placeholder actions for unsupported commands.

Validation:

- Menu only invokes action ids.
- Action registry can be unit-smoked without ImGui where practical.
- Implemented as `apps/editor/src/editor_action.hpp/.cpp` and a minimal
  `apps/editor/src/editor_context.hpp/.cpp`; File menu entries are disabled action placeholders and View menu entries
  route through action ids to the panel registry.
- Editor smoke validates action count, disabled action rejection and View/Log action routing without ImGui widgets.

### 16.6 Event Queue Baseline

Status: Done.

Scope:

- Add small typed `EditorEventQueue`.
- Emit `PanelOpened`, `PanelClosed`, `PanelFocused` and `ActionInvoked`.
- Drain events at a deterministic point in `EditorApp`.

Validation:

- Log panel can display recent editor events from diagnostics history without owning panel internals or reading the
  frame-local queue directly.
- No global EventBus or pointer payloads.
- Implemented as `apps/editor/src/editor_event.hpp/.cpp`; panel registry emits lifecycle facts into the queue, action
  invocation emits `ActionInvoked`, and `EditorDiagnosticsLog` keeps a short recent-event history consumed by `LogPanel`.
- Editor smoke validates panel lifecycle events, disabled action rejection and View/Log menu action invocation through the
  typed queue.

### 16.7 Editor Shell Smoke

Status: Done.

Scope:

- Add `--smoke-editor-shell` to `asharia-editor`.
- Verify shell startup, dockspace/menu construction and panel registry open state.

Validation:

- Smoke returns non-zero on initialization, panel registry or ImGui frame failures.
- `docs/workflow/review.md` includes the editor smoke once stable.
- Implemented in `apps/editor/src/main.cpp` and `apps/editor/src/editor_app.cpp` with explicit
  `EditorRunMode::SmokeShell`; shell smoke validates startup, ImGui frame construction, dockspace/menu/panel registry,
  action registry and event queue without requiring viewport sampled texture presentation.
- `--smoke-editor-viewport` remains the stricter viewport texture smoke for Stage 17 work.

## Phase 17: Editor Viewport Host

### 17.1 Viewport Request Model

Status: Done.

Scope:

- Add `editor_viewport` request/result types.
- Scene View panel submits `EditorViewportRequest` with backend-neutral extent.
- Panel displays last completed texture if available, otherwise reserves content space.

Validation:

- Panel `draw()` contains no Vulkan command recording.
- Request/result model supports one-frame delayed display.
- Implemented as `apps/editor/src/editor_viewport.hpp/.cpp`; panel-facing request/result types use `EditorExtent2D`
  instead of Vulkan types.
- `SceneViewPanel::draw()` submits `EditorViewportRequest` and draws the last completed texture if available, otherwise
  reserves the requested content space.
- `EditorViewportCoordinator` resets requests at the start of each ImGui frame and records only the current frame's submitted
  request.

### 17.2 Texture Registry With Delayed Retirement

Status: Done.

Scope:

- Add `imgui_texture_registry`.
- Wrap `ImGui_ImplVulkan_AddTexture()` and `RemoveTexture()`.
- Track submitted frame epoch for descriptors and retire only after `completedFrameEpoch`.

Validation:

- Resize does not immediately free a descriptor that may be referenced by submitted ImGui draw data.
- Descriptor pool capacity is documented and checked against expected live descriptors.
- Implemented as `apps/editor/src/imgui_texture_registry.hpp/.cpp`; the registry wraps `ImGui_ImplVulkan_AddTexture()`
  and `ImGui_ImplVulkan_RemoveTexture()`, owns the descriptor set lifetime and keeps sampled image/view ownership in
  `EditorViewportCoordinator`.
- `acquireForDraw()` marks the active descriptor with the current frame's expected submitted epoch; replacing or retiring
  the descriptor moves it to a retired list and `beginFrame(completedFrameEpoch)` removes it only after completion.
- `--smoke-editor-viewport` checks peak live viewport texture descriptors against the editor viewport descriptor budget.

### 17.3 Viewport Render Coordinator

Status: Done.

Scope:

- Host collects viewport requests after panel drawing.
- Host calls `BasicFullscreenTextureRenderer::recordViewFrame()` outside panel code.
- Renderer output is published through `ImGuiTextureRegistry`.

Validation:

- Scene View displays sampled RenderView output through the registry.
- Renderer and RHI still do not include ImGui headers.
- Implemented as `apps/editor/src/editor_viewport_coordinator.hpp/.cpp`; `editor_app.cpp` now owns frame order and smoke
  checks, while the coordinator owns the pending/presented/retired viewport render targets.
- `EditorViewportCoordinator::recordRequestedViews()` is the only editor-side caller of
  `BasicFullscreenTextureRenderer::recordViewFrame()` for the viewport path.
- Renderer output is published through `ImGuiTextureRegistry` after the sampled RenderView record succeeds.

### 17.4 Resize And Pending Texture Flow

Status: Done.

Scope:

- Preserve presented texture while preparing pending resized target.
- Retire old target image/view through frame-loop deferred deletion.
- Retire old ImGui descriptor through completed-frame tracking.

Validation:

- Rapid resize does not drop to invalid descriptor use.
- Viewport smoke verifies at least one resize and continued texture presentation.
- Implemented with presented/pending/retired viewport targets in `EditorViewportCoordinator`.
- `--smoke-editor-viewport-resize` shrinks the editor window after a sampled viewport texture is presented, then verifies a
  smaller viewport texture is submitted through ImGui.
- The resize smoke checks that the old viewport render target reaches frame-loop deferred deletion and the old ImGui
  descriptor is retired through completed-frame tracking.

### 17.5 Editor Viewport Smoke

Status: Done.

Scope:

- Add `--smoke-editor-viewport`.
- Add `--smoke-editor-viewport-resize` if resize cannot be covered by the base smoke without flakiness.

Validation:

- Smoke verifies texture registration, at least one rendered viewport frame and at least one submitted ImGui texture frame.
- For resize smoke, verify old texture retirement and new extent publication.
- `apps/editor/src/main.cpp` exposes both `--smoke-editor-viewport` and `--smoke-editor-viewport-resize`.
- `docs/workflow/review.md` includes resize smoke in the editor viewport gate.

### 17.6 Input Capture Skeleton

Status: Done.

Scope:

- Add `EditorInputRouter` capture snapshot after `ImGui::NewFrame()`.
- Gate viewport camera/gizmo placeholders and shortcuts using `WantCaptureMouse`, `WantCaptureKeyboard`,
  focused panel and hovered viewport.
- Keep GLFW backend callback installation unchanged until raw input is needed.
- Current implementation adds `apps/editor/src/editor_input_router.hpp/.cpp`, stores an input router reference in
  `EditorFrameContext`, reports Scene View hover/focus from `SceneViewPanel`, and displays capture diagnostics in
  `LogPanel`.

Validation:

- UI-focused keyboard/mouse does not trigger viewport shortcut placeholders.
- Viewport-focused input path is visible in diagnostics/log panel.
- `--smoke-editor-shell` validates that input frames are captured and Scene View reports input state.

### 17.7 Shortcut Routing Baseline

Status: Done.

Scope:

- Add `EditorShortcutRouter` as the single per-frame ImGui shortcut poller.
- Parse `EditorActionDesc::shortcut` strings into ImGui key chords.
- Gate shortcuts through `EditorInputRouter::snapshot().shortcutsEnabled`.
- Invoke shortcuts through `EditorActionRegistry::invoke()` so menus and shortcuts share action ids and events.
- Keep disabled actions registered for menu layout while preventing shortcut invocation.

Validation:

- Synthetic smoke validates that disabled input capture blocks shortcuts.
- Synthetic smoke validates that disabled actions are not invoked by shortcuts.
- `--smoke-editor-shell` validates that registered shortcuts are parseable and evaluated every rendered frame.

## Phase 20: Scene Object And Selection Baseline

### 20.1 Scene Object Identity

Status: Complete.

Depends on:

- Complete: `packages/scene-core` now owns the runtime `EntityId` and local transform baseline.

Scope:

- Scene/world owns entity/object identity and transform.
- Editor consumes ids, not mutable pointers.

Validation:

- `asharia-scene-core-smoke-tests` covers create/destroy, stale id rejection and transform movement keeping
  `EntityId` stable.
- `asharia::scene_core` links only `asharia::core`; editor, renderer, ImGui, GLFW and Vulkan stay out of the
  scene baseline.

### 20.2 Editor-core Extraction Point

Status: Blocked.

Depends on:

- Real selection or transaction consumers.

Scope:

- Extract owned id/action/event/panel metadata where it is backend-neutral.
- Keep ImGui panel adapters and texture registry in `apps/editor`.

Validation:

- `packages/editor-core` CMake target does not link ImGui, Vulkan, GLFW or renderer implementation.

### 20.3 Selection Model

Status: Blocked.

Scope:

- Add `SelectionSet` in editor-core.
- Store `EntityId` and editor-only selection metadata.
- Emit `SelectionChanged`.

Validation:

- CPU-only selection tests or smoke cover add/remove/clear and deleted entity cleanup.

### 20.4 Transaction Skeleton

Status: Blocked.

Scope:

- Add command/transaction path for editor mutations.
- Inspector/gizmo future edits must go through transaction.

Validation:

- `--smoke-editor-transaction-transform` once transform editing exists.

### 20.5 Inspector Data Model Stub

Status: Blocked.

Scope:

- Define data model for property display and mixed values.
- Do not build full Inspector UI before schema/scene metadata is ready.

Validation:

- Model can represent single and multi-selection fields without ImGui dependency.

## Phase 21: Scene View Tools

### 21.1 Scene View Flags

Status: Done.

Scope:

- Add viewport flags for Scene View authoring overlays and explicit Game View debug overlays.
- Flags become render packet or RenderView inputs.

Implementation:

- `EditorViewportOverlayFlags` now lives in `apps/editor/src/editor_viewport.hpp` as backend-neutral editor viewport metadata.
- Scene View requests default grid、transform gizmo and selection-outline intent; wire、debug overlay and debug gizmo default off.
- `EditorViewportCoordinator` computes effective flags, strips Scene-only authoring flags from Game/Preview requests and
  allows Game View to retain explicitly requested debug overlay/debug gizmo flags for runtime diagnostics.
- `ImGuiTextureRegistry` carries the effective flags with the presented texture result so panel-facing metadata stays aligned
  with the sampled viewport texture.

Validation:

- `--smoke-editor-viewport` validates defaults, verifies Scene flags are retained, verifies Game clears Scene-only authoring
  flags while retaining explicit debug overlay/debug gizmo flags, verifies Preview effective flags are empty and checks at
  least one flagged Scene View frame was rendered.

### 21.2 Viewport Overlay Metadata Loop

Status: Done.

Scope:

- Keep Scene/Game/Preview overlay intent backend-neutral in editor code.
- Prove the request/result path carries effective overlay metadata back to panel-facing texture results.
- Do not connect Scene View grid rendering until render view prerequisites are explicit.

Implementation:

- `EditorViewportCoordinator` now counts both flagged render submissions and flagged texture acquisitions, so the editor smoke
  covers the full panel request -> viewport render -> ImGui texture result loop.
- `asharia-editor` now depends on `asharia-renderer-basic-shaders`, so editor-only builds regenerate the renderer shader
  artifacts required by `BasicFullscreenTextureRenderer`.
- Renderer-basic still receives only `BasicRenderViewDesc::target` for this slice; Scene View grid intent remains editor
  metadata until render-side camera, blending and pass semantics are ready.

Validation:

- `--smoke-editor-viewport` verifies Scene View defaults, Scene/Game/Preview filtering, at least one flagged Scene View render
  and at least one flagged texture acquisition by the panel-facing result path.
- `--smoke-editor-viewport-resize` continues to cover descriptor retirement and render-target deferred destruction.

### 21.3 Gizmo Interaction

Status: Blocked.

Scope:

- Add transform gizmo input flow.
- Continuous drag merges into one transaction.

Validation:

- Mouse capture respects ImGui capture flags and hovered Scene View.

### 21.4 Selection Outline

Status: Blocked.

Scope:

- Express selected objects through render packet flags or overlay packet.
- Add Scene View-only outline pass.

Validation:

- Selected state affects Scene View only.

### 21.5 Debug Draw Overlay

Status: Blocked.

Scope:

- Add minimal debug line/shape packet.
- Render only in Scene/debug views.

Validation:

- Debug draw packet is data-only and does not capture editor object pointers.

## Phase 24: Asset Browser And Material Editor

### 24.1 Asset Catalog View

Status: Deferred.

Depends on:

- `asset-core` GUID, metadata, product/cache baseline.

Scope:

- Editor displays asset catalog through public asset API.
- No direct source path mutation from UI.

Validation:

- Asset Browser can list known assets and diagnostics from asset-core.

### 24.2 Import Settings Editing

Status: Deferred.

Scope:

- Editing import settings creates editor command and reimport request.
- Product cache is updated by asset pipeline, not UI code.

Validation:

- Editor-only import settings do not enter runtime manifest.

### 24.3 Material Editor Data Path

Status: Deferred.

Depends on:

- Material resource signature and descriptor contract.

Scope:

- Editor edits material params and texture slots through material API.
- Renderer receives material/resource handle and pipeline key only.

Validation:

- Material mismatch diagnostics are available without editor.

### 24.4 Preview View

Status: Deferred.

Scope:

- Material/asset preview uses RenderView kind `Preview`.
- It reuses renderer caches and has view-local resources.

Validation:

- Preview View does not create editor-specific renderer path.

### 24.5 Asset/Material UI Smoke

Status: Deferred.

Scope:

- Add smoke for asset list and one editable material when owning systems exist.

Validation:

- Smoke can run headless enough for CI or deterministic local gate.

## Phase 28: Play Session And Diagnostics

### 28.1 Edit/Game State Machine

Status: Deferred.

Scope:

- App/editor/scene layer owns Edit Mode, Entering Play, Play Mode, Paused and Exiting Play.
- Runtime world is cloned or loaded separately from edit world.

Validation:

- Enter/exit Play does not mutate edit scene unless explicitly applied.

### 28.2 Game View Panel

Status: Deferred.

Scope:

- Add Game View panel using RenderView kind `Game`.
- Scene View and Game View can coexist in one editor frame.

Validation:

- Game View graph does not include editor-only Scene View passes.

### 28.3 Multi-view Profiling Labels

Status: Deferred.

Scope:

- Profiling/debug output is tagged by view id/kind.
- Editor diagnostics can show CPU/GPU timing per view.

Validation:

- Existing performance profiling docs remain the authority for metric ownership.

### 28.4 Editor Diagnostics Panel

Status: Deferred.

Scope:

- Show editor host diagnostics such as panel state, action invocations, texture registry lifetime and input capture state.
- Keep game performance panel separate from editor diagnostics.

Validation:

- Diagnostics panel does not become required for runtime profiling.

### 28.5 Play Session Smoke

Status: Deferred.

Scope:

- Add smoke for enter/exit Play, Game View render and edit scene preservation.

Validation:

- Play Session smoke confirms Game View and Scene View can render in the same frame without shared mutable view-local state.

## Recommended Next Commits

Current completed slices:

- `feat: add render graph diagnostics snapshot`: `RenderGraph::diagnosticsSnapshot()` creates a structured, backend-neutral
  snapshot from `RenderGraphCompileResult`: pass nodes, image/buffer resource nodes, pass-resource access edges, dependency
  edges, transitions, culled passes, command counts and transient lifetime data. It is validated by `--smoke-rendergraph` and
  package-local rendergraph compile tests.
- `feat: add render view diagnostics snapshot`: `BasicRenderViewDesc` can collect a `BasicRenderViewDiagnostics` snapshot from
  each successful `recordViewFrame()` call. `EditorViewportCoordinator` stores the latest view-local diagnostics and
  `--smoke-editor-viewport` verifies the expected pass/resource/access/dependency/transition counts.

1. `feat: add frame debugger capture state`

Add an editor-controlled capture workflow: `Running -> CaptureRequested -> CapturingFrame -> WaitingGpuFence ->
PausedFrameDebug -> Resume`. Paused frame debug freezes snapshot inspection and frame progression; it does not use
`vkDeviceWaitIdle` as the normal mechanism and does not read transient GPU resources unless a later explicit preview/readback
path is added.

2. `feat: add render graph viewer panel`

Add a read-only editor panel with an RG View matrix first: views, compiled passes, resources, dependencies, transitions and
details for the selected pass/resource. The pass node graph can appear as a second tab derived from the same snapshot.

3. `feat: add render view overlay prerequisites`

Prepare renderer-owned view data before connecting Scene View grid: camera/view/projection params, explicit overlay pass
load/store behavior, blend state support or an equivalent overlay composition path, and a narrow debug/world-line draw route.
Then add a camera-aware world grid as the first Scene View-only render pass; no gizmo interaction, picking or selection outline
in that slice.

## Non-goals

- No generic UI abstraction layer.
- No editor package extraction before action/event/panel/selection/transaction state has real consumers.
- No inspector, asset browser, material editor or Play Mode before their owning systems exist.
- No direct Vulkan command recording in panel `draw()`.
- No editor-only pass in Game View graph.
- No editable RenderGraph/node authoring UI in Frame Debug or RG View.
- No pass intermediate texture preview before explicit debug preservation or copy/readback is designed.
- No script-owned editor shell or raw ImGui scripting before action, transaction, schema and script diagnostics are stable.
