# Studio Public Dialog Contract Design

状态：Implemented

更新日期：2026-07-12

关联 Issue：[#237](https://github.com/66six11/Engine/issues/237)

## 1. 目的

在 dependency-free `Asharia.Editor` 中建立正式、UI-neutral、跨平台可表达的 modal dialog request/result contract，并让现有 Studio compatibility Dialog Host 原子迁移到该公共合同。

本切片只稳定瞬时对话框的数据协议。它不公开 dialog service，不决定 owner window，不创建 Avalonia `Window`/`Control`，不实现平台按钮排序，也不允许扩展注入自定义 modal content。

## 2. 当前仓库事实

实现后的当前事实如下：

- `Asharia.Editor.Dialogs` 拥有本设计的七个 UI-neutral public type；
- action ID、role、default、destructive 与 completion 语义彼此独立；
- request 构造验证全部 invariant，并冻结 action 的防御性只读快照；
- public action 声明顺序保持确定性，但不是跨平台屏幕顺序承诺；
- compatibility Dialog Host 消费公共合同，Presentation 仍拥有 overlay、focus、action projection 和 second-active-modal rejection；
- 用户 system-dismiss result 与未来 operation cancellation 仍是不同终止路径；
- legacy `Editor.Core.Models.Dialogs` 已删除，没有 wrapper、type forwarding 或 duplicate model；
- dialog service、owner-window routing、custom content、platform ordering、localization、file picker、progress、notification 和 modal queue 均未实现。

以下是实施前基线和本切片要解决的风险记录。实施前 `Editor.Core.Models.Dialogs` 包含：

- `EditorDialogKind`：`Information | Confirmation`；
- `EditorDialogButtonRole`：`Accept | Reject | Cancel`；
- `EditorDialogButtonDescriptor`；
- `EditorDialogRequest`；
- `EditorDialogResultKind` 与 `EditorDialogResult`。

这些类型 UI-neutral，但仍编译进 legacy `Editor` executable，而不是正式公共 `Asharia.Editor`。当前实现还有以下公共合同风险：

- `Kind` 同时暗示内容和业务流程，不能独立表达 warning/error；
- `Accept/Reject/Cancel` 让框架猜测调用方业务结果；
- default、safe dismiss 与 destructive style 没有正交表达；
- `EditorDialogRequest.Buttons.ToArray()` 以数组形式暴露为 `IReadOnlyList<T>`，调用者可以强转并修改元素；
- enum 没有构造期合法性校验；
- request 没有阻止重复 button ID、多个 default 或不安全 destructive 组合；
- helper 固定英文 `OK`，会把 localization policy 写入底层合同。

现有 Presentation 由 `EditorDialogHostViewModel` 拥有，一次只允许一个 active dialog，并通过主窗口 overlay 显示。项目/Package extension loader 尚未实现，因此不存在已发布的 `Editor.Core.Models.Dialogs` 外部二进制使用者。

## 3. 外部资料审查

### 3.1 Action 语义必须正交

Windows ContentDialog 区分 Primary、Secondary、Close，并独立指定 Default；Close 表达安全、非破坏性退出：

<https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/dialogs-and-flyouts/dialogs>

Apple Alerts 区分 default、cancel 与 destructive，并要求 destructive action 配套清晰的安全退出：

<https://developer.apple.com/design/human-interface-guidelines/alerts>

GNOME Dialog/Button guidance 同样区分 suggested 与 destructive，并建议一个 view 最多突出一个 suggested/destructive action：

<https://developer.gnome.org/hig/patterns/feedback/dialogs.html>

<https://developer.gnome.org/hig/patterns/controls/buttons.html>

结论：业务 action identity、Primary/Secondary/Dismiss role、keyboard default 与 destructive style 必须分别表达。公共合同不能把它们压缩成 `Accepted/Rejected/Canceled`。

### 3.2 Presentation 拥有平台排列

Windows 与 Apple 对按钮行的惯常位置不同。公共 request 可以保存确定性的声明顺序，但不能承诺该顺序就是屏幕顺序。Presentation 根据 Studio convention 或 Windows/macOS/Linux adapter policy 映射 role、style、focus 和排列。

本切片不实现 OS 分支；它只保证未来实现不需要修改 public ABI。

### 3.3 Modal lifetime 绑定 owner window

Avalonia `ShowDialog<T>(owner)` 要求明确 owner，并以 `Task<T>` 表达该次 modal lifetime：

<https://docs.avaloniaui.net/docs/how-to/dialogs-how-to>

GNOME secondary window 属于某个 primary window，并建议避免 secondary window stack：

<https://developer.gnome.org/hig/patterns/containers/windows.html>

macOS sheet 同样绑定 parent window；系统是否排队 sheet 是 Presentation policy：

<https://developer.apple.com/documentation/appkit/nswindow/sheets>

结论：request 不能保存 Window ID 或全局 current-window 假设。未来 scope-aware dialog service 从 module/scope context 选择 owner-window Host。

### 3.4 Operation cancellation 不是用户结果

.NET cooperative cancellation 把 `CancellationToken` 定义为一次异步 operation 的取消协议：

<https://learn.microsoft.com/en-us/dotnet/standard/threading/cancellation-in-managed-threads>

结论：module unload、Host shutdown 或调用方取消应使未来 `ShowAsync` operation 取消，而不是伪造用户点击 Dismiss。用户 action/system dismiss 才产生 `EditorDialogResult`。

### 3.5 Record 只提供浅不可变性

C# record 的 reference-type property 仍可指向 mutable array：

<https://learn.microsoft.com/en-my/dotnet/csharp/language-reference/builtin-types/record>

`Array.AsReadOnly` 只是 wrapper；若底层数组仍被外部持有，变化仍会穿透：

<https://learn.microsoft.com/en-us/dotnet/api/system.array.asreadonly?view=net-10.0>

结论：request 必须先 `ToArray()` 防御性复制，再 `Array.AsReadOnly()`，且不暴露原数组。集合元素自身必须 immutable。

### 3.6 Enum 与兼容性

C# 允许把任意 integral value 转为 enum；`Enum.IsDefined` 才能确认值属于已定义成员：

<https://learn.microsoft.com/en-us/dotnet/api/system.enum.isdefined?view=net-10.0>

.NET compatibility rules 将修改 namespace/type name 视为 breaking change；type forwarding 只适合形状不变、仅移动 assembly 的类型：

<https://learn.microsoft.com/en-us/dotnet/core/compatibility/library-change-rules>

<https://learn.microsoft.com/en-us/dotnet/standard/assembly/type-forwarding>

结论：新公共合同所有 enum 在构造边界显式校验。由于本次同时更改 namespace、名称和语义，而且 legacy 类型尚未作为 SDK 发布，不使用 type forwarding 或双份 compatibility model。

## 4. 方案比较与决定

### 4.1 原样移动 legacy models

优点是 diff 小。缺点是把混合语义、浅不可变集合和缺失 validation 冻结为 public ABI。拒绝。

### 4.2 设计通用 Prompt/custom-content framework

优点是可以覆盖表单、file picker、wizard 与自定义 UI。缺点是必须提前定义 content factory、Host service resolution、Window ownership 和 generation binding，并把多个独立交互类型耦合到一个协议。拒绝。

### 4.3 重新设计严格的 message/confirmation contract

只覆盖 title/message 与 1–3 个语义 action；复杂表单继续使用 Panel/Tool Window，file picker、background progress 和 notification 使用各自服务。选择此方案。

## 5. 公共 API

所有类型位于：

```text
Asharia.Editor.Dialogs
```

### 5.1 Severity

```csharp
public enum EditorDialogSeverity
{
    Information,
    Warning,
    Error,
}
```

Severity 只影响 Presentation 语义和视觉提示，不决定 action 或 result。

### 5.2 Action role

```csharp
public enum EditorDialogActionRole
{
    Primary,
    Secondary,
    Dismiss,
}
```

- `Primary`：主要业务动作；
- `Secondary`：可选的第二业务动作；
- `Dismiss`：安全、非破坏性退出，例如 Close、Cancel、Not Now。

Role 不决定最终按钮位置。

### 5.3 Action identity

```csharp
public readonly record struct EditorDialogActionId
{
    public string Value { get; }
    public bool IsValid { get; }

    public static EditorDialogActionId Create(string value);
    public static bool TryCreate(string? value, out EditorDialogActionId result);
}
```

规则：

- request-local identity；
- lowercase-kebab，例如 `save`、`dont-save`、`retry`、`close`；
- default value invalid；
- invalid default 的 `Value`/`ToString()` 为空。

Action ID 是业务判断入口。框架不再返回 Accepted/Rejected。

### 5.4 Action descriptor

```csharp
public sealed record EditorDialogActionDescriptor
{
    public EditorDialogActionDescriptor(
        EditorDialogActionId id,
        string text,
        EditorDialogActionRole role,
        bool isDefault = false,
        bool isDestructive = false);

    public EditorDialogActionId Id { get; }
    public string Text { get; }
    public EditorDialogActionRole Role { get; }
    public bool IsDefault { get; }
    public bool IsDestructive { get; }
}
```

Text 是 display-ready string。Localization resource/token system 尚不存在，本切片不伪造该抽象；调用方负责在构造 request 前得到最终显示文本。

### 5.5 Request

```csharp
public sealed class EditorDialogRequest
{
    public EditorDialogRequest(
        EditorDialogSeverity severity,
        string? title,
        string message,
        bool allowSystemDismiss,
        IReadOnlyList<EditorDialogActionDescriptor> actions);

    public EditorDialogSeverity Severity { get; }
    public string? Title { get; }
    public string Message { get; }
    public bool AllowSystemDismiss { get; }
    public IReadOnlyList<EditorDialogActionDescriptor> Actions { get; }
}
```

Title 可以为 `null`；非 null 时必须包含非空白文本。Message 必填。Windows guidance 同样允许省略 title、要求 content/message 存在。

Request 使用 sealed class 而不是 record，因为 collection property 的 record equality 会退化为 collection reference equality，容易暗示不存在的 structural equality。

Request 保存输入 action 的确定性顺序供 diagnostics/tests 使用，但 Presentation 不得把它解释为跨平台屏幕顺序保证。

### 5.6 Result

```csharp
public enum EditorDialogCompletionKind
{
    ActionInvoked,
    SystemDismissed,
}

public sealed record EditorDialogResult
{
    public EditorDialogCompletionKind Completion { get; }
    public EditorDialogActionId? ActionId { get; }

    public static EditorDialogResult ActionInvoked(EditorDialogActionId actionId);
    public static EditorDialogResult SystemDismissed();
}
```

- 点击任何 action（包括 role 为 Dismiss 的 action）返回 `ActionInvoked` 与明确 ID；
- Escape、window close 等非 action 路径在 `AllowSystemDismiss=true` 时返回 `SystemDismissed`；
- operation cancellation 不产生 `EditorDialogResult`。

Result 不公开允许构造矛盾 kind/ID 组合的 constructor。

## 6. Validation invariants

`EditorDialogActionDescriptor`：

- `Id.IsValid`；
- `Text` 非空白；
- `Role` 由 `Enum.IsDefined` 验证。

`EditorDialogRequest`：

- `Severity` 由 `Enum.IsDefined` 验证；
- Title 为 null 或非空白；
- Message 非空白；
- Actions 非 null，数量 1–3；
- action item 非 null；
- action ID 唯一；
- action role 唯一，因此 Primary/Secondary 各最多一个；
- 恰好一个 Dismiss；
- 最多一个 default；
- 最多一个 destructive；
- 被 default 或 destructive 标记的 action 合计最多一个；两种标记可以落在同一个明确 action 上，但不能分别突出两个 action；
- Dismiss 不得 destructive；
- 构造函数完成防御性复制和只读包装。

`EditorDialogResult`：

- `ActionInvoked` 拒绝 invalid ID；
- `ActionInvoked` 总有 ID；
- `SystemDismissed` 总无 ID。

## 7. Ownership 与 lifetime

```text
Extension / built-in command
  -> immutable EditorDialogRequest
  -> future scope-aware dialog service
  -> owner-window Dialog Host
  -> Presentation role/style/order mapping
  -> EditorDialogResult | operation cancellation
```

- `Asharia.Editor` 只拥有 immutable data contract；
- Presentation 拥有 Window、overlay、focus、keyboard、button order 与 pending completion；
- request 不包含 Window、Control、ViewModel、TopLevel handle 或 owner window ID；
- future dialog service 从 module/scope context 解析 owner Host；
- concurrency/queue policy 属于每个 owner Host，不进入 request/result；
- 当前 compatibility host 继续一次只允许一个 active dialog，第二次请求明确失败；
- host 必须 single-complete：action/system dismiss 竞态只能有一个 terminal result；
- future service 接收 `CancellationToken`，用于 caller/module/Host operation lifetime；该接口不在本切片实现。

## 8. Compatibility migration

新增：

```text
apps/studio/src/Asharia.Editor/Dialogs/
  EditorDialogSeverity.cs
  EditorDialogActionRole.cs
  EditorDialogActionId.cs
  EditorDialogActionDescriptor.cs
  EditorDialogCompletionKind.cs
  EditorDialogRequest.cs
  EditorDialogResult.cs
```

删除：

```text
apps/studio/Core/Models/Dialogs/**
```

同一 PR 更新：

- `EditorDialogHostViewModel`；
- `EditorDialogButtonViewModel`；
- `EditorDialogHostDesignViewModel`；
- `MainWindowViewModel` About dialog path；
- Dialog view/view-model tests；
- legacy physical-directory architecture assertions。

不创建旧 namespace wrapper、duplicate DTO 或 type forwarding。新的 `Asharia.Editor.Dialogs` 是正式 public ABI 起点。

Compatibility host 可以暂时保留统一 Studio action order，但必须根据 role/style/default 创建按钮 view model，不能再根据 Accept/Reject enum 推导业务 result。

## 9. Error handling

- 所有 invalid request shape 在 public constructor fail-fast；
- Host 不重复验证已由 request 保证的 structural invariant，但仍验证 runtime state，例如 second active request；
- `AllowSystemDismiss=false` 时 Escape/window-close bridge 不完成 result；
- action 完成后清空 active state，再发布 completion；
- 重复 action、Escape/action race 或 close/action race 不得完成两次；
- future operation cancellation 清理 pending host state并以 `OperationCanceledException` 结束，不返回 `SystemDismissed`；
- Presentation mapping failure 属于 Host diagnostic/failure，不修改 request。

## 10. Testing strategy

### 10.1 Public contract tests

在 `Tests/Asharia.Editor.Tests/Dialogs/**` 覆盖：

- enum name/value stability；
- Action ID valid/invalid/default/ToString；
- valid action descriptor；
- invalid ID/text/role；
- valid 1/2/3-action requests；
- null/blank title/message；
- invalid severity；
- null/empty/too-many/null-item actions；
- duplicate ID/role；
- missing/multiple Dismiss；
- multiple default/destructive；
- default 与 destructive 分别落在两个 action；
- destructive Dismiss；
- caller collection mutation 不穿透 request；
- `ICollection<T>.Add` 无法修改公开集合；
- result factory invariants。

### 10.2 Compatibility tests

保留并调整：

- initial closed state；
- About dialog opens；
- action projection exposes text/default/destructive；
- action click returns exact action ID；
- system dismiss allowed/prohibited；
- second active request rejects；
- terminal state clears once；retained/repeated action signals and an action losing to system-dismiss cannot complete a later request。

### 10.3 Architecture gates

- public Dialog types assembly ownership 为 `Asharia.Editor`；
- `Asharia.Editor` 继续无 ProjectReference/PackageReference；
- public Dialog source、type properties 与 authored constructor/static-method inputs 不含 Avalonia、Window、Control、delegate、`object` factory、`CancellationToken`、native/Vulkan/Studio implementation vocabulary；
- legacy `Core/Models/Dialogs` 不再存在；
- legacy `Editor` 通过 ProjectReference 消费新合同。

### 10.4 Full gates

```powershell
dotnet build apps/studio/src/Asharia.Editor/Asharia.Editor.csproj -c Release --no-restore -warnaserror
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~Dialog
dotnet test apps/studio/Editor.sln -c Release --no-restore
dotnet test apps/studio/Asharia.Studio.sln -c Release --no-restore
```

随后运行 changed-project format、repository encoding、strict UTF-8 without BOM、doc sync 与 diff gates。

纯 public contract 不含 OS branch，可在 Windows/Linux/macOS 使用同一 managed tests。平台特定 button ordering 需要未来 Presentation Slice 的实际 per-platform test，不在本 Slice 伪造验证。

## 11. 不做事项

- 不新增 `IEditorDialogService`；
- 不修改 `EditorModuleContext` service resolution；
- 不实现 modal queue、跨窗口调度或多 Dialog stack；
- 不允许 custom content、Code-first/Avalonia content factory；
- 不实现 file/folder picker、progress、toast、notification 或 wizard；
- 不实现 localization resource/token system；
- 不实现 native OS dialog；
- 不实现 Windows/macOS/Linux 专属 button ordering；
- 不修改 Package generation、factory binding、ALC、reload、Dock、Viewport、renderer、C++ 或 Play Mode。

## 12. 验收标准

- `Asharia.Editor.Dialogs` 暴露本设计的 7 个 public types；
- contract construction 确定性拒绝所有 invalid invariant；
- request actions 是 defensive read-only snapshot；
- result 不允许矛盾 completion/action ID；
- legacy Dialog host 与 About path 只消费 public contract；
- legacy Dialog model 源被删除且没有 forwarding/duplicate model；
- public assembly 继续 dependency-free 与 UI/native-neutral；
- focused public/compatibility/architecture tests 与两套 Solution 全部通过；
- docs、format、encoding 和 diff gates 通过；
- Windows/Linux/macOS Presentation 可以在不改 public ABI 的情况下实现不同 action layout policy。

## 13. 后续顺序

本 Slice 完成后仍不应立即实现通用 Prompt system。建议顺序：

```text
public Dialog data contract
  -> Asharia.Studio.Application static Host + scope service resolution
  -> IEditorDialogService async/cancellation contract
  -> Presentation owner-window routing and per-platform action layout tests
```

File picker、background progress、notification 和 complex form 分别设计，不复用 Dialog request 承载不相关职责。

## 14. 实施与验收证据

实施提交按 RED → GREEN 完成：Task 1 在 `Asharia.Editor.Dialogs` 不存在时得到预期 compile RED，随后 action/identity focused suite 33/33 GREEN；Task 2 在 `EditorDialogRequest`/`EditorDialogResult` 不存在时得到预期 compile RED，随后 Dialog public suite 23/23、完整 public suite 174/174 GREEN；Task 3 在 compatibility Host 仍使用 legacy request/result 时得到预期 compile RED，随后 Host/View 9/9 GREEN，并完成 About、projection、exact action ID、system dismiss 与 single-active-modal migration；最终 review 又以三个 real-code regression 得到精确 3/3 assertion RED，随后用 generation-scoped completion 修复达到 Host/View 12/12 GREEN；Task 4 的 architecture gate 因 legacy physical folder 仍存在得到预期 assertion RED，删除六个 legacy model 后 focused gate 1/1、完整 architecture suite 8/8 GREEN。

2026-07-12 的最终 fresh validation：

- `Asharia.Editor` 与 architecture warning-as-error build 均为 0 warning、0 error；
- public tests 174/174、architecture tests 8/8、focused Dialog/MainWindow compatibility tests 49/49；
- `Editor.sln` 为 602/602；`Asharia.Studio.sln` 为 public 174/174、architecture 8/8、legacy 602/602；全部 0 failed、0 skipped；
- 四个 changed-project `dotnet format --verify-no-changes` 全部通过；
- repository encoding 检查 743 files，0 missing BOM、0 unexpected BOM、0 invalid UTF-8；599 个 tracked existing `.cs`/`.md` 文件通过 strict UTF-8 decoding 且没有 leading BOM；
- doc sync、`git diff --check` 和 `git diff --cached --check` 通过。

最终边界保持不变：本 Slice 只发布数据合同并迁移 compatibility Host；后续依赖顺序是 Application static Host/scope service resolution → `IEditorDialogService` async/cancellation contract → Presentation owner-window routing 与 Windows/Linux/macOS per-platform layout tests。
