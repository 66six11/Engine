# Studio Code-first 公共 API 迁移设计

状态：Approved（实施待开始）

更新日期：2026-07-11

关联 Issue：[#233](https://github.com/66six11/Engine/issues/233)

## 1. 目的

把 Studio 已有的 Code-first authoring、UI tree、state、event 和 validation 从 legacy `Editor` executable 提升到 dependency-free `Asharia.Editor` 公共程序集，使 built-in、项目 `Editor/` 和 Package 扩展能够编译引用同一套 API。

本切片是程序集 ownership 迁移，不是 UI 重写。现有控件语义、状态恢复、command button、panel frame update 和 Avalonia presentation 行为保持不变。Avalonia、Dock、Window 和具体 host 继续由 Studio implementation 拥有。

## 2. 当前事实

当前有 29 个 Code-first production `.cs` 文件，位于：

```text
apps/studio/Core/CodeFirstUI/
  Abstractions/
  Authoring/
  Building/
  Events/
  Models/
  State/
  Validation/
```

5 个纯 Code-first 行为测试位于 `Tests/Editor.Tests/Core/CodeFirstUI/`。Shell host、Avalonia adapter 和 AXAML 测试位于 `Tests/Editor.Tests/Shell/CodeFirstUI/`。

这些 production 文件不引用 Avalonia、P/Invoke、native、`Editor.Shell` 或 `Editor.Features`，但仍有三个 legacy public-contract 依赖：

- `Editor.Core.Models.Diagnostics.EditorDiagnosticSeverity`；
- `Editor.Core.Models.Workbench.WorkbenchCommandExecutionResult`；
- `Editor.Core.Models.Panels` 中的 lifecycle、frame update 和 dock-area 类型。

此外，`CodeFirstEditorPanel.Dispatch*` 当前是 `internal`。移动到 `Asharia.Editor` 后，留在 `Editor` assembly 的 `CodeFirstPanelHostViewModel` 无法继续调用这些方法；生产项目又禁止通过 `InternalsVisibleTo` 绕过边界。

因此原重构计划中“直接移动 `Core/CodeFirstUI/**`”的描述存在隐藏依赖，不能机械执行。

## 3. 方案比较

### 3.1 方案 A：直接移动全部源码

优点是改动表面上最接近原 Task 3。缺点是公共程序集会立即缺少 Diagnostics、Workbench 和 Panels 类型；若反向引用 legacy `Editor`，则依赖方向错误。该方案不可行。

### 3.2 方案 B：只迁移纯 tree kernel

只移动 Models、Events、State 和 Validation，并暂缓 `EditorGui`、`CodeFirstEditorPanel` 与 command integration。该方案 PR 较小，但项目/Package 开发者仍无法编写完整 Code-first panel，会产生一个无法独立使用的半成品公共 API。

### 3.3 方案 C：迁移完整 Code-first，并提升最小前置合同

同一切片先提升 Code-first 必需的三个 UI-neutral contract family，再迁移完整 Code-first API。Shell 通过显式 host SPI 驱动 panel lifecycle，不使用程序集内部访问。

选择方案 C。它保持公共 API 可独立使用，同时把前置提升限制在 Code-first 已经真实消费的类型，不提前引入 contribution descriptor、registry 或 Host resolver。

## 4. Assembly ownership

迁移后的依赖方向是：

```text
Asharia.Editor
  owns Code-first authoring/tree/state/events/validation
  owns minimal Diagnostics/Commands/Panels contracts
  depends on no Studio project and no UI/native package

Editor executable
  references Asharia.Editor
  owns Avalonia adapters, Dock integration and panel host ViewModels

Built-in features during migration
  compile in Editor executable
  consume Asharia.Editor contracts through the ProjectReference
```

`Editor.csproj` 增加对 `src/Asharia.Editor/Asharia.Editor.csproj` 的 `ProjectReference`。现有 `Compile Remove="src\**\*.cs"` 保证公共源码不会被 legacy executable 重复编译。

`Asharia.Editor` 必须继续满足：

- 无 `ProjectReference`；
- 无 `PackageReference`；
- 无 Avalonia type；
- 无 `object` content factory、Dock node、Window 或 Control；
- 无 P/Invoke、native library、OS handle 或 Vulkan type；
- 无 `Editor.Core`、`Editor.Shell`、`Editor.Features` 或 `Asharia.Studio.*` namespace 依赖。

## 5. 公共 namespace 与类型

### 5.1 Code-first

使用单一机械映射：

```text
Editor.Core.CodeFirstUI.*
  -> Asharia.Editor.UI.CodeFirst.*
```

目录映射保持一一对应：

```text
Core/CodeFirstUI/Abstractions -> src/Asharia.Editor/UI/CodeFirst/Abstractions
Core/CodeFirstUI/Authoring    -> src/Asharia.Editor/UI/CodeFirst/Authoring
Core/CodeFirstUI/Building     -> src/Asharia.Editor/UI/CodeFirst/Building
Core/CodeFirstUI/Events       -> src/Asharia.Editor/UI/CodeFirst/Events
Core/CodeFirstUI/Models       -> src/Asharia.Editor/UI/CodeFirst/Models
Core/CodeFirstUI/State        -> src/Asharia.Editor/UI/CodeFirst/State
Core/CodeFirstUI/Validation   -> src/Asharia.Editor/UI/CodeFirst/Validation
```

不提供 compatibility type-forwarder，也不保留第二份 legacy source。所有 current callers 在同一切片更新 namespace。

### 5.2 Diagnostics 前置合同

`EditorDiagnosticSeverity` 移入：

```text
Asharia.Editor.Diagnostics.EditorDiagnosticSeverity
```

成员保持 `Debug / Info / Warning / Error`，不改变数值顺序。它既供 Code-first validation message 使用，也继续供现有 diagnostics projection 使用。

### 5.3 Command 前置合同

去除 public contract 中的 Workbench implementation vocabulary：

```text
WorkbenchCommandExecutionStatus -> EditorCommandExecutionStatus
WorkbenchCommandExecutionResult -> EditorCommandExecutionResult
namespace                       -> Asharia.Editor.Commands
```

保留 `Succeeded / NotFound / Disabled / Failed`、`CommandId`、`Message`、`Succeeded` 属性以及四个 factory method 的现有行为。`IEditorGuiCommandExecutor` 返回 `EditorCommandExecutionResult`。Legacy Workbench router、shortcut、palette 和 status projection 在迁移期直接消费新公共类型，不保留双模型 adapter。

### 5.4 Panel lifecycle 前置合同

以下类型移入 `Asharia.Editor.Panels`：

```text
DockArea                     -> EditorDockArea
EditorPanelLifecycleContext  -> EditorPanelLifecycleContext
EditorPanelFrameUpdateMode   -> EditorPanelFrameUpdateMode
EditorPanelFrameUpdateRequest-> EditorPanelFrameUpdateRequest
EditorPanelFrameContext      -> EditorPanelFrameContext
```

`EditorDockArea` 表示已经解析的实际 host area，仅包含 `Center / Left / Right / Bottom`。未来 contribution descriptor 的 `EditorDockPreference` 是默认布局请求，两者不能合并为一个含义模糊的类型。

frame update validation、manual/visible/active factory、frame sequence、elapsed time 与 repaint request 行为保持不变。

## 6. 跨程序集 panel host SPI

`CodeFirstEditorPanel` 保留 extension author 使用的 protected hooks：

```text
OnCreate
OnEnable
OnGui
OnFrame
OnDisable
OnDestroy
```

新增 public `ICodeFirstEditorPanelHost` SPI，由 `CodeFirstEditorPanel` 显式实现。SPI 暴露 host 所需的 `FrameUpdateRequest` 和 create/enable/build-gui/frame/disable/destroy dispatch；这些成员不会出现在普通 `CodeFirstEditorPanel` 实例的直接 IntelliSense surface。

合同形状固定为：

```csharp
public interface ICodeFirstEditorPanelHost
{
    EditorPanelFrameUpdateRequest FrameUpdateRequest { get; }
    void Create(EditorPanelLifecycleContext context);
    void Enable();
    void BuildGui(EditorGui gui);
    void Frame(EditorPanelFrameContext context);
    void Disable();
    void Destroy();
}
```

`CodeFirstEditorPanel` 显式实现六个 lifecycle method；`FrameUpdateRequest` 继续是 extension author 可以 override 的 public property。SPI method 不启动 task、不持有 Avalonia object，也不自行决定调用线程。

`CodeFirstPanelHostViewModel` 仍接收一个 `CodeFirstEditorPanel`，随后只通过 `ICodeFirstEditorPanelHost` 驱动生命周期。它不能接受任意 SPI implementation 来绕过 base-class authoring contract。

这个 SPI 是跨程序集调用边界，不是 contribution registry、service locator 或用户可替换 Host。生命周期顺序仍由 Studio Shell 决定。

## 7. 测试 ownership

纯 Code-first tests 移入现有大小写稳定的路径：

```text
apps/studio/Tests/Asharia.Editor.Tests/UI/CodeFirst/
```

迁移期不创建并存的 lowercase `tests/`，因为 Windows 不能同时容纳 `Tests/` 与 `tests/`，而 Linux/macOS 文件系统行为可能不同。删除旧测试树时再执行独立、显式的 casing migration。

Shell/Avalonia tests 留在：

```text
apps/studio/Tests/Editor.Tests/Shell/CodeFirstUI/
```

它们继续验证：

- Avalonia control projection；
- input event 回传；
- panel host lifecycle；
- rebuild scheduling；
- AXAML resource 与 style；
- public Code-first tree 到 Studio presentation 的 adapter 行为。

## 8. 迁移顺序

1. 先写 assembly ownership 与 ProjectReference 的 failing tests，确认类型仍属于 `Editor` assembly。
2. 提升并重命名 Diagnostics、Commands、Panels 三组最小前置合同；逐族更新现有 callers 和 focused tests。
3. 写 host SPI failing tests，证明跨 assembly 不依赖 `internal Dispatch*` 或 `InternalsVisibleTo`。
4. 移动 29 个 Code-first production 文件并机械替换 namespace。
5. 给 legacy executable 增加 `Asharia.Editor` ProjectReference，更新 built-in feature、Shell adapter 与 legacy tests。
6. 移动 5 个纯 Code-first tests 到 `Asharia.Editor.Tests`；Shell/Avalonia tests 保持原 ownership。
7. 删除旧 `Core/CodeFirstUI` source tree，证明没有 duplicate implementation。
8. 更新 architecture tests、当前事实和总重构计划，再运行完整门禁。

每个类型族必须先 RED、后 move/rename、再 GREEN；不能先批量移动全部文件后一次性修复编译。

## 9. 错误与回退

本切片不改变 runtime persistence 或 serialized asset schema，因此回退单位是整个 Git commit/PR，不需要数据迁移。

以下情况必须停止迁移，而不是通过新的反向引用修补：

- `Asharia.Editor` 需要引用 legacy `Editor` project；
- 公共 Code-first 类型需要 Avalonia package；
- Shell 必须使用 `InternalsVisibleTo` 才能驱动 panel；
- 同一类型同时由两个 assembly 编译；
- command/panel rename 需要保留两个可变状态模型才能工作。

若某个前置合同被发现依赖更大的 Task 4 graph，应把它拆成独立前置 Slice，而不是把 Task 4 全部吸收到本 PR。

## 10. 不做事项

- 不新增或冻结 Panel、Command、Provider contribution descriptor。
- 不实现 factory handle、Package generation、registry、Host resolver、activation topology 或 reload。
- 不迁移 Avalonia adapter、AXAML、Dock、Window、Shell ViewModel 或 UI dispatcher implementation。
- 不改变 Code-first node schema、控件集合、状态语义或视觉样式。
- 不新增 compatibility facade、type-forwarder 或 `InternalsVisibleTo`。
- 不修改 C++、native ABI、renderer、Viewport 或 Play Mode。

## 11. 验收与验证

验收标准：

- `EditorGui`、`GuiTreeSnapshot`、`GuiFrameBuilder`、`GuiStateStore`、`GuiEventQueue` 和 `GuiTreeValidator` 均由 `Asharia.Editor` assembly 拥有；
- `CodeFirstEditorPanel` 可被 Studio Shell 通过显式 host SPI 驱动；
- public source 不含 `Editor.Core` 或 Studio implementation namespace；
- `Core/CodeFirstUI` 不再存在 production source；
- 纯 Code-first tests 在 `Asharia.Editor.Tests` 通过；
- Shell/Avalonia adapter tests 与 657 项 legacy baseline 不回归；
- 公共程序集继续 dependency-free、UI-neutral、native-neutral；
- 路径和项目结构没有 Windows-only 条件，Linux/macOS 可以使用相同 managed source graph。

验证命令：

```powershell
dotnet build apps/studio/src/Asharia.Editor/Asharia.Editor.csproj -c Release --no-restore -warnaserror
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore
dotnet test apps/studio/Editor.sln -c Release --no-restore
dotnet test apps/studio/Asharia.Studio.sln -c Release --no-restore
dotnet format apps/studio/src/Asharia.Editor/Asharia.Editor.csproj --verify-no-changes --no-restore
dotnet format apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj --verify-no-changes --no-restore
powershell -ExecutionPolicy Bypass -File tools/check-text-encoding.ps1
git diff --check
git diff --cached --check
```

本 Slice 只验证 managed cross-platform source/build boundary。实际 Linux/macOS Studio executable、Avalonia platform backend 和 native viewport smoke 仍属于后续跨平台 CI/viewport Slice。

## 12. 后续顺序

本设计完成后，正式依赖顺序为：

```text
EditorModule identity/lifecycle/declarations
  -> Code-first API + minimal prerequisite contracts
  -> remaining UI-neutral services and immutable state
  -> contribution descriptors and opaque factory handles
  -> static Host/resolver and scope transaction
  -> Package/asmdef/build/reload
```

这修正了原 Task 3 对 legacy contract dependency 的遗漏，但不改变总体 branch-by-abstraction 方向。
