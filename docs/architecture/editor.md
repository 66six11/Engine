# Editor 架构

更新日期：2026-08-13

本文记录当前 `apps/editor` 的真实架构边界。它描述已经落地的 editor host、ImGui
integration、panel/action/event、Scene View viewport、input/shortcut routing、ImGui texture registry 和验证入口。
近期阶段顺序见 `docs/planning/next-development-plan.md`；脚本扩展和 C++/脚本协作边界见
`docs/architecture/editor-ui-scripting.md`；完整 package、managed host、hot reload 和 native bridge
方向见 `docs/architecture/managed-extension-model.md` 与 `docs/planning/system-architecture-roadmap.md`。

## 目的

`apps/editor` 是 Dear ImGui editor host。它负责把窗口、Vulkan frame loop、ImGui shell、
editor panels 和 renderer sampled output 组合成一个可运行的 editor executable：
`asharia-editor`。

Editor 不是 engine core，也不是 renderer owner。Renderer、RHI 和 RenderGraph 不依赖 editor；
runtime app 不链接 editor UI；未来 `packages/systems/editor` 内部 `editor_domain` target 只承载 backend-neutral editor state。

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
- `asharia::scene_core`
- `asharia::archive`
- `asharia::asset_core`
- `asharia::asset_core_io`
- `asharia::asset_pipeline`
- `asharia::editor_content`
- `asharia::project_core_io`
- `asharia::shader_slang`
- `imgui::imgui`
- ImGui GLFW/Vulkan backend source files from the Conan ImGui package

这些依赖属于当前 `apps/editor` host executable 的集成边界。Editor app 可以组合 public project/asset/pipeline
API 来加载项目描述、读取 `.ameta`、构造只读 catalog snapshot、生成 report 和记录 pending reimport facts，并可以消费
`scene-core` 的 `EntityId` 作为 editor selection value；这不表示已经存在可复用的 `editor_domain` target，也不表示 editor
panel 可以拥有 importer execution、product cache writes、runtime asset handles、runtime scene hierarchy 或
renderer/GPU lifetime。

禁止方向：

- `engine/core`、runtime packages、renderer packages 不依赖 `apps/editor`。
- 未来 `editor_domain` target 不 include ImGui、GLFW、Vulkan 或 renderer implementation headers。
- Editor panels 不 include 任何 package 的 `src/`，也不访问 Vulkan object ownership。

## 模块所有权

| 模块 | 拥有 | 不能拥有 |
| --- | --- | --- |
| `editor_i18n` | editor-local text catalog, locale selection and stable ImGui label formatting | runtime localization, asset text localization or renderer-facing strings |
| `editor_ui` | small editor-local ImGui style primitives, built-in editor theme tokens and component preview helpers used by panels | a generic UI framework or runtime-facing widget abstraction |
| `editor_settings` | editor-local user settings persistence plus runtime editor locale/theme switching | scene data, asset import settings or runtime/game configuration |
| `editor_app_config` | editor run paths, smoke layout/settings isolation, i18n resource directory and locale environment parsing | service aggregation, panel registry ownership or GPU/window lifecycle |
| `editor_vulkan_host` | editor window renderability wait, Vulkan context/frame-loop creation, swapchain extent readiness and one-frame RenderView/ImGui submission glue | panel registry ownership、action dispatch、persistent editor state or generic RHI abstraction |
| `editor_loop_host` | main editor loop, per-frame frame-context construction, ImGui frame begin/end order, input/shortcut routing and smoke loop state | app service lifetime、window/GPU object creation、shutdown order or broad service aggregation |
| `editor_shell_host` | per-frame shell capability context adaptation and panel draw dispatch | app service lifetime、renderer command recording、persistent editor state or broad service aggregation |
| `editor_app` | startup orchestration、service lifetime、startup smoke gates and shutdown order | main loop internals、shell capability adaptation、panel widget details becoming feature-specific renderer logic、low-level Vulkan frame submission helpers |
| `imgui_runtime` | ImGui context、GLFW backend、Vulkan backend lifecycle and the editor ImGui fragment shader contract | panel registry、editor state、viewport target ownership |
| `editor_workspace` | active editor workspace preset, dock slot list, layout reset request state | ImGui DockBuilder calls, saved scene/layout data, panel widget drawing |
| `editor_dock_layout` | translating workspace dock presets into Dear ImGui DockBuilder nodes | editor tool behavior, panel content, renderer or viewport ownership |
| `editor_tool` | tool descriptors and contributions to panels, actions, toolbar slots and viewport overlays | panel factories, command execution, viewport rendering or persistent document state |
| `editor_tool_manager` | editor-local active tool state, per-viewport primary tool selection and activate/deactivate lifecycle | renderer pass policy, Vulkan resources, panel factories or persistent scene/asset mutation |
| `asharia::editor_content` | shared UI-neutral read-only project catalog query、source-root/path/navigation helpers composed from public `project-core` / `asset-pipeline` / `asset-core` APIs | ImGui/Avalonia UI、filesystem watcher、importer execution、product manifest/blob writes、runtime asset handles or GPU resources |
| `EditorAssetCatalogStore` / report | ImGui-host fixture/current snapshot selection、deterministic text/icon report与frame-context handoff | shared query truth、watcher、runtime/GPU resource |
| `editor_asset_import_settings_command` | undoable `.ameta` import-setting edits plus editor-owned reimport request/pending facts | import scheduling, product cache mutation, catalog truth, runtime loading or preview texture allocation |
| `editor_asset_icon` | editor-owned Lucide icon ids, asset icon query descriptors, custom resolver registry and ImGui glyph rendering | plugin-owned SVG injection, source scanning, texture/Vulkan ownership or runtime asset loading |
| `imgui_editor_shell` | dockspace host, main menu, command bar, status bar and action menu binding through shell-local capability contexts | renderer command recording、panel object ownership、hard-coded tool layout policy |
| `editor_panel` | panel descriptor/state、singleton panel registry、focus/open/close lifecycle | ImGui backend setup、Vulkan resource lifetime |
| `editor_action` | action descriptor、enabled state、callback invocation、stable action ids and action-only service bundle | command transaction semantics before transaction exists、full app service access |
| `editor_event` | frame-local typed event queue, event metadata, severity/outcome labels and diagnostics history sink | global EventBus、durable document storage、panel/world pointers or Vulkan resources |
| `editor_selection` | app-local active `SelectionSet` owner, stable `sceneId + EntityId` values, empty/missing/stale/multi-selection shapes and `SelectionChanged` facts | runtime scene hierarchy ownership, scene serialization, object pointers, picking, gizmo state or writable Inspector data |
| `editor_dirty_state` | app-local dirty-state owner and snapshot for transient UI, document dirty, asset metadata dirty and pending reimport facts | autosave, source control, importer execution, product cache writes, runtime scene serialization or writable Inspector fields |
| `editor_inspector_model` | app-local data-only Inspector sections, rows, display values, mixed-value placeholders and validation messages | ImGui widgets, runtime mutable object pointers, scene serialization, dirty state or writable component editing |
| `editor_input_router` | ImGui capture snapshot、Scene View hover/focus state、derived viewport/shortcut input flags | raw GLFW callback ownership、camera/gizmo behavior |
| `editor_shortcut_router` | shortcut metadata parsing、ImGui shortcut polling、input-gated action invocation | command transaction semantics、raw GLFW callback ownership |
| `editor_viewport_tool_state` | app-local Scene View active tool、transform space、pivot mode、snap、overlay visibility、view mode、edit/play preview state and `ViewportToolStateChanged` events | scene picking, transform gizmo execution, selection outline rendering, saved scene data or renderer pass ownership |
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
    load editor i18n catalog and editor settings
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

