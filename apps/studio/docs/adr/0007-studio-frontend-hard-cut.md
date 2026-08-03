# ADR-0007：Studio 前端采用 Document-first 硬切重构

状态：Accepted（目标已批准；R0 production cutover 进行中）

日期：2026-07-31

实施注记：2026-07-31 已开始 R0 cutover；假 Scene/Frame Debug production consumer 已切除，
`ensureContext()` raw-pointer escape 已改为 value snapshot。`App` 已成为唯一 process-session owner，legacy
production composition 与 UI-thread sync-over-async 已删除，并由 typed teardown receipt 覆盖异常与 deadline。
bounded diagnostics/log ingress、自有 Avalonia sink、最小 production Shell 与真实 Headless/accessibility baseline
已完成；Code-first公共DSL/Host/专属测试已删除；断开的 `StudioCompositionRoot`、aggregate Workbench module、
`MainWindowViewModel` 与legacy session branch也已删除。
随后 Workbench action/shortcut/menu/command-palette runtime、legacy contribution validator与无 consumer的
command/status public contract，以及 disconnected Dock graph/view/viewmodel/registry/layout/ViewLocator surface均已删除。
28-file ProjectCode control plane与专属tests也因production reachability为0整体删除；其两文件distribution-build
bootstrap前置孤岛随后删除；test-only Application extension host、无host/factory的root built-in Features、无request producer的Dialog presentation、无selection owner的Project launch presentation、test-only managed ProjectOpenSession checkpoint、active ProjectSession/gateway/store、managed EngineBridge Project及其无caller C++ bridge/self-smoke尾链也已删除；无consumer的App级UI token/icon/tree/control/font registry及其三个附加包也已删除；未引用的旧public SDK surface删除和
完整 native 门禁仍未满足。随后R0 Studio中无consumer的managed viewport/native deployment闭包也已硬切：process receipt
只陈述真实managed composition，root publish不再携带`Runtime.Contracts`、`EngineBridge`、`editor_native.dll`或`slang.dll`；
Editor Image只分发fresh App publish与AppRelative所需的选定hostfxr/Core runtime，不携无reader的`dotnet.exe`、SDK、
reference pack或ProjectCode metadata。随后只有stub/fixture tests自证、且默认绑定被发行物拒绝DLL的managed/public
Frame Debugger岛与其scheduler专属枚举也已删除；独立C++ editor target/smoke保持不变，不能被Studio fixture替代。

后续更新：2026-08-03 的 [ADR-0008](0008-authoritative-project-session.md) 在真实 App consumer、Application owner、
专用 project-core adapter 和 Release identity closure 同时成立后，重新引入最小 create/open ProjectSession；它不撤销
R0 对旧 facade、无 caller adapter、renderer/Vulkan Project IO 耦合和 phantom distribution 的删除结论。

取代：

- [ADR-0004：采用统一 Editor Extension Framework](0004-unified-editor-extension-framework.md) 中
  “先建设统一扩展框架、Code-first backend、BuiltInExtensions dogfooding 和八项目扩展宿主”的目标；
- [ADR-0005：采用隔离构建、generation reload 与 last-known-good](0005-managed-editor-module-build-and-reload.md)
  作为 Studio v1 启动与前端关键路径的决策。

ADR-0004 关于编译期边界、Host 拥有 Window/Dock/native lifetime，以及 ADR-0005 关于外部代码不是安全边界、
构建不得阻塞 UI thread 的理由仍然成立。若未来出现第二个真实外部扩展 consumer，必须基于当时需求重新立 ADR，
不能自动恢复旧方案。

完整目标结构、数据结构、设计模式、native 问题账本和切换门禁见
[Studio 前端硬切架构](../architecture/studio-frontend-hard-cut.md)。

## Context

当前实现并不是 Avalonia、MVVM 或模块化方向本身失败，而是建设顺序和真相所有权发生了倒置：

- hard-cut前 `Asharia.Editor` 已有139个C# source file，公共合同覆盖module generation、capability、Code-first、
  transaction、selection、task和viewport，但没有production `DocumentHost`；当前已缩至100个文件；
