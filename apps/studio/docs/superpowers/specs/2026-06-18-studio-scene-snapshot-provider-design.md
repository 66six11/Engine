# Studio Scene Snapshot Provider Design

## Goal

让 `apps/studio` 的 Hierarchy 和 Inspector 从当前 demo-only 数据进入同一个只读 scene snapshot 数据源，同时保留现有 `IEditorSelectionService` 作为跨面板同步边界。

## Context

当前 Studio 已有以下基础：

- `Core/Abstractions/IEditorSelectionService.cs` 定义 selection 服务入口。
- `Shell/Selection/EditorSelectionService.cs` 拥有当前 app-local selection 状态。
- `Features/Hierarchy/Services/IHierarchyDataSource.cs` 和 `DemoHierarchyDataSource.cs` 提供小规模演示节点。
- `Features/Hierarchy/ViewModels/HierarchyPanelViewModel.cs` 负责树投影、搜索、展开和选择。
- `Features/Inspector/ViewModels/InspectorPanelViewModel.cs` 从 selection snapshot 派生只读 Inspector 文档。
- `docs/Dock系统指南.md` 已将 “Hierarchy provider follow-up” 和 “Inspector provider follow-up” 列为下一批切片。

本设计对应 GitHub 方向：

- `#16 [Epic] Editor: scene object, selection, and transaction foundations`
- `#119 [Epic] Editor: Unity-like workbench UI redesign`

## Non-goals

- 不实现 scene save/load。
- 不实现 Transform 写回、Undo/Redo、dirty state 或 transaction-backed edits。
- 不接入真实 C++ runtime、Vulkan renderer、asset import、Project Browser 或 Console。
- 不引入 `packages/editor-core` 或多项目拆分。
- 不让 ViewModel 直接引用 Avalonia 控件或创建 View。
- 不让 Feature 之间直接依赖。

## Recommended Approach

采用一个最小只读 snapshot provider。

Core 层定义 backend-neutral scene snapshot 模型和 provider 接口。Hierarchy 从 provider 获得完整节点快照并建立树投影。Inspector 通过当前 selection id 查询同一个 snapshot 中的对象摘要，并生成只读 `InspectorDocumentModel`。

`DemoHierarchyDataSource` 不再作为长期业务路径，而是保留为 design/test fixture provider。这样可以先稳定数据合同，再替换真实 engine 或 project-backed provider。

## Alternatives Considered

### A. Keep DemoHierarchyDataSource And Expand It

优点是改动最少。缺点是 demo source 会继续承担真实接口职责，Hierarchy 和 Inspector 会围绕假数据形状增长，后续替换成本更高。

### B. Add Writable Scene Model Now

优点是可以更快进入 Transform 编辑。缺点是会同时引入 mutation、validation、dirty、undo/redo 和 persistence 问题，范围超过当前 PR-sized slice。

### C. Add Read-only Scene Snapshot Provider

这是推荐方案。它让 Hierarchy/Inspector 共用同一个对象事实源，但仍保持只读、可测试、边界清楚。写回能力留给单独 transaction-backed transform edit slice。

## Architecture

### Core Contracts

新增 Core 模型应保持无 Avalonia 依赖：

- `SceneSnapshot`
  - 表示一个只读 scene/document 快照。
  - 包含 scene id、display name、revision 和节点列表。
- `SceneObjectSnapshot`
  - 表示一个对象或组件摘要。
  - 包含 stable id、display name、kind、parent id、icon key、active state 和只读 property facts。
- `SceneObjectPropertySnapshot`
  - 表示 Inspector 可显示的一行只读属性。
  - 包含 property id、display name、value text、value kind 和 optional diagnostic。
- `ISceneSnapshotProvider`
  - 提供当前 snapshot。
  - 可按 stable object id 查询对象。

接口只表达当前 UI 需要的事实，不提前定义 serialization、component schema、runtime pointer 或 asset handle。

### Feature Ownership

Hierarchy Feature:

- 依赖 `ISceneSnapshotProvider` 和 `IEditorSelectionService`。
- 将 `SceneObjectSnapshot` 投影为 `HierarchyNodeRowViewModel`。
- 继续拥有搜索、展开、可见行和行选择状态。
- 不拥有 scene 数据，也不修改 provider。