### Selection state

`EditorAppServices` owns the single active `EditorSelectionSet` for the app. The first slice keeps this owner in
`apps/editor` rather than extracting `editor_domain`; it is still backend-neutral and uses `asharia::EntityId` from
`scene-core` plus a scene/document key string, not runtime object pointers.

Selection mutations normalize invalid and duplicate targets, keep one primary item, preserve explicit `Resolved`,
`Missing` and `Stale` states, increment a revision, and emit a frame-local `SelectionChanged` event through
`EditorEventQueue`. The event metadata carries the selection revision, change reason, primary target label and count
summary; `EditorDiagnosticsLog` can record that fact the same way it records panel/action events.

Scene Tree and Inspector receive a read-only selection context from `EditorPanelRegistry::drawPanels()`. They can display
the same stable selection ids and missing/stale state without owning panel-local selection. They still do not perform scene
picking, tree hierarchy mutation, transform editing, selection outline rendering or Inspector field writes.

### Inspector data model

`EditorInspectorModel` is the first app-local Inspector data contract. It describes backend-neutral sections, rows, display
values, mixed-value placeholders and validation messages. The current Inspector panel builds the model from the
`EditorSelectionSet` snapshot plus command-history depths, then renders that model through ImGui; the panel no longer owns
the property-row data shape itself.

The first model is intentionally read-only. Empty selection, single selection, multi-selection mixed values and
missing/stale selection validation are represented explicitly so future scene/schema-backed properties can plug into the
same row contract. Writable Transform/component fields remain deferred until dirty state, validation and command/transaction
ownership can restore visible scene state through undo/redo.

### Dirty state

`EditorAppServices` owns one app-local `EditorDirtyState`. It exposes a read-only `EditorDirtySnapshot` that separates:

- transient UI dirty facts, such as layout or panel-local state that should not make a document unsaved;
- document dirty facts that will later be set by scene/schema-backed transactions;
- asset metadata dirty facts that belong to editor-owned metadata writes rather than runtime assets;
- pending reimport count, derived from `EditorAssetReimportPendingState`.

The shell updates pending reimport count from the existing pending state before drawing each frame. The status bar can
show Clean, Dirty, Pending reimport or Transient state, and Inspector can render a read-only Dirty State section from the
same snapshot. This is only a state contract: it does not save files, schedule importers, refresh catalog truth, own source
control state, run autosave or make Inspector fields writable.

`EditorDirtyState` can optionally bind to the frame-local `EditorEventQueue`. Real dirty-state changes emit
`DirtyStateChanged` with revision, bucket label and subject id metadata; duplicate/no-op updates do not emit an event.

### State event contract

`EditorEvent` is still a frame-local fact queue, not a durable bus. Events now share a small metadata shape: revision,
subject id, label, message, severity and outcome. Existing panel/action events may leave metadata empty; state events use it
so Selection, Dirty State, Command History and validation facts can be correlated in Log/Console, status diagnostics and
future Inspector validation without panel-local hidden coupling.

Current state event kinds:

- `SelectionChanged`: emitted by `EditorSelectionSet` with selection revision/reason/count metadata.
- `DirtyStateChanged`: emitted by `EditorDirtyState` for real dirty bucket changes.
- `CommandHistoryChanged`: emitted by `EditorCommandHistory` for push/undo/redo/clear success and undo/redo failure facts.
- `ViewportToolStateChanged`: emitted by `EditorViewportToolState` when Scene View mode/tool state changes, or when a pending
  tool activation is rejected as a no-op warning.
- `ValidationReported`: reserved for row/object validation facts; this slice defines the diagnostics route but does not
  turn the read-only Inspector model into writable validation fixing UI.

Events do not own scene objects, asset products, ImGui widgets, Vulkan resources or saved document data.

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
  map editor flags to BasicRenderViewDesc view/camera/frame/overlay contract
  BasicFullscreenTextureRenderer::recordViewFrame()
  ImGuiTextureRegistry::registerOrUpdate(sampled texture view + flag metadata)
  keep pending/presented/retired viewport texture state