- hard-cut前 `Asharia.Studio.Application` 的59个C# source file中，`ProjectCode`占28个文件、13,324行；
  该零production-consumer控制面、两文件distribution bootstrap及10文件Application extension host现已删除，
  Application缩至19个文件；
- production 启动仍经过
  `StudioCompositionRoot -> LegacyEditorModuleCompatibilityAdapter -> WorkbenchFeatureModule
  -> legacy Panel/Action registry`，迁移脚手架已经成为主路径；
- 只有 Frame Debugger 与 UI Style 两个 production panel 使用自有 Code-first DSL，但它已经引入
  31 个 namespace/source area、大量测试和一套 Avalonia subtree host；
- ADR 审查时的 scene projection 在项目 ready 后生成虚构的 `Untitled Scene` 与 `Main Camera`，没有真实
  Scene Document、load/save、dirty 或 Engine mutation receipt；
- transaction 保存可执行 object closure，只同步调用 editor-side `Apply/Revert`，没有 `DocumentId`、
  expected revision、native mutation result 或失败后的可靠原子恢复；
- App、MainWindow ViewModel、Dock workspace、compatibility adapter 与 native viewport runtime 分别拥有
  一部分 composition/lifetime，导致关闭、异常隔离和重启语义无法由一个 owner 证明；
- native viewport 使用 process singleton、raw pointer token 和永久 `shutdownRequested`，managed 侧又有一套
  static drain state。这个问题不能由继续包装 ViewModel 或 adapter 修复。

继续逐个修补局部问题会保留双框架、扩大公共 API，并让新的 Document/Undo/Save 反过来适配错误的宿主顺序。
因为当前没有旧版兼容要求，保留 compatibility layer 的成本大于硬切成本。

## Decision

### 1. 重构方式

采用**无旧版兼容的分阶段硬切**：

- 不提供 wrapper、type forwarding、双注册、旧布局 schema 自动兼容或旧 extension binary compatibility；
- 每个阶段必须交付一条可运行垂直切片，禁止先批量搬目录再等待后续修复；
- 新路径通过真实门禁后直接删除对应旧路径；同一能力不得长期双写或双读；
- 在 cutover 之前冻结旧 compatibility、Code-first、generation reload 与扩展 public surface，不再加能力。

### 2. 建设顺序

第一条闭环固定为：

```text
open real SceneDocument
-> read Engine-owned snapshot
-> edit one typed Transform field
-> submit expected-revision mutation batch
-> receive typed mutation receipt/change set
-> publish new document revision + dirty state
-> undo/redo
-> save
-> close with save/discard/cancel
```

Document、Edit World、transaction、save 和 snapshot 是前端架构的核心写模型。Action、Dock、Inspector、
extension、reload 和 UI authoring 都只能消费这条闭环，不能先于它定义真相。

### 3. 目标编译期边界

Studio v1 使用六个 managed Studio production project，并继续复用独立的 `Asharia.Runtime.Contracts`：

```text
Asharia.Studio.Application
Asharia.Studio.EngineInterop
Asharia.Studio.EngineBridge
Asharia.Studio.Infrastructure
Asharia.Studio.Presentation.Avalonia
Asharia.Studio.App
```

- `Application` 同时拥有被当前垂直切片验证的 UI-neutral ID、snapshot、intent/result contract，以及
  Studio/Project/Document session、selection、action、transaction、task 和 diagnostics；
- `EngineInterop` 只承载 Presentation 与 EngineBridge 都必须看到的 viewport/frame lease 窄合同；
- `EngineBridge` 是唯一 native/runtime adapter；Application 不依赖其实现；
- `Infrastructure` 实现 project descriptor、filesystem、settings、build/worker process 等 Application port；
  它不拥有 editor state，也不能成为通用 dumping ground；
- `Presentation.Avalonia` 包含 Shell、Dock、built-in panel 与 ViewModel；MVVM 只在此层成立；
- `App` 是唯一 composition root 和进程 lifetime owner。

不保留当前膨胀的 `Asharia.Editor` public SDK，也不立即创建 `Asharia.Editor.Avalonia`、
`BuiltInExtensions` 或 analyzer/source-generator project。出现第二个真实外部 consumer 后，再从已经稳定的
Application use case 提取窄 public facade，并用 compiler boundary 证明拆分价值。