Inspector Feature:

- 依赖 `ISceneSnapshotProvider` 和 `IEditorSelectionService`。
- 将 selection snapshot 映射到 `InspectorDocumentModel`。
- 单选时显示对象基础属性和 provider 提供的只读 property facts。
- 多选时显示 selection summary，不做 mixed editable value。
- missing/stale/unknown selection 显示明确 validation row。

Workbench/Shell:

- 创建并注入当前 provider。
- 当前 provider 使用 fixture-backed snapshot。
- Shell 不生成 Hierarchy 行、Inspector property row 或 Feature 内部布局。

### Fixture Provider

新增 fixture provider 替代当前 `DemoHierarchyDataSource` 的长期角色：

- 提供一个小型 read-only scene：Main Scene、Camera、Light、Demo Cube、Mesh Renderer。
- 每个对象使用稳定 id，例如 `scene:main/cube`。
- 每个对象提供足够 Inspector 展示的只读属性：Name、Kind、Id、Active、Parent。
- 测试可直接构造 provider，不依赖 Avalonia runtime。

## Data Flow

启动路径：

```text
WorkbenchFeatureModule
  creates fixture SceneSnapshotProvider
  injects provider into HierarchyPanelViewModel
  injects provider into InspectorPanelViewModel
```

Hierarchy 选择路径：

```text
Hierarchy row selected
  -> HierarchyPanelViewModel calls IEditorSelectionService.Select(...)
  -> selection snapshot revision changes
  -> InspectorPanelViewModel observes or refreshes from selection snapshot
  -> Inspector queries ISceneSnapshotProvider by selected id
  -> InspectorDocumentModel is rebuilt as read-only document
```

Snapshot refresh path for this slice:

```text
provider snapshot replacement
  -> Hierarchy rebuilds rows from new snapshot
  -> selection service keeps stable ids
  -> Inspector resolves selected id as found, missing, or stale
```

This slice may expose explicit refresh methods for tests. It does not add background refresh, filesystem watchers, runtime polling or async data loading.

## Error Handling

- Empty provider snapshot produces an empty Hierarchy and an Inspector empty-state document.
- Duplicate scene object ids are rejected by the provider factory before reaching ViewModels.
- A selected id missing from the current snapshot produces an Inspector validation row with missing state.
- A selected id from a different scene id produces stale or missing state; it does not silently select a same-named object.
- Null display names are normalized to stable fallback text based on object kind and id.
- Provider exceptions are not swallowed inside row templates; construction errors should fail tests or surface as a provider-level diagnostic before UI projection.

## Testing

Unit tests should cover:

- Fixture provider produces deterministic object order and stable ids.
- Duplicate ids fail during provider construction.
- Hierarchy builds parent/child rows from `SceneSnapshot`.
- Hierarchy selection writes a stable `EditorSelectionSnapshot`.
- Inspector single selection resolves object properties from provider.
- Inspector missing selection produces a validation document.
- Inspector multi-selection keeps summary-only behavior.
- Selection revision changes rebuild Inspector without Feature-to-Feature references.

Verification commands:

```powershell
dotnet test Editor.sln -c Release
git diff --check
```

Debug build/test should also run when no live Studio process is locking `bin\Debug\net10.0\Editor.dll`.

## Acceptance Criteria

- Hierarchy and Inspector no longer depend on `DemoHierarchyDataSource` as their main runtime contract.
- Both panels consume the same provider-backed scene snapshot.
- Selection ids remain stable across Hierarchy and Inspector.
- Inspector remains read-only and does not expose editable transform controls.
- Tests prove provider, Hierarchy projection, Inspector projection and missing selection behavior.
- `docs/Dock系统指南.md` is updated to mark the provider v0 facts after implementation.

## Placement Decision

Use `Core` for the provider contract now, because both Hierarchy and Inspector consume it and it is backend-neutral. Keep the first fixture implementation inside `Features/Hierarchy` or `Features/Workbench` only if it remains clearly test/demo scoped; move it to a neutral shell/service folder only when a second production-style provider appears.