```

The display is intentionally one frame delayed. This keeps panel drawing simple and avoids two-phase panel rendering until
same-frame presentation is required and measured.

### Studio Viewport Presentation Transaction boundary

The Avalonia Studio Scene View uses the same native renderer through a separate external-presentation path. Its production
abstraction is a reusable `Viewport Presentation Transaction`, not a dock-only operation. Every endpoint owns
its front/candidate surfaces, stream/import state and retirement. Each participant validates
`SessionId + EndpointEpoch + TransactionId`: the transaction id is shared by the group, while session and epoch bind that endpoint
to one content session and one attach/compositor lifetime; request sequence, target revision and geometry or
capture identity provide the finer content gate.

The group lifecycle is `Proposal → Preparing → Prepared → Validated → Published → Rendered → Retiring → Completed`.
Recoverable pre-publish failure/cancellation becomes `Aborted`; an ambiguous result after publish becomes `Quarantined` with
resource ownership retained. All participants must be prepared and validated before publish. Participants under the same
Avalonia compositor can apply their state/layout mutation and every `visual.Surface`/`Size` switch in one UI turn and share one
composition-batch `Rendered` barrier. Different compositors have no common commit barrier and are explicitly non-atomic; they
must use separate transactions.

Scene endpoints require an exact panel extent. Game Preview endpoints can freeze an independent fit policy in their proposal.
Frame Debugger immutable captures use an independent endpoint and capture identity, so inspecting a frozen frame cannot replace
the mutable Scene/Game front. The endpoint policy is therefore separate from the transaction state machine.

`EditorDockStagedGridSplitter`, `EditorDockSplitResizePolicy` and `EditorDockSplitResizeCoordinator` are only a layout-proposal
adapter. They translate drag input through min/max/layout-rounding rules, synchronously probe the prospective exact `PixelSize`,
and restore the committed `GridLength` before yielding. The transaction coordinator prepares endpoint-owned candidate surfaces,
then publishes the committed `GridLength` and all same-compositor surface switches together while `Opacity` remains 1. A fault,
cancellation, stale identity or any participant mismatch aborts before publish and preserves every old front. Replaced fronts
begin retirement only after the shared batch reports `Rendered`. A→B→A requires a new transaction and cannot revive an old
snapshot. Plain `GridSplitter.ShowsPreview` and drag-end debounce remain rejected because they do not produce unique exact
geometry during the drag.

Main and Floating Windows share `EditorDockPresentationLayoutHost` around their dock workspace. The shared host implements the
platform-neutral `IInteractiveTopLevelResizeSink` and obtains an optional `IInteractiveTopLevelResizeAdapterFactory` through the
application composition root. `IInteractiveTopLevelResizeAdapterProvider`, `IInteractiveTopLevelResizeAdapterFactory`,
`IInteractiveTopLevelResizeAttachment`, `IInteractiveTopLevelResizeSink`, `IInteractiveTopLevelResizeCommit` and
`InteractiveTopLevelResizeProjection` live in `Asharia.Studio.Presentation.Avalonia.Windowing`; the shared host, transaction
coordinator and endpoint control contain no HWND, WM-message, USER32 or P/Invoke surface.

The independent `Asharia.Studio.Presentation.Avalonia.Windows` integration owns the native hook. Its
`Win32InteractiveTopLevelResizeAdapterFactory` has one deliberately narrow precommit scope: an ordinary decorated border drag within
one fixed-DPI epoch. `WM_SIZING` copies the proposed screen-space `RECT`, writes the last accepted exact `RECT` back to USER32 and
coalesces work outside WndProc. The host keeps at most one active workspace request and one queued latest, probes all visible exact
viewport endpoints while the old HWND/layout/front remain committed, and prepares their candidate surfaces. The publish turn applies
the platform-neutral outer commit; only the Windows integration calls `SetWindowPos`, after which the host calls
`TopLevel.UpdateLayout`, revalidates the actual workspace/endpoint extents and switches the same-compositor fronts. Only a successful
`Published` result advances the accepted HWND `RECT`. Pre-publish failure or an invalid interaction/DPI/chrome/endpoint epoch preserves
or restores the old committed state. WndProc itself never waits, renders or walks the visual tree.

`WM_EXITSIZEMOVE` closes the interaction epoch. Every outer commit not yet accepted becomes stale, the queued successor is discarded,
and the shared host rejects it when `IsCurrent()` is false. An active candidate already inside native render, GPU or consumer work is
not synchronously killed; it completes and then follows the normal pre-publish abort/work-fence retirement path. The Window stays at
the last Published exact `RECT` instead of applying the raw cursor-final proposal after release. The accepted final may therefore lag
raw final by 0–1 candidate, and diagnostics must report both values plus pixel/logical lag. Avoiding a post-release catch-up
`SetWindowPos` removes that additional grow-gap/shrink-crop transition, but does not make drag-time USER32/DWM and Avalonia commits
physically atomic.

The V7 native stream still keeps at most one executing request, one latest pending replacement and one ready frame per
viewport, with at most three persistent full presentation slots. Each slot keeps its external image, producer/consumer
semaphores, command resources and retirement proof together across frames. The old three-slot steady front plus the one-frame
candidate stay within the process-wide four-resource cap; Realtime prefill resumes only after the prepared switch. Snap,
maximize/restore, programmatic Window/Bounds resize, DPI/cross-monitor transitions, non-Windows top-levels without the capability and
other geometry sources without a precommit seam remain on the unchanged exact-only hidden fallback: they hide the mismatched front
until a new exact frame is ready, never crop or stretch, but can show a blank Scene interval and have not reached zero-flash acceptance.

The Win32 publish turn is not a physical atomicity claim. USER32/DWM top-level geometry and an Avalonia composition batch have no
shared public scanout fence during drag. A separate Windows-only opt-in WGC acceptance project observes corner sentinels in
`wgc-dwm-composited-pixels`; its release capture window requires every WGC-delivered sample after the epoch closes to match the final
accepted/Published exact extent, with no gap, crop, stretch, blank or spill. WGC-delivered samples are not a lossless record of every
DWM refresh and are not LCD scanout evidence, so `PhysicalDisplayedEvidenceAvailable` remains `false`. The checked Unreal public
threaded-rendering contract and Unity public SceneView source/API expose no native-top-level-plus-viewport physical transaction
precedent. Asharia adopts their immutable render ownership and repaint/invalidation separation, but rejects copying their APIs or
placing platform hooks in shared presentation; the capability plus independent integration is an Asharia-local, package-first boundary.

`IsRealtime=true` is the explicit default and keeps producing exact frames for a static scene at the >=60 FPS acceptance
floor. `false` is an explicit OnDemand mode: the session emits one coalesced refresh request when target, camera, extent or
exposure changes, hidden dock tabs stop frame admission, and re-exposure or a newly attached surface requests an exact frame.
Target/camera/exposure changes also advance a managed content-sequence fence so an older snapshot cannot cross the surface
commit; extent freshness remains owned solely by the exact geometry generation. Removing or replacing a session hides its old
surface and retires both active and desired streams before the replacement can present. The runtime's frame index is a
render-attempt identity (failed attempts may leave gaps), while shader/preview time comes from a monotonic runtime clock and is
never synthesized as `frameIndex / 60`. The 2026-08-09 pre-split combined Studio splitter run completed 90/90 unique exact generations at
108.25/s, with proposal-to-shared-batch-`Rendered` p95 about 12.59 ms, requested-mismatch hidden duty 0, and subsequent
Realtime steady surface-update cadence 222.84 FPS. These are application/Avalonia surface facts, not physical scanout facts;
they are also not Win32 outer-Window pixel evidence. Physical display cadence remains separate from both PresentMon top-level timing
and WGC DWM-composited pixels. The WGC opt-in entry is implemented, but only a successful full run may be recorded as current pixel
evidence. The detailed contracts are maintained in
[`apps/studio/docs/architecture/viewport-rendering.md`](../../apps/studio/docs/architecture/viewport-rendering.md) and
[`apps/studio/docs/adr/0006-viewport-interactive-resize.md`](../../apps/studio/docs/adr/0006-viewport-interactive-resize.md).

Scene View overlay state remains editor-owned until the coordinator translates it into renderer-owned data. Renderer-facing
`BasicRenderViewDesc` uses `BasicRenderViewKind`, camera matrices, per-view frame params, explicit overlay color load/store
and blend policy, plus a data-only debug world-line span. It does not use `EditorViewportKind`,
`EditorViewportOverlayFlags`, ImGui ids or Vulkan handles from panels. Grid, gizmo and debug draw passes must consume this
contract in later slices instead of reading editor panel state directly.

The Studio V7 request carries a view-local FOV axis but does not yet carry selection or mutable overlay intent. Those features require an explicit immutable
view-state snapshot/revision before they can participate in the content fence; document revision or a managed invalidation bit
must not be used as a substitute.

## 生命周期

### Viewport target 生命周期

`EditorViewportCoordinator` owns the editor viewport target state:

- Viewport state is keyed by `panelId + EditorViewportKind`.
- Each keyed slot owns the last presented texture safe for panels to draw and a pending texture that receives a newly
  rendered or resized target.
- Each keyed slot stores its latest `EditorRecordedRenderViewDiagnostics` snapshot so Live RG / smoke validation can inspect
  a specific Scene/Game/Preview view without relying on a global "last request wins" value.
- `retiredTextures_` holds replaced targets until they are deferred through the frame loop.

旧 render target 通过 `VulkanFrameRecordContext::deferDeletion()` 销毁，让 frame loop 用 fence/epoch
约束 GPU 使用。Resize 不能立刻销毁当前正在呈现的 target。

### ImGui descriptor 生命周期

`ImGuiTextureRegistry` owns ImGui descriptor registration:

- `registerOrUpdate()` calls `ImGui_ImplVulkan_AddTexture()`.
- `acquireForDraw()` records the submitted frame epoch that may reference the descriptor.
- `collectGarbage(completedFrameEpoch)` calls `ImGui_ImplVulkan_RemoveTexture()` only after the frame loop reports the
  relevant submitted frame complete.
- Descriptor owner keys are internal registry keys; viewport results still return the panel-facing `EditorId`. This lets a
  future panel host Scene/Game/Preview textures without descriptor-key collisions.

The registry does not own the underlying image or image view. It only owns ImGui's descriptor handle and retirement state.

### ImGui workspace and layout persistence

`ImGuiRuntime` owns Dear ImGui layout persistence. It resolves a user-local `imgui-layout.ini` path under the editor app
state directory, assigns `ImGuiIO::IniFilename` during ImGui context creation and flushes the layout before ImGui shutdown.
This state stores editor window/docking layout only; it is not scene data, asset data or runtime configuration.

`EditorWorkspaceController` owns the current editor workspace preset and transient layout reset requests. The default
workspace describes the dock slots for the internal Scene Tree, Scene View, Inspector, Live RG View, Frame Debugger,
Asset Browser, UI Style Preview, Editor Settings and Log panels. The Unity-like workbench baseline keeps those stable
panel ids, but the visible default labels are Hierarchy, Scene View, Inspector, Project and Console.
`editor_dock_layout` is the only editor module that calls ImGui DockBuilder APIs; the shell asks it to apply the active
workspace when no dock node exists or when `View > Reset Layout` requests a reset. This keeps future layout presets and tool
contributions out of panel widget code.

`EditorExtensionRegistry` is the first manifest-like owner for built-in editor tool contributions. It currently validates
extension stable ids, rejects duplicate tool ids during a reload-style replace, and publishes tool contributions to
`EditorToolRegistry`. It does not load external JSON/script packages yet, and it does not own panel factories or action
callbacks. Those remain in `EditorPanelRegistry` and `EditorActionRegistry`.

`EditorToolRegistry` records the published tool view: panels, actions, toolbar buttons, viewport overlay intents and
viewport activation metadata. It does not own panel factories or invoke commands. The command bar is generated from tool
toolbar contributions, so future tools can shape the editor chrome without adding more hard-coded button lists to
`imgui_editor_shell`.
Viewport overlay contributions are queried by viewport id through `visitViewportOverlays()`. Scene View uses that query to
draw its compact overlay strip over the sampled viewport while keeping Grid/Gizmo/Select overlay ids tool-owned. Only Grid
is enabled until the pending Gizmo/Select ids have real selection/provider/render bridge consumers.

`EditorToolManager` owns the editor-only lifecycle state for those registered tools. It syncs from `EditorToolRegistry`,
tracks one primary active tool per viewport, rejects activation when the tool did not declare support for that viewport,
exposes begin/complete activation and deactivation states, and marks missing tools as `Unregistered` during reload-style
sync. It does not execute tool behavior, mutate scene data, draw panel contents or decide renderer pass insertion; those
remain command/transaction, panel, viewport coordinator and renderer-owned responsibilities.

### ImGui theme

`editor_ui` owns the editor-local Dear ImGui style tokens, compact editor metrics and the built-in editor theme catalog.
The default theme is `unity-6-dark` (Unity 6 Dark); `black-default` (Black Default), `classic-blue-gray-2` and the other
legacy built-in themes remain available, and the legacy `classic-blue-gray` settings value is still accepted as an alias.
`ImGuiRuntime::create()` applies the startup theme from editor settings, and runtime theme changes are applied through
`EditorSettingsController`. This is editor shell presentation state only; renderer, RHI and runtime packages do not depend
on theme colors, rounding values, compact metrics or component preview helpers.

`editor_ui_widgets` owns small shared ImGui drawing helpers for the Unity-like workbench baseline: compact panel headers,
component section headers, toolbar toggles, search fields, property rows and status chips. These helpers are intentionally
presentation primitives, not a generic UI toolkit or a scene editing data model.

Theme colors are authored as display-referred sRGB bytes. `EditorUiTheme` stores `ColorSrgba8` values such as `#171D24`;
it does not store `ImVec4` or linear floats. `editor_ui` converts those bytes to encoded sRGB `ImVec4` / `ImU32` values only
at the Dear ImGui adapter boundary. Helper names use `EncodedSrgb` to make this transport contract explicit.