### 4. UI 与扩展

- Studio v1 只有一个 UI runtime 和一条 production authoring 路径：Avalonia retained control tree；
- App只安装当前真实control tree消费的Avalonia基础主题；view-local颜色由唯一View拥有。只有出现真实第二consumer
  与显式startup/teardown owner后，才提取Asharia共享style/icon/font registry；
- 长期 panel 默认 compiled XAML + typed ViewModel；专用绘制使用 Avalonia custom control；
- 删除自有 Code-first tree/host/state/event DSL；未接 render lane 的 Frame Debugger 先移除，
  UI Style developer gallery 以 typed ViewModel + compiled Avalonia View 重建；
- built-in 功能先静态组合，不经过 dynamic module/generation host；
- project/package extension、collectible ALC、hot reload 与 last-known-good 从 v1 前端关键路径移除；
- 未来扩展第一版默认 restart-required。只有第二个真实 consumer 和重复 unload canary 同时证明收益后，
  才考虑动态 reload。

### 5. 写模型与 native 边界

- View/ViewModel 不保存 mutable Engine object、native pointer 或可执行 undo closure；
- Application 发送包含 `DocumentId + expected revision + typed operations` 的 mutation batch；
- Engine owner 返回 typed success/rejection/fault、new revision、change set 与可撤销 inverse data；
- history 只在 mutation 与补偿成功后移动；补偿失败使对应 document 进入显式 fault/read-only recovery；
- native runtime/session/slot/packet 改为显式 owner handle 和 generation 校验；`IntPtr`/`void*` 不越过 adapter；
- 本 ADR 只记录 native 问题和所需合同。ABI 改造必须独立 Slice、独立 smoke，不与第一批目录迁移混做。

### 6. 所有权与并发

- `StudioSession -> ProjectSession -> DocumentHost -> DocumentSession` 是唯一业务生命周期树；
- `App` 先请求异步 stop，等待 task/panel/document/viewport drain，再执行 native barrier，最后关闭窗口；
- Application mutable state 由单一串行 owner 修改。后台工作只返回带 session/generation/revision 的结果；
- UI event 是 invalidation，subscriber 重读 immutable snapshot；不引入 global event bus 或 Redux store；
- 高频 frame 数据使用固定 slot/ring buffer/lease，不把每帧 GPU 状态复制成 managed immutable object graph。

## Engine-reference-first rationale

采用：

- Unreal `FDocumentTracker` 所体现的 document payload、document tab 与 tab history 分离；
- Unreal transaction/Interactive Tools 的 begin/commit/cancel 与一次用户意图边界；
- Unreal Subsystems 把实例绑定到明确 outer/lifetime，并由 owner 触发 initialize/deinitialize；R0 process session
  采用同样的 scope-owned lifetime，而不复制 UObject subsystem API；
- Unreal `FOutputDeviceRedirector` 对跨线程缓冲、backlog 与 shutdown flush 的边界，作为下一个 diagnostics
  Slice 的参照；Asharia 已采用 process owner、跨线程 bounded ingress 与 teardown tail，但拒绝无界 backlog，
  改为固定 ring、cursor/drop 和逐 subscriber 隔离；
- O3DE Action Manager 的 action、context、placement 与 hotkey 分离；
- O3DE System Component 的依赖顺序 activate 与反向 deactivate/resource disconnect，支持 registration lease
  逆序释放；Asharia 不采用 EBus 或全局 component registry；
- Godot `EditorUndoRedoManager` 的 scene/resource history 归属思想；
- Godot `Main` 拥有启动、主循环与关闭的结构，交叉支持把最终 process shutdown 留给 App，而不是 Window；
- Unreal 无显式 project 参数时进入 Project Browser；Godot 首先显示 Project Manager；O3DE 也把 Project Manager
  作为选择/创建 project 的入口。R0 因而只在 start 完成后公开 `No Project` / `No Document` 空状态，不在真实
  Project/Document owner 之前装配完整 Dock、panel、scene 或 recent-project 恢复路径；
- Avalonia retained controls、compiled binding 与 Headless 测试作为唯一桌面 UI 路径；R0 使用官方 Headless backend
  真实加载 production XAML/control tree，并以 `AutomationProperties.Name`、稳定 `AutomationId` 和标准 control role
  固定最小 accessibility 语义。
