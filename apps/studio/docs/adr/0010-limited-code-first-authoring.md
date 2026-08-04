# ADR-0010：Studio 以真实标准工具试点受限 Code-first authoring

状态：Proposed

日期：2026-08-04

关联：延续 [ADR-0007](0007-studio-frontend-hard-cut.md) 的 Document-first 与单一 Avalonia runtime 边界；以
[ADR-0009](0009-authoritative-scene-document.md) 的真实 SceneDocument/Dock consumer 作为进入条件。

## 背景

R0 删除了旧 Code-first 公共 DSL、Host 与专属测试。该实现拥有 28 种 node kind、独立 state/event/tree validator 和
整棵 subtree replacement，但 production consumer 只有未接真实 render lane 的 Frame Debugger 与 UI Style gallery；
当时没有真实 Document、dirty/save、focus/IME 保持或可证明的 panel owner。删除结论成立，Git history 也不构成 API
兼容要求。

现在 #353 已让 ProjectSession、SceneDocument、EditWorld、Hierarchy、Inspector、Save/dirty/reopen 与真实 Dock owner
形成闭环。Code-first 可以重新评估，但不能倒回“先恢复一套通用 UI toolkit，再等待 consumer”的顺序。

公开引擎与 UI 框架给出的边界一致：Unreal Slate 证明 retained editor UI 可以采用代码中的声明式组合，但不要求 Asharia
复制 Slate macros 或建立第二套 virtual tree；Avalonia code-only UI 与 XAML 创建同一 control object graph，并支持代码中的
compiled binding；Godot EditorPlugin 要求贡献者在停用时移除并释放自定义 Dock，说明 Host 生命周期必须显式闭合。

参考：

- <https://dev.epicgames.com/documentation/unreal-engine/slate-overview-for-unreal-engine>
- <https://docs.avaloniaui.net/docs/fundamentals/coded-ui>
- <https://docs.godotengine.org/en/stable/classes/class_editorplugin.html>

## 提议

### 1. 区分两种代码 authoring

- **Code-only Avalonia**：直接创建 Avalonia controls，和 XAML 共用同一 runtime、binding、style、accessibility 与 Dock
  lifecycle。适合 algorithmic composition、专用绘制或 markup 不自然的局部视图；它不是 Asharia Code-first DSL。
- **Asharia Code-first schema**：UI-neutral、受限的 standard-tool 描述，只为少量低频、小规模工具服务。它不能表达
  任意 Avalonia control、style、DataTemplate、虚拟化、动画或 Window/Dock ownership。

### 2. 接入时点

按以下顺序实施：

```text
#353 SceneDocument + real Dock consumer 收口
-> P0.5 独立 Slice：最小 Code-first kernel/host + Document Diagnostics Summary
-> P1 Package Manifest/Lock：Package Resolution Summary 作为第二个内部 consumer
-> 两个 consumer 稳定后冻结可复用 schema
-> 第二个真实外部 consumer 出现后，才讨论 public Editor facade
```

因此 Code-first 不混入当前 Hierarchy/Inspector Slice，也不等待 Play Mode 或完整插件系统。它在真实编辑闭环之后、Package
能力 UI 扩张之前接入。

### 3. 首个 consumer 与最小能力

首个 consumer 是 Dock 底部的只读 `Document Diagnostics Summary`，数据来自现有 bounded `StudioDiagnosticHub` 与当前
Project/Document snapshot：Project/Document identity、revision、saved revision、dirty、scene native availability、少量
recent diagnostics，以及 presentation-only `Copy Summary` action。

v0 只实现该 consumer 需要的 section、text、status、key/value row 与 action。Host 必须证明 stable key、duplicate-key
拒绝、每 dispatcher turn 最多一次 rebuild、bounded node/depth、异常隔离、attach/detach/dispose、主题与 automation
semantics。v0 不包含 editable text、列表虚拟化、raw Avalonia control 嵌入、frame tick、反射 binding、插件加载或 ALC。

### 4. 明确不使用 Code-first 的面板

- Hierarchy、Project/Asset Browser、Console/Problems：数据量与虚拟化要求高；
- Inspector：文本、数值、验证、focus/IME 与 transaction 交互密集；
- Scene/Game View、graph、timeline、curve：需要专用 drawing/input surface；
- 长期复杂 panel：默认 compiled XAML + typed ViewModel。

若一个 panel 超过 v0 schema，应升级为 Avalonia content panel，不为单一功能扩张 Code-first primitive。

## 与 ADR-0007 的关系

ADR-0007 对旧 Code-first virtual tree、第二套通用 toolkit 和无 consumer public SDK 的拒绝继续有效。本提议只有在独立
Slice 被接受时，才允许恢复一个内部、consumer-driven 的窄 schema；不恢复旧 namespace、二进制兼容、旧 28-kind API
或 dynamic extension host。若试点无法在上述限制内完成，则保持 ADR-0007 的删除状态，直接使用 code-only Avalonia。

## 验收门禁

1. 首个真实 panel 从 Application snapshot/diagnostic truth 构建，不含 fixture production data。
2. Headless 验证 Dock attach、tab hide/show、重建合并、automation semantics 与 terminal dispose。
3. duplicate key、node/depth budget、builder exception 与 stale invalidation 都 fail closed。
4. Editor Image 不增加插件/runtime compiler 依赖；Application 不引用 Avalonia 或 Code-first host。
5. architecture gate 继续禁止 public `Asharia.Editor`、legacy Code-first namespace 与第二套 Window/Dock owner。

## 被拒绝的替代方案

- **现在把 Hierarchy/Inspector 改成 Code-first**：会立即要求虚拟化、editable text、validation、focus/IME 与 transaction，
  迫使 schema 重新膨胀。
- **直接恢复历史实现**：旧 API 的 consumer、owner 与风险已失效，恢复会违反 hard-cut 无兼容迁移原则。
- **只做 code-only Avalonia 并称为 Code-first**：会混淆同一 runtime 的 authoring syntax 与 UI-neutral schema。
- **等插件市场再做**：会把 schema 与 public SDK、reload 和安全边界一次耦合，无法用小型真实 consumer 验证。