The editor ImGui Vulkan pass always expects linear shader output. ImGui vertex colors are transported as encoded sRGB
8-bit values, and `apps/editor/shaders/imgui_srgb_color.slang` decodes vertex `rgb` to linear in the fragment shader before
writing to the swapchain or an LDR editor target. The final encode is handled by an `_SRGB` color attachment or a later
presentation pass; the decode switch is therefore tied to the UI pass output contract, not just to the current target
format.

Texture color space is tracked separately from vertex color. `ImGuiTextureRegistry` records `EditorUiTextureColorSpace`
metadata for registered editor viewport and preview textures. Color images that are authored/stored as sRGB must be exposed
through `_SRGB` image views so Vulkan sampling linearizes `rgb`; linear render textures, alpha coverage textures, masks,
data and debug textures must keep linear/UNORM/FLOAT semantics. The ImGui fragment shader assumes sampled `texel.rgb` is
already in the pass working space.

### Editor i18n

`editor_i18n` owns the first editor-local text catalog. The catalog is key-based, loaded from
`apps/editor/resources/i18n/*.json`, and currently covers `en-US` and `zh-Hans` for menus, panel titles and the core Scene
View / Console / RG View / Frame Debug labels. The current catalog deliberately maps the internal Scene Tree, Asset Browser
and Log panels to Unity-like visible names Hierarchy, Project and Console without renaming their stable ids. It is
deliberately scoped to `apps/editor`; runtime, renderer and asset text localization are separate future concerns.

Dear ImGui labels must preserve stable IDs when visible text changes. Editor UI code should use `EditorI18n::label()` for
menus, actions, panel windows and other stateful controls so labels are emitted as `translated text###stable-id`. This keeps
layout ini, docking state and widget identity stable across locale changes.

`editor_settings` persists the interactive editor locale and UI theme in a user-local `settings.json` beside the ImGui
layout state. `ASHARIA_EDITOR_LOCALE` remains a startup fallback when no saved setting exists. The Editor Settings panel
switches locale and theme at runtime through `EditorSettingsController`, updates the active `EditorI18n` service or ImGui
style, and saves the setting immediately.

`ImGuiRuntime` requests CJK glyph coverage during editor startup so runtime switches to `zh-Hans` do not require rebuilding
the ImGui font atlas. It uses `ASHARIA_EDITOR_CJK_FONT` or a small list of common system font locations. This keeps the
first localization path usable during development, but bundled editor font assets and license-reviewed packaging remain a
later distribution task.

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

- Use the capability groups on `EditorFrameContext` (`ui`, `diagnostics`, `settings`, `tools`,
  `input`, `renderGraph`, `viewport`) instead of adding new flat service-locator fields.
- Panel `draw()` implementations should immediately adapt the frame context into a panel-local
  context before calling helpers. Panel-local helpers should accept the smallest capability group
  they need; keep the top-level `ImGuiEditorPanel` virtual entry point as the adapter boundary until
  the panel API is narrowed further.
- Declare category and preferred dock metadata in `EditorPanelDesc`; workspace presets can use that metadata or explicitly
  list panel ids for default layouts.
- Keep persistent scene/asset edits out of `draw()` until transactions exist.
- Do not allocate ImGui Vulkan textures directly.
- Do not record Vulkan commands.
- Report hover/focus state to `EditorInputRouter` instead of making global input routing decisions locally.
- Reuse `editor_ui` helpers for repeated editor styling primitives, but do not hide raw ImGui behind a broad widget clone.