- Unreal `FAppStyle/FSlateStyleRegistry`、Godot `Theme`与O3DE `StyleManager`都把应用级style绑定到真实共享控件与
  显式注册owner；R0因此只保留Avalonia Fluent基线，并把唯一Window的三个颜色留在view内。
- Unreal `RuntimeDependencies`只staging模块声明的真实runtime file，O3DE Asset Bundler按product dependency收敛bundle；
  Microsoft AppRelative apphost实现只从相对root的`host/fxr`与`shared/<framework>`解析运行闭包。R0因此只发布当前
  managed App、selected hostfxr与Core runtime；SDK apphost template仅作build-time byte qualification，不进入产品树。
- Unreal RenderDoc integration从真实project/Level Viewport捕获实际frame，O3DE `RenderDocSystemComponent`只在真实API
  可用时随component lifetime连接/断开，Unity Frame Debugger附着真实Editor/Player观察实际render event。R0因此采用
  “真实render owner、同一frame lane与explicit lifetime后才定义adapter”的边界，不把独立native smoke解释成Studio能力。

调整：

- 不复制 Unreal/UObject、Godot singleton 或 O3DE bus；
- 不复制 Avalonia `StringLogSink` 的 Trace/global text truth 或 source hash；自有 adapter 只提取 bounded value fields，
  Application 以 `Framework` origin 保持 UI-neutral；
- 不把 Headless 属性断言冒充 Windows UIA 端到端测试：R0 证明真实 production control tree、binding 与 automation
  metadata；平台 accessibility bridge/Appium 矩阵留到 R5；
- 不因 Slate 的代码声明语法建立自有 virtual tree DSL；
- 不为不存在的控件保留全局token/icon/control-style/typography/font registry，也不依赖缺项fallback掩盖悬空owner；
- 不为不存在的native Studio child保留drain/fallback receipt、fixture PE或runtime DLL；也不把无reader的SDK/reference
  pack/metadata当成能力。独立native target由自身owner与smoke证明，未来接回Studio必须另过生命周期门禁；
- 不保留无consumer的Frame Debug public DTO、`bool + JSON + process-static` P/Invoke或scheduler capture枚举；stub snapshot
  与静态`CaptureRequested`不能替代real frame → render-lane owner → product consumer闭环；
- 不在没有外部 consumer 时复制大型引擎的 plugin/reload 基础设施；
- Asharia 的 C++23/Vulkan、headless 和 package-first 约束要求 native mutation、frame lease 与 UI state 分离。

## Rejected alternatives

### 继续渐进兼容迁移

Rejected。当前 adapter 已位于 production composition 中，继续迁移会让临时模型继续决定新 Document/Action API。

### 一次性重写全部前端和 native

Rejected。风险无法定位，也违反完整垂直切片原则。硬切的是 contract 和 production path，不是取消阶段门禁。

### 保留 Code-first 作为未来插件优势

Rejected。两个 production consumer 不足以证明第二套 authoring/runtime abstraction；当前 subtree replacement
还带来 focus、IME、control identity、accessibility 与测试成本。

### 先完成 dynamic extension/reload

Rejected。它没有修复 Document truth、transaction atomicity 或 native lifetime，反而扩大 public compatibility
和 unload surface。

### 全局 store/event bus/Service Locator

Rejected。当前状态天然按 Studio、Project、Document、Panel 和 Viewport scope 分区；全局化会隐藏 owner、
revision 和 teardown。

## Consequences

Positive：

- 前端首先验证真实编辑闭环，而不是验证扩展框架能否自洽；
- 单一 UI 路径消除重复 lifecycle、binding、focus 和 accessibility 语义；
- Application 从 ProjectCode 控制面和 native/Avalonia implementation 中解耦；
- 无兼容层使 compiler 能直接暴露错误依赖，并允许删除错误 public API；
- native 阻断项有明确 owner、ABI 目标和独立验证边界。

Negative：

- 已完成的 Code-first、module generation 与 compatibility 代码会有删除成本；
- 旧 layout、旧 extension binary 和旧 generated artifact 不迁移；
- 在首个 SceneDocument 闭环完成前，功能数量会短期减少；
- native runtime handle 改造需要 C++/C ABI/C# adapter 三层同步。

