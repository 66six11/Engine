# Studio Declaration-only Panel Contribution Contract Design

状态：Approved（实施待开始）

更新日期：2026-07-11

关联 Issue：[#235](https://github.com/66six11/Engine/issues/235)

## 1. 目的

在 dependency-free `Asharia.Editor` 中建立正式、UI-neutral、declaration-only 的 Panel contribution contract，使 built-in、项目 `Editor/` 和 Package module 可以在 `EditorModule.Configure()` 中声明 Code-first Panel，并把声明冻结进 `EditorModuleDeclaration`。

本切片只稳定 authoring-time data contract。它不注册 Panel、不绑定 factory、不创建内容、不修改 Dock，也不显示 UI。运行时可见性、Package generation、factory registry、scope transaction 和实例生命周期仍属于未来 Host Slice。

## 2. 当前事实

PR #234 合并后：

- `Asharia.Editor.UI.CodeFirst` 已拥有完整 Code-first authoring/tree/state/events/validation；
- `Asharia.Editor.Panels` 已拥有 `EditorDockArea` 与 panel lifecycle/frame contracts；
- `EditorModuleBuilder` 已冻结 module dependency/capability 声明，并在 `Build()` 后拒绝修改；
- legacy `PanelDescriptor` 仍保存 `Func<object>`，只供现有 Studio compatibility host 使用；
- legacy `EditorPanelContributionDescriptor` 与 validator 仍在 `Editor.Core`，不是正式公共 ABI；
- 当前没有 public contribution ID、UI backend ID、factory identity 或 Panel descriptor。

现有总计划原本让 declaration descriptor 直接保存 `GenerationScopedFactoryHandle(PackageGenerationId, LocalId)`，但当前 `EditorModuleDefinitionContext` 只有 `EditorModuleDefinitionId`，没有 Package generation context。`PackageGenerationId` 还需要精确 resolver closure、artifact selection、managed/native subgraph 与 closure hash 语义，本切片不能伪造或提前冻结。

## 3. 方案比较与决定

### 3.1 立即冻结 Package generation handle

Descriptor 直接保存 `GenerationScopedFactoryHandle`。优点是形状接近原总计划；缺点是 declaration builder 无 generation context，并会把 resolver/reload identity 拉入本 Slice。拒绝。

### 3.2 两阶段 factory identity

声明阶段只保存 validated `EditorFactoryLocalId`。未来 Host staging 已知 candidate generation 和 owner definition 后，再绑定：

```text
PackageGenerationId
+ EditorModuleDefinitionId
+ EditorFactoryLocalId
-> GenerationScopedFactoryHandle
```

选择此方案。它让 authoring contract 可稳定、可测试，同时不承诺尚未实现的 generation resolver。

### 3.3 保存 Type、delegate 或 object factory

直接保存 `Type`、`Func<object>` 或 backend-specific factory 实现最容易接入 legacy host，但会泄漏 CLR/Avalonia/lifetime，阻碍 ALC、reload、Linux/macOS backend 和 headless validation。拒绝。

## 4. 公共 identity

所有 identity 使用 validated readonly record struct，默认值无效，`Value`/`ToString()` 对默认值返回空字符串，提供 `Create` 与 `TryCreate`。

### 4.1 EditorContributionId

Namespace：

```text
Asharia.Editor.Contributions.EditorContributionId
```

语法复用 `ModuleLocalId` 的 lowercase dot-separated namespace：

```text
valid:   terrain.main-panel
valid:   render.frame-debugger
invalid: Terrain.MainPanel
invalid: terrain
invalid: terrain..panel
```

允许 lowercase ASCII letter、digit 和 segment-internal hyphen；必须至少包含一个 dot namespace separator。

Contribution ID 在一个可见 scope partition 内是 logical ID。当前 builder 只能验证同一 module declaration 内唯一；跨 module/package/scope 冲突由未来 Host staging 验证。

### 4.2 EditorFactoryLocalId

Namespace：

```text
Asharia.Editor.Contributions.EditorFactoryLocalId
```

使用与 `EditorContributionId` 相同的 lowercase dot-separated syntax，例如：

```text
terrain.panel.main-content
```

它只在一个 `EditorModuleDefinitionId` 内寻址 factory declaration，不包含 Package generation、CLR type name、assembly-qualified name、delegate 或 process-global token。

同一 module declaration 内 factory local ID 必须唯一。本 Slice 不允许两个 Panel descriptor 共享同一 factory local ID；未来若需要显式 shared factory，必须另定义 lifetime/instance contract，不能靠偶然复用 ID。

### 4.3 UiBackendId

Namespace：

```text
Asharia.Editor.Contributions.UiBackendId
```

语法为 lowercase ASCII kebab ID：

```text
valid:   code-first
valid:   avalonia
invalid: CodeFirst
invalid: code_first
invalid: code--first
```

提供：

```csharp
public static UiBackendId CodeFirst { get; }
```

`CodeFirst.Value` 固定为 `code-first`。公共 contract 允许未来 bridge 声明其他合法 backend ID，但本 Slice 不注册或验证 backend availability。

## 5. Panel descriptor

新增：

```text
Asharia.Editor.Panels.EditorPanelDescriptor
Asharia.Editor.Panels.EditorPanelKind
Asharia.Editor.Panels.EditorDockPreference
Asharia.Editor.Panels.EditorPanelCachePolicy
```

枚举固定为：

```csharp
public enum EditorPanelKind
{
    Document,
    Tool,
}

public enum EditorDockPreference
{
    Center,
    Left,
    Right,
    Bottom,
}

public enum EditorPanelCachePolicy
{
    RecreateOnOpen,
    KeepAlive,
}
```

`EditorDockPreference` 是 descriptor 的默认布局请求；`EditorDockArea` 是运行时已经解析的实际 host area。两者含义不同，不共用类型。

Descriptor 形状：

```csharp
public sealed record EditorPanelDescriptor(
    EditorContributionId Id,
    string Title,
    EditorPanelKind Kind,
    EditorDockPreference DefaultDock,
    EditorPanelCachePolicy CachePolicy,
    UiBackendId Backend,
    EditorFactoryLocalId ContentFactory);
```

实现使用显式 constructor/properties 完成 fail-fast validation，但 public constructor 参数顺序和 property surface 必须与上述 contract 一致。

Authoring example：

```csharp
editor.Panels.Add(new EditorPanelDescriptor(
    EditorContributionId.Create("terrain.main-panel"),
    "Terrain",
    EditorPanelKind.Tool,
    EditorDockPreference.Right,
    EditorPanelCachePolicy.KeepAlive,
    UiBackendId.CodeFirst,
    EditorFactoryLocalId.Create("terrain.panel.main-content")));
```

构造时必须验证：

- `Id`、`Backend`、`ContentFactory` 有效；
- `Title` 非 null、非空白，并保留 caller 提供的原值，不静默 trim；
- enum 值均已定义；
- descriptor 不包含 scope、menu path、runtime status、frame scheduler state 或 content instance。

Module scope 隐式决定 Panel scope：Application module 声明 Application-scoped Panel，Project module 声明对应 `ScopeInstanceId` partition 的 Project Panel。Descriptor 不能重复声明或覆盖 scope。

## 6. Builder 与 immutable declaration

`EditorModuleBuilder` 新增：

```csharp
public EditorPanelContributionBuilder Panels { get; }
```

Builder API：

```csharp
public sealed class EditorPanelContributionBuilder
{
    public void Add(EditorPanelDescriptor descriptor);
}
```

`EditorModuleDeclaration` 新增：

```csharp
public IReadOnlyList<EditorPanelDescriptor> Panels { get; }
```

规则：

- 保留 `Add()` 顺序；
- `Build()` defensive-copy 为只读集合；
- 重复 `Build()` 返回同一个 declaration instance；
- 首次 `Build()` 后任何 `Panels.Add()` 明确抛出 `InvalidOperationException`；
- 同一 module 内重复 `EditorContributionId` 明确失败；
- 同一 module 内重复 `EditorFactoryLocalId` 明确失败；
- descriptor 本身 immutable，builder 不保存 mutable caller collection；
- Panel duplication validation 与 dependency/capability validation 使用同一个 builder freeze boundary。

## 7. Factory binding data flow

本切片的数据流终止在 immutable declaration：

```text
EditorModule.Configure(builder)
  -> builder.Panels.Add(descriptor)
  -> builder.Build()
  -> EditorModuleDeclaration.Panels
```

未来 Host 的数据流为设计约束而非本 Slice 实现：

```text
candidate Package generation + owner module definition
  -> validate local factory registration
  -> bind EditorFactoryLocalId
  -> create GenerationScopedFactoryHandle
  -> stage Panel registry partition
  -> commit
  -> UI backend resolves actual factory
```

Declaration 不能假装 local ID 已经绑定。未绑定、backend 不可用、factory missing、cross-module ID conflict 或 generation mismatch 都由未来 staging diagnostics 表达。

## 8. 错误模型

Authoring-time invalid input fail-fast：

- invalid identity 或 enum：`ArgumentException` / `ArgumentOutOfRangeException`；
- null descriptor：`ArgumentNullException`；
- duplicate contribution/factory ID：`InvalidOperationException`；
- mutation after freeze：`InvalidOperationException`。

这些异常表示 module `Configure()` 的确定性程序错误，不是 runtime UI dialog。未来 Host 对跨 owner conflict、missing factory、backend unavailable 和 generation mismatch 返回结构化 staging diagnostics，本 Slice 不定义这些结果类型。

## 9. 测试与架构门禁

Public API tests 必须覆盖：

- 三种 ID 的 canonical/non-canonical/default cases；
- `UiBackendId.CodeFirst` exact value；
- enum names 与 numeric values；
- descriptor valid construction 与每个 invalid field；
- Panel 添加顺序和 immutable snapshot；
- duplicate contribution ID；
- duplicate factory local ID；
- repeated `Build()` identity；
- freeze 后 mutation；
- Project/Application module 均可声明 Panel，scope 只来自 `DefinitionContext`；
- read-only collection 不能通过 `ICollection<T>.Add` 修改。

Architecture tests 必须继续证明：

- `Asharia.Editor` 无 ProjectReference/PackageReference；
- public source 不含 `Editor.Core`、Avalonia、Studio implementation、native/Vulkan/P/Invoke；
- exported descriptor/property types 不包含 `Type`、delegate、`object` content factory 或 `GenerationScopedFactoryHandle`；
- legacy `PanelDescriptor(Func<object>)` 未迁入 public assembly。

## 10. 不做事项

- 不冻结 `PackageGenerationId` 或 `GenerationScopedFactoryHandle` 最终结构。
- 不实现 factory registration、binding、lookup、content creation 或 removal lease。
- 不实现 Panel registry、Dock commit、Panel instance host 或 UI backend registry。
- 不迁移 legacy `PanelDescriptor`、`EditorPanelContributionDescriptor`、validator 或 adapter。
- 不加入 menu/shortcut/command metadata；Command contribution 是独立 Slice。
- 不加入 lifecycle/frame scheduling descriptor；Code-first panel runtime contract 已由 `CodeFirstEditorPanel` 与 `EditorPanelFrameUpdateRequest` 表达。
- 不实现 Avalonia bridge、Package resolver、asmdef、ALC、reload、native ABI、renderer、Viewport 或 Play Mode。

## 11. 验收标准

- 项目/Package module 可以使用 `EditorModuleBuilder.Panels.Add()` 声明一个有效 Code-first Panel；
- declaration 保存完整 UI-neutral descriptor，按顺序冻结且不可修改；
- module-local duplicate contribution/factory ID 与 post-freeze mutation fail-fast；
- Panel scope 只能来自 module definition；
- descriptor 不含 generation、CLR/Avalonia/Dock implementation/native object；
- public project 继续 dependency-free，现有 Studio runtime 行为完全不变；
- 新旧 Solution、public/architecture tests、格式、编码和 diff 门禁通过；
- 文档明确 local factory identity 与 future generation binding 的两阶段边界。

## 12. 后续顺序

建议后续依赖顺序：

```text
declaration-only Panel contribution
  -> PackageGenerationId + factory registration/binding contract
  -> remaining UI-neutral services and immutable state
  -> Command/Provider contribution descriptors
  -> static Host resolver and scope transaction
  -> Package/asmdef/build/reload
```

Panel declaration contract 通过后，不能直接把 local factory ID 当作可调用 handle；下一 Slice 必须先定义 generation ownership 和 factory registration lifetime。