### Tool 扩展

Add built-in tool metadata through `EditorExtensionRegistry` manifest-like descriptors, then publish the tool view into
`EditorToolRegistry` after registering the tool's panels and actions. A tool may contribute panel ids, action ids, toolbar
slots, viewport overlay ids and viewport activation metadata. Contribution ids must point at existing panel and action
registries; overlay ids are editor-facing intent until a concrete viewport overlay renderer consumes them.

Tool rules:

- Do not execute actions or draw panel contents from the tool registry.
- Use `EditorToolManager` for active tool and lifecycle state; do not keep competing active-tool booleans in panels.
- Declare activation policy and activation viewport ids before a tool can become a viewport's primary active tool.
- Keep toolbar placement as metadata; the shell decides how toolbar slots are presented.
- Keep viewport overlay ids backend-neutral and map them to RenderView/debug draw inputs through the viewport coordinator.
- Query viewport overlays by viewport id when panel chrome needs controls; do not duplicate another tool's overlay list in a
  panel.
- Use `editor_i18n` keys for user-facing labels and keep technical names such as pass, resource and shader identifiers
  untranslated.

### Asset Browser / Icons

`AssetBrowserPanel` is the first shell for Phase 24. It is intentionally read-only and consumes an `asset-core`
`AssetCatalogView` supplied through its panel draw context. The panel keeps its transient filter/UI state locally; project
source scanning and product manifest reads belong to the editor-owned catalog snapshot service, not to panel draw code. The
panel registers as a normal panel/action/tool contribution and defaults to the right-bottom dock slot.

Icon ownership stays in `editor_asset_icon`:

- Panel code submits `EditorAssetIconQuery` values and draws the returned `EditorIconDescriptor`.
  Queries expose catalog-facing identity such as extension, asset type, importer id, diagnostic state, source path,
  display name, GUID text, import profile, asset role and sub-asset count; they do not expose filesystem scan internals or
  runtime resources.
- Panel rows come from public catalog view entries, not direct source tree scanning, import execution or product cache
  mutation.
- Asset Browser row selection, filter text, visible-row summary and selected-asset details are transient panel state. The
  detail pane re-reads `AssetCatalogViewEntry` metadata such as GUID, source path, type, importer, importer version, product
  counts, import profile, asset role, read-only sub-assets and row diagnostics; it may offer clipboard copy buttons for
  read-only identifiers, but it does not create runtime asset handles or editor commands.
- Text search matches catalog-facing metadata only: display name, source path, type, importer name, extension, GUID, product
  state, import profile, asset role, sub-assets and row diagnostics.
- Folder scope browsing is derived from `AssetCatalogViewEntry::sourcePath` only. It provides read-only source-path scope
  navigation and breadcrumbs for visible rows, but it does not enumerate the filesystem, watch directories or create folder
  assets.
- Asset type filtering is derived from visible catalog row metadata (`AssetCatalogViewEntry::assetTypeName`) and remains local
  panel state. It does not query importers, load assets or create editor/runtime type registries.
- Import profile filtering is derived from catalog row metadata (`AssetCatalogViewEntry::importProfileName`) after the current
  folder/type scope and remains local panel state. It makes texture roles such as Texture2D, SpriteSheet, TextureCube and
  Skybox browsable without deriving meaning from source file extensions.
- Product state filtering is derived from catalog row metadata (`AssetCatalogViewEntry::productState`) after the current
  folder/type/import-profile scope and remains local panel state. It does not trigger import/reimport, product-cache writes,
  resource loading or renderer preview creation.
- Asset table sorting is a local view over the visible `AssetCatalogViewEntry` rows. Sorting by name, type, import profile,
  importer or product state does not mutate catalog order, asset metadata, product records or project files.
- The Asset Browser main table displays import profile as a first-level column so source images with different semantics
  such as Texture2D, SpriteSheet, TextureCube or Skybox are browsable without opening the details pane.
- `packages/editor-content` 的 `asharia::editor_content` 现在组合 public `project-core`、`asset-pipeline` 和
  `asset-core` APIs，形成 UI-neutral read-only project snapshot。旧 app-private `editor_asset_catalog` query source
  已硬切删除；`apps/editor` 只保留 ImGui-owned store/fixture/icon/report/command consumer。该 package 不拥有 watcher、
  hot reload、import execution、runtime loading 或 renderer resources。
- Editor project snapshots explicitly sequence source scan, metadata discovery, source hashing and import planning instead
  of treating `planScannedAssetImports()` as the UI contract. Missing `.ameta` sidecars and orphan sidecars are editor
  warnings for Resource Browser visibility, while invalid roots, filesystem errors, invalid source paths and duplicate
  source/metadata paths remain errors.
- Source files that are scanned but do not produce a catalog source are appended as read-only `DefaultAsset` rows with
  product state `NotTracked`. They keep only source-path/display/extension facts plus a warning diagnostic; the editor does
  not invent a GUID, importer, product key or runtime resource for them.
- `EditorAssetCatalogStore` selects either the deterministic fixture catalog or a loaded project snapshot before the frame
  loop. `AssetBrowserPanel` consumes the catalog rows and snapshot diagnostics through its panel context.
- `EditorAssetCatalogStore` owns the current browser catalog view. It defaults to a deterministic fixture for development
  runs without a project and can be switched to a project snapshot at startup.
- `EditorFrameContext` passes the optional `EditorAssetCatalogSnapshot` pointer into the Asset Browser draw context. The
  panel uses it only to display current catalog source facts such as fixture/project mode, resolved project file, resolved
  product manifest path, target profile and source-root mappings; it does not mutate project descriptors, metadata or
  product cache state.
- `resolveEditorAssetCatalogSourceRoots()` and `resolveEditorAssetCatalogSourceRootForSourcePath()` expose the project
  descriptor's asset source roots as editor/reporting facts: root name, virtual source-path prefix, authored directory and
  resolved project-local directory. Asset Browser uses the same helper to show the loaded source roots and the selected
  row's matched root.
- `makeEditorAssetCatalogNavigationNodes()` builds a deterministic read-only navigation model from the same snapshot:
  source-root nodes, virtual folder nodes, asset nodes and sub-asset nodes. The model is catalog-derived and does not
  enumerate the filesystem or imply that sub-assets have standalone source files. Sub-asset nodes keep their own stable id
  and asset role, so sprite slices can resolve different icon/tooling policy from the parent sprite-sheet source asset.
- Snapshot-backed Asset Browser runs draw this navigation model as the left-to-right browser entry point: selecting
  source-root/folder nodes updates transient folder scope, and selecting asset/sub-asset nodes selects the parent catalog
  row for details. Sub-asset selection is kept as a local stable-id selection that drives a read-only detail section and
  copy affordance; it does not turn the slice into an independent source file, product row or runtime asset. Fixture-backed
  runs keep the older source-path folder controls so smoke and local UI development still work without project IO.
- `resolveEditorAssetCatalogSourceFilePath()` and `resolveEditorAssetCatalogMetadataFilePath()` derive physical source and
  `.ameta` paths from the loaded project descriptor and catalog row `sourcePath`. They are read-only helpers for UI/reporting
  and do not perform filesystem discovery, import execution or cache writes.