## Cutover gates

1. 新的 SceneDocument 垂直切片在无 Avalonia、无 real native 和 real native 三种 fixture 下通过。
2. Shell 只消费 Application snapshot/intent，ViewModel constructor 不创建业务 service。
3. App 的 start/stop 全异步，关闭无 UI-thread sync wait，task/document/viewport lease 归零。
4. Code-first production source、compatibility adapter、legacy Panel/Action contract、当前未验证的
   `Asharia.Editor` public surface 和 root `Editor.csproj` 被删除。
5. `Asharia.Studio.Application` 不再包含 `ProjectCode`/build、filesystem、settings 或 process implementation。
6. native handle/epoch、stale release、device lost、shutdown timeout 和 restart smoke 独立通过。
7. architecture tests 从 namespace/source-string 约定升级为 project reference、public API 和 forbidden dependency 门禁。

## References

- [Unreal FDocumentTracker](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/WorkflowOrientedApp/FDocumentTracker?application_version=5.5)
- [Unreal FTrackingTransaction](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FTrackingTransaction)
- [Unreal Programming Subsystems](https://dev.epicgames.com/documentation/en-us/unreal-engine/programming-subsystems-in-unreal-engine)
- [Unreal FOutputDeviceRedirector](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/Misc/FOutputDeviceRedirector?application_version=5.5)
- [Unreal RenderDoc integration](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-renderdoc-with-unreal-engine)
- [O3DE RenderDocSystemComponent](https://github.com/o3de/o3de/blob/development/Gems/Atom/RHI/Code/Source/RHI.Profiler/RenderDoc/RenderDocSystemComponent.cpp)
- [Unity Frame Debugger](https://docs.unity3d.com/Manual/FrameDebugger.html)
- [O3DE Action Manager](https://www.docs.o3de.org/docs/user-guide/action-manager/)
- [O3DE System Components](https://www.docs.o3de.org/docs/user-guide/programming/components/system-components/)
- [O3DE Component lifecycle](https://www.docs.o3de.org/docs/user-guide/programming/components/overview/)
- [Godot EditorUndoRedoManager](https://docs.godotengine.org/en/stable/classes/class_editorundoredomanager.html)
- [Godot architecture overview](https://docs.godotengine.org/en/stable/engine_details/architecture/godot_architecture_diagram.html)
- [Unreal running without a project](https://dev.epicgames.com/documentation/en-us/unreal-engine/running-unreal-engine)
- [Godot Project Manager](https://docs.godotengine.org/en/stable/getting_started/introduction/first_look_at_the_editor.html)
- [O3DE Project Manager](https://docs.o3de.org/docs/user-guide/project-config/project-manager/)
- [Avalonia classic desktop lifetime](https://api-docs.avaloniaui.net/docs/T_Avalonia_Controls_ApplicationLifetimes_IClassicDesktopStyleApplicationLifetime)
- [Avalonia ShutdownRequested](https://api-docs.avaloniaui.net/docs/E_Avalonia_Controls_ApplicationLifetimes_ClassicDesktopStyleApplicationLifetime_ShutdownRequested)
- [Avalonia compiled bindings](https://docs.avaloniaui.net/docs/data-binding/compiled-bindings)
- [Avalonia Headless testing](https://docs.avaloniaui.net/docs/testing/setting-up-the-headless-platform)
- [Avalonia accessibility](https://docs.avaloniaui.net/docs/app-development/accessibility)
- [Unreal FAppStyle](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/FAppStyle)
- [Unreal FSlateStyleRegistry](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/FSlateStyleRegistry)
- [Godot Theme](https://docs.godotengine.org/en/stable/classes/class_theme.html)
- [Godot GUI skinning](https://docs.godotengine.org/en/stable/tutorials/ui/gui_skinning.html)
- [O3DE UI component development guidelines](https://docs.o3de.org/docs/tools-ui/uidev-component-development-guidelines/)
- [O3DE StyleManager source](https://github.com/o3de/o3de/blob/development/Code/Framework/AzQtComponents/AzQtComponents/Components/StyleManager.h)
