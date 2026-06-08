# Editor 架构

更新日期：2026-06-08

本文记录当前 `apps/editor` 的真实架构边界。它描述已经落地的 editor host、ImGui
integration、panel/action/event、Scene View viewport、input/shortcut routing、ImGui texture registry 和验证入口。
阶段拆分见 `docs/planning/editor-development-plan.md`；脚本扩展和 C++/脚本协作边界见
`docs/architecture/editor-ui-scripting.md`；工具、插件、viewport overlay 和 hot reload 的后续 contract 见
`docs/architecture/editor-extension-architecture.md`。

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
- `asharia::archive`
- `asharia::asset_core`
- `asharia::asset_core_io`
- `asharia::asset_pipeline`
- `asharia::project_core_io`
- `asharia::shader_slang`
- `imgui::imgui`
- ImGui GLFW/Vulkan backend source files from the Conan ImGui package

这些依赖属于当前 `apps/editor` host executable 的集成边界。Editor app 可以组合 public project/asset/pipeline
API 来加载项目描述、读取 `.ameta`、构造只读 catalog snapshot、生成 report 和记录 pending reimport facts；这不表示
存在可复用的 `packages/editor-core`，也不表示 editor panel 可以拥有 importer execution、product cache writes、runtime
asset handles 或 renderer/GPU lifetime。

禁止方向：

- `engine/core`、runtime packages、renderer packages 不依赖 `apps/editor`。
- 未来 `packages/editor-core` 不 include ImGui、GLFW、Vulkan 或 renderer implementation headers。
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
| `editor_asset_catalog` | editor-owned read-only project catalog snapshot service, source-root/path helpers and deterministic catalog reports composed from public `project-core` / `asset-pipeline` / `asset-core` APIs | filesystem watcher, importer execution, product manifest/blob writes, runtime asset handles or GPU resources |
| `editor_asset_import_settings_command` | undoable `.ameta` import-setting edits plus editor-owned reimport request/pending facts | import scheduling, product cache mutation, catalog truth, runtime loading or preview texture allocation |
| `editor_asset_icon` | editor-owned Lucide icon ids, asset icon query descriptors, custom resolver registry and ImGui glyph rendering | plugin-owned SVG injection, source scanning, texture/Vulkan ownership or runtime asset loading |
| `imgui_editor_shell` | dockspace host, main menu, command bar, status bar and action menu binding through shell-local capability contexts | renderer command recording、panel object ownership、hard-coded tool layout policy |
| `editor_panel` | panel descriptor/state、singleton panel registry、focus/open/close lifecycle | ImGui backend setup、Vulkan resource lifetime |
| `editor_action` | action descriptor、enabled state、callback invocation、stable action ids and action-only service bundle | command transaction semantics before transaction exists、full app service access |
| `editor_event` | frame-local typed event queue、diagnostics history sink | global EventBus、durable document storage |
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

Scene View overlay state remains editor-owned until the coordinator translates it into renderer-owned data. Renderer-facing
`BasicRenderViewDesc` uses `BasicRenderViewKind`, camera matrices, per-view frame params, explicit overlay color load/store
and blend policy, plus a data-only debug world-line span. It does not use `EditorViewportKind`,
`EditorViewportOverlayFlags`, ImGui ids or Vulkan handles from panels. Grid, gizmo and debug draw passes must consume this
contract in later slices instead of reading editor panel state directly.

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
workspace describes the dock slots for Scene Tree, Scene View, Inspector, Live RG View, Frame Debugger, Asset Browser,
UI Style Preview, Editor Settings and Log.
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

`editor_ui` owns the editor-local Dear ImGui style tokens and the built-in editor theme catalog. The default theme is
`black-default` (Black Default); `classic-blue-gray-2` remains available, and the legacy `classic-blue-gray` settings value
is still accepted as an alias. Alternate themes include warm graphite amber, forest green slate, purple electric, carbon
copper, cool gray teal and light graphite orange. `ImGuiRuntime::create()` applies the startup theme from editor settings, and runtime theme
changes are applied through `EditorSettingsController`. This is editor shell presentation state only; renderer, RHI and
runtime packages do not depend on theme colors, rounding values or component preview helpers.

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
View / Log / RG View / Frame Debug labels. It is deliberately scoped to `apps/editor`; runtime, renderer and asset text
localization are separate future concerns.

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
- `editor_asset_catalog` composes public `project-core`, `asset-pipeline` and `asset-core` APIs into a read-only project
  snapshot for future browser wiring. It does not own watcher, hot reload, import execution, runtime loading or renderer
  resources.
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

- Scene Tree and Inspector now exist as read-only shell panels in the default workbench. Real selection, transaction,
  dirty state, inspector data model, writable asset operations and richer asset browser workflows are still blocked on
  scene/asset/schema ownership becoming concrete enough.
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
- There is no `packages/editor-core` yet by design.