- Interactive runs may pass `--project <asharia.project.json|project-dir>`, optional
  `--product-manifest <products.aproducts.json>` and optional `--asset-target-profile <profile>` to load a real project
  snapshot. Directory input resolves to `asharia.project.json`; when no manifest is passed, the loader reads an existing
  project-default `.aproducts` manifest beside the generated asset cache root and otherwise leaves products missing.
  `ASHARIA_EDITOR_PROJECT`, optional
  `ASHARIA_EDITOR_PRODUCT_MANIFEST` and optional `ASHARIA_EDITOR_ASSET_TARGET_PROFILE` remain fallback/script entry points
  when CLI project options are absent. Regular editor smoke modes reject project-loading options and keep the deterministic
  fixture path; `--smoke-editor-asset-browser` loads a temporary snapshot-backed project catalog with material and
  texture-profile rows to prove the startup/frame-context route.
- `--check-project <asharia.project.json|project-dir>` runs the same read-only snapshot loader and prints row/diagnostic counts plus
  compact row/sub-asset metadata such as source path, type, importer, import profile, asset role and product state without
  opening a window or creating fixture data. It is the preferred first check for real project-path development.
- `--check-project-json <asharia.project.json|project-dir>` and
  `--check-project <asharia.project.json|project-dir> --json` run the same read-only path but emit deterministic JSON
  through the repository archive facade. The JSON report is intended for real project-path review logs and automation; it
  records resolved project/manifest paths, resolved source roots, navigation nodes, per-row matched source root and per-row
  source/metadata file paths, and does not create fixture data, execute importers, write product cache or load runtime
  resources. Each row and navigation node records the default resolved Lucide icon descriptor through the same icon query
  path used by the Asset Browser panel. Programmatic callers can pass an `EditorAssetIconRegistry` to resolve report icons
  with the same custom resolver/rule set used by the UI.
- Built-in fallback ids use Lucide vocabulary such as `lucide.folder`, `lucide.file`, `lucide.image`, `lucide.braces`,
  `lucide.palette`, `lucide.box`, `lucide.copy`, `lucide.x`, `lucide.circle-help` and `lucide.triangle-alert`.
- Custom providers can override by extension, asset type, importer id, diagnostic state, source path, display name, GUID,
  import profile, asset role or sub-asset count, but they only return stable ids, tint and tooltip metadata.
- Simple override policies can use `EditorAssetIconRule` instead of hand-written resolver lambdas. Empty rule fields are
  wildcards, while extension/import-profile/GUID fields match normalized values and `*Contains` fields match
  case-insensitive substrings.
- Asset Browser localizes descriptor tooltip keys through `EditorI18n` before drawing row and folder icons; custom providers
  should return stable tooltip keys plus fallback text, not pre-localized UI strings.
- Custom providers are registered through `EditorAssetIconRegistry`; resolver ids can be replaced or unregistered so future
  extension reload can update icon policy without recreating panel state.
- Empty icon ids and payload-like ids are invalid descriptor output. Rule registration rejects them, and resolver output is
  diagnosed and ignored so the row/report falls back to the built-in Lucide descriptor.
- Custom providers do not return raw SVG, ImGui callbacks, `ImTextureID`, Vulkan handles or renderer resources.
- Asset Browser UI state such as filter text, folder scope, type/import-profile/product-state filters, row selection,
  navigation selection and selected sub-asset stable id is transient panel state, not asset metadata, product cache state or
  project descriptor state. The clear-filters icon button resets only those local controls.
- `editor_asset_import_settings_command` owns the first narrow import-settings mutation contract. It creates undoable
  editor commands that read and rewrite a selected source `.ameta`, recompute the canonical settings hash, and record an
  editor-owned reimport request fact containing source GUID, source path, changed setting keys and target profile. It does
  not execute importers, refresh the catalog, write product manifests/blobs, allocate preview textures, upload GPU
  resources or make Asset Browser panel state persistent.
- The same module also owns the first pending reimport coordination state. It consumes command-produced request facts and
  coalesces them by source and target profile. Asset Browser rows can add a separate pending marker beside the catalog
  product-state pill, and selected details can show/clear that pending state for the current source/target profile by
  source GUID or source path, but the queue is still not product truth. Clearing pending state does not mutate `.ameta`,
  product manifests, product blobs, cache files, runtime resources or GPU objects.
- `EditorAssetReimportPendingState::snapshotPendingWork()` is the narrow future scheduler handoff contract. It returns a
  deterministic, read-only value list of pending work facts, sorted changed-setting keys and request counts; it does not
  schedule imports, refresh the catalog, invalidate products or allocate runtime resources.
- `refreshEditorAssetCatalogStore()` is the explicit editor-owned catalog refresh contract. It rebuilds the current
  snapshot from its original project/product-manifest/target-profile request and swaps the store view so metadata changes
  can be reflected as catalog facts. It does not consume pending reimport state, execute importers, write product
  manifests/blobs, mutate product cache files, allocate runtime resources or upload GPU textures.
- The Asset Browser Import Settings section is a command producer for the current selected texture row. Its profile combo
  edits only `texture.profile` through `EditorTransaction`, records pending reimport when the metadata changed, and leaves
  invoking catalog refresh/import execution/product-cache writes to explicit editor/pipeline service slices.
- After a successful metadata command, the Import Settings UI may read the current canonical `texture.profile` back from the
  selected row's `.ameta` to keep the visible draft/baseline aligned with execute, undo and redo. This is editor metadata
  readback only; it does not refresh `AssetCatalogView`, recompute product readiness, execute importers or change pending
  reimport ownership.
- The current command surface is intentionally limited to `texture.profile`. A source image file extension such as `.png`
  remains source-format information; catalog semantics come from `.ameta` settings such as `texture2d`, `sprite-sheet`,
  `texture-cube` and `skybox`. Sprite slices remain read-only catalog sub-assets until a later Import Settings/Inspector
  slice owns rects, pivots, packing and atlas bake data.

### Actions 扩展

Add menu or shortcut commands through `EditorActionRegistry`. Disabled actions should remain registered when a feature is
planned but unavailable, so menus and diagnostics stay stable.

Action rules:

- Use stable action ids such as `view.scene-view`.
- Keep `shortcut` strings in action descriptors; `EditorShortcutRouter` is the only per-frame ImGui shortcut poller.
- Emit `ActionInvoked` through the event queue.
- Keep callbacks on `EditorActionContext`; `EditorActionInvokeContext` owns event emission for
  dispatch, and broad app service bundles should not enter command handlers.
- 未来状态修改必须通过 command/transaction services。
- `EditorTransaction` failure paths must preserve the visible document contract: execute failure rolls back already-executed
  commands, undo failure restores already-undone commands, and failed undo/redo keeps the transaction on its original stack.

### Input 扩展

`EditorInputRouter` 是 editor host 的输入归属事实源。它当前记录 ImGui capture flags、raw mouse
drag/wheel facts、Scene View hover/focus state、`sceneViewCanReceiveMouse`、Scene View camera
input intent 和 `shortcutsEnabled`。Scene View camera navigation consumes this snapshot instead of
reading global ImGui/GLFW input state in the panel. Because Scene View is itself an ImGui-hosted viewport,
global `imguiWantsMouse` remains a recorded fact; local camera ownership is derived from the Scene View
viewport hover/focus report, overlay exclusion and text-input capture.

`EditorShortcutRouter` 消费 input router snapshot。它只在 `shortcutsEnabled` 为 true 时把
registered action shortcuts 转为 ImGui key chord，并调用 `EditorActionRegistry::invoke()`。
菜单、快捷键和未来 command palette 必须共享 action id，不要各自实现命令语义。

后续 gizmo 和 selection picking 也应先消费 input router snapshot，不要在各自模块里重新读取全局
ImGui/GLFW 状态。

### Viewport tool state

`EditorViewportToolState` 是 Scene View mode/tool chrome 的 app-local 单一事实源。它记录 active tool、transform
space、pivot mode、snap enabled、overlay flags、view mode 和 edit/play preview state，并通过 frame-local
`ViewportToolStateChanged` 事件进入 Log / Console diagnostics。

当前只有 View/Grid 这类 shell-level 状态可用；Select、Move、Rotate、Scale、Gizmo 和 selection outline 仍是 pending
provider/render bridge 状态。尝试激活 pending transform / selection tool 不会改变 active tool revision，只会产生 no-op warning
event。Scene View panel 只消费 snapshot 并把 overlay strip 的受限切换写回该 owner，不再持久保存自己的 overlay flag 副本。

### Viewports 扩展

Add new viewport consumers through `EditorViewportKind` and the `EditorViewportPanelHost` request/result API. Scene View,
Game View and Preview View should share renderer/RHI caches but own view-local request state.

`EditorViewportOverlayFlags` currently carries grid、transform gizmo、wire、selection outline、debug overlay and debug gizmo intent.
Only Grid is enabled in the Scene View overlay strip today. Transform Gizmo and Select / selection-outline contributions keep
their stable ids, but the controls are disabled and marked pending until `SelectionSet`, gizmo provider data and renderer
bridge work exist. `EditorViewportCoordinator` strips pending Scene authoring flags from the effective Scene View request
and strips Scene-only authoring flags from Game/Preview requests, while Game View may retain explicitly requested debug
overlay/debug gizmo flags for future runtime diagnostics.

Game View 不能隐式包含 grid、transform gizmo、wire overlay、selection outline 这类 Scene View authoring pass；如果用户需要在 Game View 里看 runtime debug gizmo，必须通过明确的 debug overlay/debug gizmo flag 进入 graph。

### 未来 `editor_domain`

Do not extract `editor_domain` just to move files. Extract it into the complete Editor System Package only when there is durable backend-neutral state with real
consumers, such as selection, transaction, inspector data model or editor service facade.

`packages/systems/editor` 内部 `editor_domain` may own:

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

The shell smoke also runs CPU-only editor state contract gates:

- `EditorSelectionSet`: replace/no-op/multi missing-stale refresh, layout-reset stability, clear, deterministic
  `SelectionChanged` event emission and diagnostics routing.
- `EditorInspectorModel`: empty selection, single read-only selection, multi-selection mixed values and validation row
  representation without ImGui dependency.
- `EditorDirtyState`: transient UI, document, asset metadata and pending reimport buckets stay separated, no-op updates
  preserve revision, and clears do not cross buckets.
- `EditorStateEvent`: selection, dirty, command-history and validation facts route through one deterministic event metadata
  shape, preserve ordering in diagnostics, and do not emit duplicate no-op state events.

Asset Browser、asset catalog snapshot 或 asset icon resolver 相关改动必须运行：

```powershell
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-asset-browser
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-asset-browser
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

Studio `Editor.exe --smoke-studio-viewport-cadence` 只承担前台静态 Scene 的 5 秒 Realtime 稳态基线，门控 exact surface-update
`>=60 FPS`、p95 与 max；它不再承载 resize/fault/overload 场景。`--smoke-viewport-transaction-resize`、
`--smoke-viewport-transaction-overload`、`--smoke-viewport-transaction-faults`、
`--smoke-viewport-transaction-supersede` 与 `--smoke-viewport-multi-endpoint` 已是独立真实 Studio GPU smoke，分别门控 splitter
exact/hidden、bounded latest-wins、13-stage 失败 ownership、latest/stale identity 与 same-compositor 两-endpoint group atomicity；
`--smoke-viewport-transaction-flash` 另逐个成功 transaction 的 group composition batch 检查 native
corner sentinel 的结构边界。各入口分开报告 native resource、transaction phase、Avalonia surface/`Rendered` 与 physical display；
`--smoke-viewport-transaction-window-resize` 还用真实 HWND 驱动 Windows integration 的 `WM_SIZING` precommit，但把性能与连续结构证据分开：
`performance` lane 不启动连续 recorder，以 first `Proposed`→final exact `Rendered` 门控 grow/shrink/A→B→A 三个 120 Hz、
90-input case 均 `>=60/s`；`continuous` lane 只对短 ABA 轨迹连续请求并采样 outer/client/workspace/panel/surface composition batch，门控
blank/stretch/crop/gap/mismatch=0，不作 FPS claim。release policy 在 `WM_EXITSIZEMOVE` 关闭 interaction epoch，使未接受 proposal stale，
停在最后 Published exact RECT；它必须输出 raw/accepted final 与 0–1 candidate 的 pixel/logical lag。release-stop 之前
`wait-final` policy 的 ABA 性能代表值为
90 inputs/744.47 ms、50 unique exact `Rendered` /
757.57 ms（66.00/s）、post-request transaction publish catch-up 2/2（25.44 ms，小于两个 60 Hz composition budget）、hidden=0；
结构 lane 为 24/24 exact sampled batches，五类结构错误
全为 0；这些历史数值不作为新的 release-stop gate 的通过数据。两条应用内 lane 仍明确输出
pixel/PhysicalDisplayed evidence unavailable。独立 Windows-only
`Asharia.Studio.WindowsCapture.Tests` 通过 `ASHARIA_RUN_STUDIO_WGC_DWM_ACCEPTANCE=1` opt in：drag 样本仍分类
blank/stretch/crop/gap/spill；release capture 从 interaction epoch 关闭起硬门控每个 WGC-delivered sample 都与最后
accepted/Published exact extent 一致，不允许 release gap/crop/stretch/blank/spill。它不保证捕获每个 DWM refresh，且始终报告
`PhysicalDisplayedEvidenceAvailable=false`。当前严格 release-stop gate 以 `SystemRelativeTime` 对齐 `release-imminent` QPC，从
`WM_EXITSIZEMOVE` 前的保守边界开始筛选；grow/shrink 2/2 PASS，release 分别为 1/1 与 2/2 exact，每个 delivered sample 都满足
`SceneBounds == completion accepted extent`，gap/blank/crop/stretch/accepted-extent mismatch 全为 0。两条 case 都先建立 pending raw
final，再验证其 `Cancelled` 且 `rawFinalProposalAccepted=false`。没有 observer 的层明确输出 evidence unavailable。代表性
owned-splitter resize 为 209/209 observed exact `Rendered` generations、106.44/s、p95
15.26 ms、hidden 0；新增 Window smoke 之前的五族 GPU process acceptance 为 47/47，steady 为 219.43 surface-updates/s。当前 PresentMon 复采因大量 ETW
event loss 且无 CSV 被作废，不能把这些
应用侧数字称为 physical display；PresentMon 即使有效也只能证明顶层 cadence，不能排除 Scene-only 中间像素帧。WGC pixel evidence
与 LCD scanout evidence 也必须继续分层。
multi-endpoint 当前只通过两 endpoint；3–4 realtime 容量与 slow-consumer queue HOL 仍未解决。

`--smoke-editor-viewport` also validates Scene View flag defaults, verifies that pending Gizmo/Select authoring flags are
cleared from effective Scene View diagnostics, verifies that Scene-only authoring flags are cleared from Game/Preview,
verifies that Game View can retain explicit debug overlay/debug gizmo intent, verifies that a flagged Scene View texture is
rendered and acquired back through the panel-facing texture result, and checks that the recorded RenderView exposes a
view-local diagnostics snapshot. It also validates the editor-only Scene View camera bridge, center viewport unproject ray,
near-plane origin, viewport corner orientation, invalid matrix rejection and resize aspect handling. It also verifies idle
Scene View on-demand reuse by checking that UI frames can reuse the last completed texture without incrementing
`viewportFramesRendered` every frame.
`--smoke-editor-asset-browser` validates that editor startup can load a snapshot-backed project catalog into
`EditorAssetCatalogStore`, route catalog rows and diagnostics through the frame context, and present a clean
`AssetCatalogView` without direct panel-side scanning, import execution, product cache writes or runtime loading. It also
seeds an editor-owned pending reimport marker from the temporary texture-profile row and verifies that the frame path
reports one reimport request, one pending reimport entry and one matching pending catalog row without treating that state
as product truth.
`--smoke-editor-frame-debugger` validates the editor-controlled `Running -> CaptureRequested -> CapturingFrame ->
WaitingGpuFence -> PausedFrameDebug -> Resume -> Running` flow. While waiting/paused, the editor keeps ImGui rendering alive
but skips normal RenderView recording, so the captured render inputs and diagnostics snapshot stay frozen until Resume. The
paused-state owner also gates the editor-owned inspected-world scheduler seam: frame advance, game update and script update
safe-point counters do not advance while the capture is waiting/paused, then resume afterward. The same smoke also verifies
that the Frame Debugger panel's RenderGraph view consumes the captured snapshot, requests a selected image resource preview,
records only the debug replay/copy path, and displays the resulting sampled preview texture.

## 当前缺口

- Scene Tree and Inspector now exist as read-only shell panels in the default workbench. They read the app-local
  `EditorSelectionSet` snapshot for stable selected ids and missing/stale state, and Inspector renders an app-local
  data-only model for rows, mixed values, validation and read-only dirty-state summary. The app-local `EditorDirtyState`
  separates transient UI, document, asset metadata and pending reimport facts, and state changes now share frame-local
  event metadata for diagnostics. Real scene hierarchy, picking, transaction-backed writable fields, dirty
  persistence/autosave, writable asset operations and richer asset browser workflows are still blocked on
  scene/asset/schema ownership becoming concrete enough.
- The Unity-like workbench UI baseline is visual and presentational only. It adds the `Unity 6 Dark` default theme,
  compact toolbar/status/panel metrics, Hierarchy/Project/Console visible labels, a Scene View header, a Project split
  navigation/content layout, a gray workbench shell behind darker rounded panel content blocks and disabled/pending controls
  for play, search, Console filters, Inspector lock/pin and not-yet-wired authoring affordances. It does not implement
  writable Inspector fields, scene hierarchy mutation, picking, transform gizmos, selection outlines or a new
  `editor_domain` target.
- World-space transform gizmo, wire, selection outline, debug overlay and debug gizmo passes are still pending
  renderer-side view pass work. Gizmo and Select controls stay disabled/pending in Scene View until real provider/render
  bridge support exists. Grid now has a renderer-owned fullscreen world-grid pass, RenderView policy for
  camera-height LOD/fade, source overlay diagnostics, Frame Debug replay preservation and a `sceneGrid` settings bridge
  for plane, spacing, fade, opacity and color. The Scene grid overlay contribution declares the same built-in default used by
  settings bootstrap, and Editor Settings consumes built-in category contributions for the left-nav/right-content General
  and Viewport pages. External settings manifests and reload remain deferred to the script/plugin boundary.
- Renderer prerequisites still pending for richer overlays are a more complete debug/world-line draw route for
  gizmo/selection shapes. External manifest loading, hot update behavior and reload diagnostics belong to the later
  script/plugin system boundary, not to renderer pass ownership.
- `EditorFrameDebugger` now owns capture/pause/resume state. A capture does not serialize script VM objects.
  `EditorInspectedWorldScheduler` is the current counter-based seam for future runtime/script integration: it runs frame
  advance、game update 和 script update safe-point counters while allowed, and records skipped counters while Frame Debug is
  waiting/paused.
- `RenderGraphPanel` is the Live RG View: it browses the latest compiled RenderView diagnostics snapshot as
  pass/resource/access/dependency/transition data without requiring Frame Debug capture.
- `FrameDebuggerPanel` owns Frame Debug inspection. It exposes a Unity-style Frame view and a RenderGraph view as switchable
  tabs in the same panel; the RenderGraph view browses the frozen captured snapshot, while the Frame view selects
  pass/execution-event rows and displays selected-event details plus preview imagery. RenderGraph command-summary rows remain
  supporting context, not draw-call identity.
- Scene View uses an editor-owned on-demand refresh policy. The panel still submits a viewport request every UI frame, but
  `EditorViewportCoordinator` only records a new RenderView when it derives a repaint reason such as initial texture,
  resize, overlay flag change, frame-debug event or `AlwaysRefresh`; otherwise ImGui redraws the previous texture.
- Frame Debug, Live RG View and pass graph visualization are separate editor concepts:
  - Frame Debug owns capture, pause/resume and fixed-frame inspection. It does not use `vkDeviceWaitIdle` for normal capture
    and does not read transient GPU resources after normal execution. Its primary view uses a left pass/command list and a
    right details/preview pane; its RenderGraph diagnostics are a tab inside the same `FrameDebuggerPanel`.
  - Live RG View displays the latest diagnostics snapshot derived after RenderGraph compile. The graph topology, dependency
    order, culling result, transition plan and resource lifetime are known at compile time; panel `draw()` does not record
    Vulkan commands or infer graph structure from GPU execution.
  - Pass graph visualization is a read-only node view derived from one of those snapshots, not an editable graph authoring
    system.
- Intermediate texture preview v1 is GPU-side only: Frame Debug records a controlled replay/copy into a debug-owned sampled
  image and registers that image through the existing `ImGuiTextureRegistry`. It supports color images with matching
  extent/format/mip/layer shape and reports `preview unavailable` for depth, buffer or unsupported resources. The primary
  Frame Debug panel now selects a renderer execution event first, resolves that event's pass to a previewable image output
  from the frozen diagnostics snapshot, and serves the refresh without resuming normal RenderView recording. Frame Debug
  smoke preserves the selected execution event id and preview image resource at preview time, then verifies that they resolve
  back to the frozen capture, the pass used for the preview copy, the event's target image resource and the corresponding
  RenderGraph write access edge. CPU readback, export and draw-call precise replay remain deferred.
- `recordEditorImguiFrame()` 位于 `imgui_frame_renderer.cpp`，由 `editor_vulkan_host` 的一帧提交 helper
  调用。作为 host integration 现在可以接受；如果它超出 swapchain ImGui pass recording，应继续移动到
  `imgui_runtime` 或独立的 editor ImGui pass module。
- There is no reusable `editor_domain` target yet by design.
