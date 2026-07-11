# Studio Code-first Public API Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the complete UI-neutral Code-first editor authoring API into dependency-free `Asharia.Editor` while preserving existing Studio behavior and keeping Avalonia/Shell implementation private.

**Architecture:** Establish the legacy executable → public API ProjectReference first, then promote the three real prerequisite contract families in dependency order: Diagnostics, Commands, and Panels. Move the Code-first kernel before authoring/host lifecycle, introduce an explicit cross-assembly host SPI, and delete the legacy source only after equivalent tests run from `Asharia.Editor.Tests`.

**Tech Stack:** .NET 10, C# 14, xUnit, SDK-style projects, Avalonia 12 only in the legacy Studio implementation, PowerShell validation on Windows with platform-neutral managed source for Linux/macOS.

**Implementation status:** Tasks 1–7 are implemented in commits `3680c8be` through the final Task 7 docs/gates commit. Validation and Issue/PR evidence are prepared locally; publishing remains reserved for the controller after whole-branch review.

| Task | Actual status |
| --- | --- |
| 1. Project boundary | Implemented |
| 2. Diagnostics prerequisite | Implemented |
| 3. Commands prerequisite | Implemented |
| 4. Panels prerequisite | Implemented |
| 5. Code-first kernel | Implemented |
| 6. Authoring and host SPI | Implemented |
| 7. Architecture gates and documentation | Implemented |

## Global Constraints

- `Asharia.Editor` remains `net10.0` with no `ProjectReference` and no `PackageReference`.
- Public source must not reference Avalonia, P/Invoke, native libraries, Vulkan, `Editor.Core`, `Editor.Shell`, `Editor.Features`, or `Asharia.Studio.*`.
- Production projects must not add `InternalsVisibleTo`; cross-assembly panel dispatch uses the explicit public host SPI from Task 6.
- Do not add contribution descriptors, registry/Host resolver behavior, Package generation, asmdef, ALC, reload, C++, native ABI, renderer, Viewport, or Play Mode changes.
- Preserve Code-first control behavior, node/state/event schema, command results, panel lifecycle order, and Avalonia presentation.
- Use the existing uppercase `apps/studio/Tests/` tree; do not create a parallel lowercase `tests/` tree.
- Managed `.cs`, `.csproj`, `.sln`, and Markdown changes are strict UTF-8 without BOM; do not use `dotnet sln` without removing any introduced BOM.
- Preserve user-owned untracked `apps/studio/.vs/` and `qodana.yaml`.
- Every production behavior change follows RED → GREEN; every task ends with a buildable commit.
- Baseline evidence: 58 pure Code-first tests, 657 legacy tests, 47 public API tests, and 4 architecture tests pass before implementation.

---

### Task 1: Add the legacy executable → public API project boundary

**Files:**

- Modify: `apps/studio/Tests/Asharia.Studio.Architecture.Tests/ProjectReferenceGraphTests.cs`
- Modify: `apps/studio/Editor.csproj`

**Interfaces:**

- Consumes: existing `apps/studio/src/Asharia.Editor/Asharia.Editor.csproj`.
- Produces: a compile-time `Editor.csproj` → `Asharia.Editor.csproj` reference used by all later type moves.

- [ ] **Step 1: Write the failing exact-reference test**

Add this test to `ProjectReferenceGraphTests`:

```csharp
[Fact]
public void Legacy_editor_references_only_the_public_editor_project()
{
    var projectPath = Path.Combine(FindStudioRoot(), "Editor.csproj");
    var project = XDocument.Load(projectPath);

    var references = project
        .Descendants("ProjectReference")
        .Select(element => element.Attribute("Include")?.Value.Replace('\\', '/'))
        .Where(value => value is not null)
        .Order(StringComparer.Ordinal)
        .ToArray();

    Assert.Equal(["src/Asharia.Editor/Asharia.Editor.csproj"], references);
}
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
dotnet test apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~Legacy_editor_references_only_the_public_editor_project
```

Expected: FAIL because `references` is empty.

- [ ] **Step 3: Add the ProjectReference**

Add this item group to `Editor.csproj` after the source exclusions:

```xml
<ItemGroup>
  <ProjectReference Include="src\Asharia.Editor\Asharia.Editor.csproj" />
</ItemGroup>
```

Do not add a reverse reference to `Asharia.Editor.csproj`.

- [ ] **Step 4: Verify GREEN and the baseline build**

Run:

```powershell
dotnet test apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~Legacy_editor_references_only_the_public_editor_project
dotnet build apps/studio/Editor.csproj -c Release --no-restore
```

Expected: focused test PASS; both `Asharia.Editor.dll` and `Editor.dll` build with 0 errors.

- [ ] **Step 5: Commit**

```powershell
git add apps/studio/Editor.csproj apps/studio/Tests/Asharia.Studio.Architecture.Tests/ProjectReferenceGraphTests.cs
git commit -m "build(studio): reference public editor api"
```

---

### Task 2: Promote diagnostic severity into `Asharia.Editor`

**Files:**

- Create: `apps/studio/src/Asharia.Editor/Diagnostics/EditorDiagnosticSeverity.cs`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/PublicApi/PublicPrerequisiteContractTests.cs`
- Delete: `apps/studio/Core/Models/Diagnostics/EditorDiagnosticSeverity.cs`
- Modify: `apps/studio/Core/Abstractions/IEditorDiagnosticService.cs`
- Modify: `apps/studio/Core/CodeFirstUI/Authoring/EditorGui.cs`
- Modify: `apps/studio/Core/CodeFirstUI/Building/GuiFrameBuilder.cs`
- Modify: `apps/studio/Core/CodeFirstUI/Models/GuiNodePayload.cs`
- Modify: `apps/studio/Core/Models/Diagnostics/EditorDiagnosticRecord.cs`
- Modify: `apps/studio/Core/Services/EditorDiagnosticService.cs`
- Modify: `apps/studio/Features/FrameDebugger/FrameDebuggerPanel.cs`
- Modify: `apps/studio/Features/SceneView/ViewModels/SceneViewPanelViewModel.cs`
- Modify: `apps/studio/Features/UiStyle/UiStylePanel.cs`
- Modify: `apps/studio/Shell/CodeFirstUI/Adapters/GuiAvaloniaControlFactory.cs`
- Modify: `apps/studio/Shell/ViewModels/Windowing/MainWindowViewModel.cs`
- Modify: all test files returned by `rg -l "EditorDiagnosticSeverity" apps/studio/Tests -g '*.cs'`

**Interfaces:**

- Consumes: the Task 1 ProjectReference.
- Produces: `Asharia.Editor.Diagnostics.EditorDiagnosticSeverity` with numeric order `Debug=0`, `Info=1`, `Warning=2`, `Error=3`.

- [ ] **Step 1: Write the failing public ownership test**

Create `PublicPrerequisiteContractTests.cs`:

```csharp
using System;
using Asharia.Editor.Diagnostics;
using Xunit;

namespace Asharia.Editor.Tests.PublicApi;

public sealed class PublicPrerequisiteContractTests
{
    [Fact]
    public void Diagnostic_severity_is_owned_by_public_editor_api()
    {
        Assert.Equal("Asharia.Editor", typeof(EditorDiagnosticSeverity).Assembly.GetName().Name);
        Assert.Equal(
            ["Debug", "Info", "Warning", "Error"],
            Enum.GetNames<EditorDiagnosticSeverity>());
    }
}
```

- [ ] **Step 2: Run and verify compile RED**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~Diagnostic_severity_is_owned_by_public_editor_api
```

Expected: compile FAIL because `Asharia.Editor.Diagnostics` does not exist.

- [ ] **Step 3: Move the enum and update all consumers**

Create the public file with this exact content:

```csharp
namespace Asharia.Editor.Diagnostics;

public enum EditorDiagnosticSeverity
{
    Debug,
    Info,
    Warning,
    Error,
}
```

Delete the legacy file. Replace every `using Editor.Core.Models.Diagnostics;` that exists only for the severity enum with `using Asharia.Editor.Diagnostics;`. Files that also use `EditorDiagnosticRecord`, channel, or source types keep both namespaces.

Verify no old definition remains:

```powershell
rg -n "enum EditorDiagnosticSeverity|Editor\.Core\.Models\.Diagnostics\.EditorDiagnosticSeverity" apps/studio -g '*.cs'
```

Expected: exactly one enum definition under `src/Asharia.Editor`; no fully qualified legacy reference.

- [ ] **Step 4: Verify GREEN and diagnostics regressions**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~Diagnostic_severity_is_owned_by_public_editor_api
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~EditorDiagnosticServiceTests|FullyQualifiedName~ProblemsPanelViewModelTests|FullyQualifiedName~ConsolePanelViewModelTests"
dotnet build apps/studio/Editor.csproj -c Release --no-restore
```

Expected: all selected tests PASS; build has 0 errors.

- [ ] **Step 5: Commit**

```powershell
git add apps/studio/src/Asharia.Editor/Diagnostics apps/studio/Tests/Asharia.Editor.Tests/PublicApi apps/studio/Core apps/studio/Features apps/studio/Shell apps/studio/Tests/Editor.Tests
git commit -m "refactor(studio): promote diagnostic severity"
```

---

### Task 3: Promote command execution results without Workbench vocabulary

**Files:**

- Create: `apps/studio/src/Asharia.Editor/Commands/EditorCommandExecutionStatus.cs`
- Create: `apps/studio/src/Asharia.Editor/Commands/EditorCommandExecutionResult.cs`
- Modify: `apps/studio/Tests/Asharia.Editor.Tests/PublicApi/PublicPrerequisiteContractTests.cs`
- Delete: `apps/studio/Core/Models/Workbench/WorkbenchCommandExecutionStatus.cs`
- Delete: `apps/studio/Core/Models/Workbench/WorkbenchCommandExecutionResult.cs`
- Modify: `apps/studio/Core/CodeFirstUI/Abstractions/IEditorGuiCommandExecutor.cs`
- Modify: `apps/studio/Core/CodeFirstUI/Authoring/EditorGui.cs`
- Modify: `apps/studio/Core/Models/Workbench/EditorStatusMessageSnapshot.cs`
- Modify: `apps/studio/Shell/CodeFirstUI/Hosting/CodeFirstPanelHostViewModel.cs`
- Modify: `apps/studio/Shell/Commands/WorkbenchCommandRouter.cs`
- Modify: `apps/studio/Shell/Commands/WorkbenchCommandStatusMessageRouter.cs`
- Modify: `apps/studio/Shell/Commands/WorkbenchShortcutRouter.cs`
- Modify: `apps/studio/Shell/ViewModels/CommandPalette/CommandPaletteViewModel.cs`
- Modify: `apps/studio/Shell/ViewModels/Menus/PanelMenuItemViewModel.cs`
- Modify: `apps/studio/Shell/ViewModels/Menus/WorkbenchMenuItemViewModel.cs`
- Modify: `apps/studio/Shell/ViewModels/Windowing/MainWindowViewModel.cs`
- Modify: all test files returned by `rg -l "WorkbenchCommandExecution(Result|Status)" apps/studio/Tests -g '*.cs'`

**Interfaces:**

- Consumes: public `Asharia.Editor` reference from Task 1.
- Produces: `EditorCommandExecutionStatus` and immutable `EditorCommandExecutionResult` in `Asharia.Editor.Commands`.

- [ ] **Step 1: Add failing command contract tests**

Append to `PublicPrerequisiteContractTests`:

```csharp
[Theory]
[InlineData(EditorCommandExecutionStatus.Succeeded, true)]
[InlineData(EditorCommandExecutionStatus.NotFound, false)]
[InlineData(EditorCommandExecutionStatus.Disabled, false)]
[InlineData(EditorCommandExecutionStatus.Failed, false)]
public void Command_result_preserves_status_semantics(
    EditorCommandExecutionStatus status,
    bool succeeded)
{
    var result = new EditorCommandExecutionResult(status, "workbench.test", "message");

    Assert.Equal(succeeded, result.Succeeded);
    Assert.Equal("workbench.test", result.CommandId);
}

[Fact]
public void Command_result_factories_preserve_messages()
{
    Assert.True(EditorCommandExecutionResult.Success("workbench.test").Succeeded);
    Assert.Equal(
        "Command 'missing.command' is not registered.",
        EditorCommandExecutionResult.NotFound("missing.command").Message);
    Assert.Equal(
        "Disabled by test",
        EditorCommandExecutionResult.Disabled("workbench.test", "Disabled by test").Message);
    Assert.Equal(
        "Failed by test",
        EditorCommandExecutionResult.Failed("workbench.test", "Failed by test").Message);
}
```

Add `using Asharia.Editor.Commands;`.

- [ ] **Step 2: Verify compile RED**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~Command_result
```

Expected: compile FAIL because the public command types do not exist.

- [ ] **Step 3: Implement the renamed public contract**

Create the enum:

```csharp
namespace Asharia.Editor.Commands;

public enum EditorCommandExecutionStatus
{
    Succeeded,
    NotFound,
    Disabled,
    Failed,
}
```

Create the result by copying current factory behavior and replacing both Workbench type names with Editor type names:

```csharp
namespace Asharia.Editor.Commands;

public sealed record EditorCommandExecutionResult(
    EditorCommandExecutionStatus Status,
    string CommandId,
    string? Message = null)
{
    public bool Succeeded => Status == EditorCommandExecutionStatus.Succeeded;

    public static EditorCommandExecutionResult Success(string commandId) =>
        new(EditorCommandExecutionStatus.Succeeded, commandId);

    public static EditorCommandExecutionResult NotFound(string commandId) =>
        new(
            EditorCommandExecutionStatus.NotFound,
            commandId,
            $"Command '{commandId}' is not registered.");

    public static EditorCommandExecutionResult Disabled(string commandId, string disabledReason) =>
        new(EditorCommandExecutionStatus.Disabled, commandId, disabledReason);

    public static EditorCommandExecutionResult Failed(string commandId, string message) =>
        new(EditorCommandExecutionStatus.Failed, commandId, message);
}
```

Delete the two legacy files. Mechanically replace `WorkbenchCommandExecutionResult` with `EditorCommandExecutionResult`, `WorkbenchCommandExecutionStatus` with `EditorCommandExecutionStatus`, and add `using Asharia.Editor.Commands;` where required. Do not create adapters or aliases.

- [ ] **Step 4: Verify GREEN and command regressions**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~Command_result
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~WorkbenchCommandRouterTests|FullyQualifiedName~WorkbenchShortcutRouterTests|FullyQualifiedName~CommandPaletteViewModelTests|FullyQualifiedName~EditorStatusMessageSnapshotTests"
rg -n "WorkbenchCommandExecution(Result|Status)" apps/studio -g '*.cs'
```

Expected: tests PASS; `rg` returns no matches.

- [ ] **Step 5: Commit**

```powershell
git add apps/studio/src/Asharia.Editor/Commands apps/studio/Tests apps/studio/Core apps/studio/Shell
git commit -m "refactor(studio): promote command execution contract"
```

---

### Task 4: Promote panel lifecycle and frame contracts

**Files:**

- Create: `apps/studio/src/Asharia.Editor/Panels/EditorDockArea.cs`
- Create: `apps/studio/src/Asharia.Editor/Panels/EditorPanelLifecycleContext.cs`
- Create: `apps/studio/src/Asharia.Editor/Panels/EditorPanelFrameUpdateMode.cs`
- Create: `apps/studio/src/Asharia.Editor/Panels/EditorPanelFrameUpdateRequest.cs`
- Create: `apps/studio/src/Asharia.Editor/Panels/EditorPanelFrameContext.cs`
- Modify: `apps/studio/Tests/Asharia.Editor.Tests/PublicApi/PublicPrerequisiteContractTests.cs`
- Delete: `apps/studio/Core/Models/Panels/DockArea.cs`
- Delete: `apps/studio/Core/Models/Panels/EditorPanelLifecycleContext.cs`
- Delete: `apps/studio/Core/Models/Panels/EditorPanelFrameUpdateMode.cs`
- Delete: `apps/studio/Core/Models/Panels/EditorPanelFrameUpdateRequest.cs`
- Delete: `apps/studio/Core/Models/Panels/EditorPanelFrameContext.cs`
- Modify: all production and test files returned by `rg -l "DockArea|EditorPanelLifecycleContext|EditorPanelFrame(UpdateMode|UpdateRequest|Context)" apps/studio -g '*.cs'`

**Interfaces:**

- Consumes: public API project reference from Task 1.
- Produces: `EditorDockArea`, `EditorPanelLifecycleContext`, `EditorPanelFrameUpdateMode`, `EditorPanelFrameUpdateRequest`, and `EditorPanelFrameContext` in `Asharia.Editor.Panels`.

- [ ] **Step 1: Add failing panel contract tests**

Append to `PublicPrerequisiteContractTests` and add `using Asharia.Editor.Panels;`:

```csharp
[Fact]
public void Panel_frame_request_validates_rate_and_mode()
{
    Assert.Same(EditorPanelFrameUpdateRequest.Manual, EditorPanelFrameUpdateRequest.Manual);
    Assert.Equal(30d, EditorPanelFrameUpdateRequest.Active(30d).TargetFramesPerSecond);
    Assert.Equal(10d, EditorPanelFrameUpdateRequest.Visible(10d).TargetFramesPerSecond);
    Assert.Throws<ArgumentOutOfRangeException>(() => EditorPanelFrameUpdateRequest.Visible(0d));
    Assert.Throws<ArgumentOutOfRangeException>(
        () => new EditorPanelFrameUpdateRequest((EditorPanelFrameUpdateMode)42));
}

[Fact]
public void Panel_frame_context_preserves_host_state_and_repaint_request()
{
    var panel = new EditorPanelLifecycleContext(
        "render.frame-debugger",
        "Frame Debugger",
        EditorDockArea.Bottom,
        IsFloatingWorkspace: false);
    var context = new EditorPanelFrameContext(
        panel,
        DateTimeOffset.UnixEpoch,
        TimeSpan.FromMilliseconds(16),
        sequence: 7);

    context.RequestRepaint();

    Assert.True(panel.IsMainWorkspace);
    Assert.True(context.IsRepaintRequested);
    Assert.Equal(7, context.Sequence);
}
```

- [ ] **Step 2: Verify compile RED**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~Panel_frame_request|FullyQualifiedName~Panel_frame_context"
```

Expected: compile FAIL because `Asharia.Editor.Panels` does not exist.

- [ ] **Step 3: Move and rename the five public contracts**

Copy the existing lifecycle/frame validation and behavior exactly, changing only namespace and `DockArea` → `EditorDockArea`. The enum is:

```csharp
namespace Asharia.Editor.Panels;

public enum EditorDockArea
{
    Center,
    Left,
    Right,
    Bottom,
}
```

Delete the five legacy definitions. Replace type references throughout production/tests and add `using Asharia.Editor.Panels;`. `EditorPanelContributionDescriptor`, `PanelDescriptor`, layout snapshots, Dock ViewModels, scheduler, and validation code all consume the public enum/contracts after this task; do not retain a second `DockArea` enum.

- [ ] **Step 4: Verify GREEN and panel regressions**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~Panel_frame_request|FullyQualifiedName~Panel_frame_context"
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~EditorPanelFrameSchedulerTests|FullyQualifiedName~EditorDockWorkspaceViewModelTests|FullyQualifiedName~PanelInstanceManagerTests|FullyQualifiedName~SceneViewPanelViewModelTests"
rg -n "\bDockArea\b|namespace Editor\.Core\.Models\.Panels;[\s\S]*EditorPanel(Frame|Lifecycle)" apps/studio -g '*.cs'
```

Expected: tests PASS; no standalone legacy `DockArea` symbol remains. Other legacy panel model files may retain `namespace Editor.Core.Models.Panels` but import the public contracts.

- [ ] **Step 5: Commit**

```powershell
git add apps/studio/src/Asharia.Editor/Panels apps/studio/Tests apps/studio/Core apps/studio/Features apps/studio/Shell
git commit -m "refactor(studio): promote panel lifecycle contracts"
```

---

### Task 5: Move the Code-first tree/state kernel and 28 kernel test cases

**Files:**

- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Building/GuiFrameBuilder.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Events/GuiEventQueue.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiColorValue.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiListItem.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiNavigationItem.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiNode.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiNodeId.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiNodeKind.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiNodePayload.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiRebuildReason.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiSplitDirection.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiTextInputCommitMode.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiTextSize.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiTextTone.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiTreeSnapshot.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiVector2Value.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiVector3Value.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/GuiVector4Value.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/State/GuiStateStore.cs`
- Create: all 4 files currently under `apps/studio/Core/CodeFirstUI/Validation/` at matching public paths
- Delete: the matching legacy Building, Events, Models, State, and Validation files
- Create: `apps/studio/Tests/Asharia.Editor.Tests/UI/CodeFirst/CodeFirstAssemblyOwnershipTests.cs`
- Move: `apps/studio/Tests/Editor.Tests/Core/CodeFirstUI/Building/GuiFrameBuilderTests.cs` to `apps/studio/Tests/Asharia.Editor.Tests/UI/CodeFirst/Building/GuiFrameBuilderTests.cs`
- Move: `apps/studio/Tests/Editor.Tests/Core/CodeFirstUI/Events/GuiEventQueueTests.cs` to `apps/studio/Tests/Asharia.Editor.Tests/UI/CodeFirst/Events/GuiEventQueueTests.cs`
- Move: `apps/studio/Tests/Editor.Tests/Core/CodeFirstUI/State/GuiStateStoreTests.cs` to `apps/studio/Tests/Asharia.Editor.Tests/UI/CodeFirst/State/GuiStateStoreTests.cs`
- Move: `apps/studio/Tests/Editor.Tests/Core/CodeFirstUI/Validation/GuiTreeValidatorTests.cs` to `apps/studio/Tests/Asharia.Editor.Tests/UI/CodeFirst/Validation/GuiTreeValidatorTests.cs`
- Modify: `apps/studio/Core/CodeFirstUI/Authoring/EditorGui.cs`
- Modify: `apps/studio/Core/CodeFirstUI/Authoring/GuiNavigationPage.cs`
- Modify: `apps/studio/Shell/CodeFirstUI/Adapters/GuiAvaloniaControlFactory.cs`
- Modify: `apps/studio/Shell/CodeFirstUI/Adapters/IGuiAvaloniaHost.cs`
- Modify: `apps/studio/Shell/CodeFirstUI/Hosting/CodeFirstPanelHostViewModel.cs`
- Modify: all Shell/feature tests importing one of the moved kernel namespaces

**Interfaces:**

- Consumes: public Diagnostics from Task 2.
- Produces: `Asharia.Editor.UI.CodeFirst.Building`, `.Events`, `.Models`, `.State`, and `.Validation` with unchanged behavior.

- [ ] **Step 1: Write the failing kernel ownership test**

Create:

```csharp
using System;
using Asharia.Editor.UI.CodeFirst.Building;
using Asharia.Editor.UI.CodeFirst.Events;
using Asharia.Editor.UI.CodeFirst.Models;
using Asharia.Editor.UI.CodeFirst.State;
using Asharia.Editor.UI.CodeFirst.Validation;
using Xunit;

namespace Asharia.Editor.Tests.UI.CodeFirst;

public sealed class CodeFirstAssemblyOwnershipTests
{
    [Theory]
    [InlineData(typeof(GuiFrameBuilder))]
    [InlineData(typeof(GuiEventQueue))]
    [InlineData(typeof(GuiTreeSnapshot))]
    [InlineData(typeof(GuiStateStore))]
    [InlineData(typeof(GuiTreeValidator))]
    public void Kernel_type_is_owned_by_public_editor_api(Type type)
    {
        Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name);
    }
}
```

- [ ] **Step 2: Verify compile RED**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~Kernel_type_is_owned_by_public_editor_api
```

Expected: compile FAIL because `Asharia.Editor.UI.CodeFirst` does not exist.

- [ ] **Step 3: Move kernel sources with one namespace mapping**

For every moved source, replace:

```text
Editor.Core.CodeFirstUI.Building   -> Asharia.Editor.UI.CodeFirst.Building
Editor.Core.CodeFirstUI.Events     -> Asharia.Editor.UI.CodeFirst.Events
Editor.Core.CodeFirstUI.Models     -> Asharia.Editor.UI.CodeFirst.Models
Editor.Core.CodeFirstUI.State      -> Asharia.Editor.UI.CodeFirst.State
Editor.Core.CodeFirstUI.Validation -> Asharia.Editor.UI.CodeFirst.Validation
Editor.Core.Models.Diagnostics     -> Asharia.Editor.Diagnostics
```

Do not change public member names or validation behavior. Update the still-legacy Authoring/Abstractions files and Shell adapters to import the new public kernel namespaces.

- [ ] **Step 4: Move the four kernel test files**

Apply the same namespace mapping and change test namespaces from `Editor.Tests.Core.CodeFirstUI.*` to `Asharia.Editor.Tests.UI.CodeFirst.*`. Do not copy tests; delete their old files in the same patch.

Before moving, these four files contain exactly 28 cases: 20 builder, 2 event queue, 2 state store, and 4 validator. Record the exact pre/post filtered counts in the Issue update; Task 6 moves the remaining 30 authoring cases, preserving the 58-case total.

- [ ] **Step 5: Verify GREEN across public kernel and Shell adapters**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~CodeFirst
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~GuiAvaloniaControlFactoryTests|FullyQualifiedName~CodeFirstPanelHostViewModelTests"
dotnet build apps/studio/Editor.csproj -c Release --no-restore
```

Expected: all moved kernel tests and selected Shell adapter tests PASS; build has 0 errors.

- [ ] **Step 6: Commit**

```powershell
git add apps/studio/Core/CodeFirstUI apps/studio/src/Asharia.Editor/UI/CodeFirst apps/studio/Tests apps/studio/Shell/CodeFirstUI
git commit -m "refactor(studio): extract code first kernel"
```

---

### Task 6: Move authoring and add the explicit panel host SPI

**Files:**

- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Abstractions/CodeFirstEditorPanel.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Abstractions/ICodeFirstEditorPanelHost.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Abstractions/IEditorGuiCommandExecutor.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Authoring/EditorGui.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Authoring/GuiFoldoutScope.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Authoring/GuiNavigationPage.cs`
- Create: `apps/studio/src/Asharia.Editor/UI/CodeFirst/Authoring/GuiNavigationScope.cs`
- Delete: all legacy `apps/studio/Core/CodeFirstUI/Abstractions/*.cs`
- Delete: all legacy `apps/studio/Core/CodeFirstUI/Authoring/*.cs`
- Move: `apps/studio/Tests/Editor.Tests/Core/CodeFirstUI/Authoring/EditorGuiTests.cs` to `apps/studio/Tests/Asharia.Editor.Tests/UI/CodeFirst/Authoring/EditorGuiTests.cs`
- Modify: `apps/studio/Tests/Asharia.Editor.Tests/UI/CodeFirst/CodeFirstAssemblyOwnershipTests.cs`
- Modify: `apps/studio/Shell/CodeFirstUI/Hosting/CodeFirstPanelHostViewModel.cs`
- Modify: `apps/studio/Features/FrameDebugger/FrameDebuggerPanel.cs`
- Modify: `apps/studio/Features/UiStyle/UiStylePanel.cs`
- Modify: all production/test files returned by `rg -l "Editor\.Core\.CodeFirstUI\.(Abstractions|Authoring)" apps/studio -g '*.cs'`

**Interfaces:**

- Consumes: public Commands from Task 3, Panels from Task 4, and Code-first kernel from Task 5.
- Produces: complete `Asharia.Editor.UI.CodeFirst` authoring API and explicit `ICodeFirstEditorPanelHost` SPI.

- [ ] **Step 1: Extend the ownership test and verify compile RED**

Add these inline data rows to `Kernel_type_is_owned_by_public_editor_api`:

```csharp
[InlineData(typeof(EditorGui))]
[InlineData(typeof(CodeFirstEditorPanel))]
[InlineData(typeof(IEditorGuiCommandExecutor))]
[InlineData(typeof(ICodeFirstEditorPanelHost))]
```

Add the `Abstractions` and `Authoring` usings. Add this behavior test:

```csharp
[Fact]
public void Panel_host_spi_dispatches_to_protected_authoring_hooks()
{
    var panel = new RecordingPanel();
    var host = Assert.IsAssignableFrom<ICodeFirstEditorPanelHost>(panel);
    var lifecycle = new EditorPanelLifecycleContext(
        "test.panel",
        "Test",
        EditorDockArea.Center,
        IsFloatingWorkspace: false);

    host.Create(lifecycle);
    host.Enable();
    host.Disable();
    host.Destroy();

    Assert.Equal(["create", "enable", "disable", "destroy"], panel.Events);
}
```

The nested `RecordingPanel` overrides `OnCreate`, `OnEnable`, `OnGui`, `OnDisable`, and `OnDestroy`; `OnGui` may have an empty body for this test.

Use this exact nested type:

```csharp
private sealed class RecordingPanel : CodeFirstEditorPanel
{
    public List<string> Events { get; } = [];

    protected override void OnCreate(EditorPanelLifecycleContext context)
    {
        Events.Add("create");
    }

    protected override void OnEnable()
    {
        Events.Add("enable");
    }

    protected override void OnGui(EditorGui gui)
    {
    }

    protected override void OnDisable()
    {
        Events.Add("disable");
    }

    protected override void OnDestroy()
    {
        Events.Add("destroy");
    }
}
```

Add `using System.Collections.Generic;`, `using Asharia.Editor.Panels;`, and the public Code-first Abstractions/Authoring namespaces.

- [ ] **Step 2: Verify compile RED**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~Kernel_type_is_owned_by_public_editor_api|FullyQualifiedName~Panel_host_spi_dispatches"
```

Expected: compile FAIL because public Authoring/Abstractions/SPI types do not exist.

- [ ] **Step 3: Create the exact public host SPI**

Create `ICodeFirstEditorPanelHost.cs`:

```csharp
using Asharia.Editor.Panels;
using Asharia.Editor.UI.CodeFirst.Authoring;

namespace Asharia.Editor.UI.CodeFirst.Abstractions;

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

Move `CodeFirstEditorPanel` and make it implement the interface explicitly:

```csharp
public abstract class CodeFirstEditorPanel : ICodeFirstEditorPanelHost
{
    public virtual EditorPanelFrameUpdateRequest FrameUpdateRequest =>
        EditorPanelFrameUpdateRequest.Manual;

    EditorPanelFrameUpdateRequest ICodeFirstEditorPanelHost.FrameUpdateRequest =>
        FrameUpdateRequest;

    void ICodeFirstEditorPanelHost.Create(EditorPanelLifecycleContext context) => OnCreate(context);
    void ICodeFirstEditorPanelHost.Enable() => OnEnable();
    void ICodeFirstEditorPanelHost.BuildGui(EditorGui gui) => OnGui(gui);
    void ICodeFirstEditorPanelHost.Frame(EditorPanelFrameContext context) => OnFrame(context);
    void ICodeFirstEditorPanelHost.Disable() => OnDisable();
    void ICodeFirstEditorPanelHost.Destroy() => OnDestroy();

    // Preserve the existing protected virtual hooks unchanged.
}
```

Remove all `internal Dispatch*` methods. Do not add `InternalsVisibleTo`.

- [ ] **Step 4: Move Authoring and command executor**

Apply:

```text
Editor.Core.CodeFirstUI.Abstractions -> Asharia.Editor.UI.CodeFirst.Abstractions
Editor.Core.CodeFirstUI.Authoring    -> Asharia.Editor.UI.CodeFirst.Authoring
Editor.Core.Models.Workbench         -> Asharia.Editor.Commands
Editor.Core.Models.Panels            -> Asharia.Editor.Panels
```

`IEditorGuiCommandExecutor.Execute(string commandId)` returns `EditorCommandExecutionResult`. `EditorGui.CommandButton` and `ExecuteCommand` return the same public result type.

- [ ] **Step 5: Update the Shell host to use only the SPI**

Keep the constructor parameter `CodeFirstEditorPanel panel`, remove the old concrete `panel_` field, then store only:

```csharp
private readonly ICodeFirstEditorPanelHost panelHost_;
```

Initialize with:

```csharp
panelHost_ = (ICodeFirstEditorPanelHost)panel;
```

Replace every `panel_.Dispatch*` call with the corresponding `panelHost_` method and read `FrameUpdateRequest` through `panelHost_`. Lifecycle ordering remains create → enable → build/frame → disable → destroy.

- [ ] **Step 6: Move the authoring test and update all callers**

Move `EditorGuiTests.cs`, change its namespace to `Asharia.Editor.Tests.UI.CodeFirst.Authoring`, and update usings to public Commands/CodeFirst namespaces. Update FrameDebugger, UiStyle, Shell host, ViewLocator tests, feature tests, and Shell Code-first tests mechanically. Delete the old Authoring/Abstractions files.

- [ ] **Step 7: Verify GREEN and preserve all 58 Code-first cases**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~CodeFirst
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~CodeFirstPanelHostViewModelTests|FullyQualifiedName~GuiAvaloniaControlFactoryTests|FullyQualifiedName~FrameDebuggerPanelTests|FullyQualifiedName~UiStylePanelTests"
rg -n "Dispatch(Create|Enable|Gui|Frame|Disable|Destroy)|Editor\.Core\.CodeFirstUI" apps/studio -g '*.cs'
rg -n "InternalsVisibleTo" apps/studio/src/Asharia.Editor -g '*.cs'
git diff 835649f5..HEAD -- apps/studio | Select-String -Pattern "InternalsVisibleTo"
```

Expected: the relocated original Code-first suite still accounts for 58 passing cases (28 kernel + 30 authoring); selected Shell/feature tests PASS; both `rg` commands and the Task 6 diff scan return no old dispatch, new public friend-assembly, new friend-assembly diff, or old namespace references. The pre-existing test-only friend in `apps/studio/Properties/AssemblyInfo.cs` is outside this extraction and remains unchanged.

- [ ] **Step 8: Commit**

```powershell
git add apps/studio/Core/CodeFirstUI apps/studio/src/Asharia.Editor/UI/CodeFirst apps/studio/Tests apps/studio/Features apps/studio/Shell/CodeFirstUI
git commit -m "refactor(studio): extract code first authoring api"
```

---

### Task 7: Freeze architecture gates, update current facts, and run full validation

**Files:**

- Modify: `apps/studio/Tests/Asharia.Studio.Architecture.Tests/ProjectReferenceGraphTests.cs`
- Modify: `apps/studio/docs/architecture/studio-code-framework.md`
- Modify: `apps/studio/docs/superpowers/plans/2026-07-11-studio-editor-framework-refactor.md`
- Modify: `apps/studio/docs/superpowers/specs/2026-07-11-studio-code-first-public-api-extraction-design.md`
- Modify: `apps/studio/docs/superpowers/plans/2026-07-11-studio-code-first-public-api-extraction.md`

**Interfaces:**

- Consumes: all public contracts and moved source from Tasks 1–6.
- Produces: enforceable assembly/source ownership and current documentation; no runtime behavior.

- [x] **Step 1: Add failing final architecture assertions before deleting any missed legacy source**

Add:

```csharp
[Fact]
public void Code_first_source_is_owned_only_by_public_editor()
{
    var studioRoot = FindStudioRoot();
    var legacyRoot = Path.Combine(studioRoot, "Core", "CodeFirstUI");
    var publicRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "UI", "CodeFirst");

    Assert.False(Directory.Exists(legacyRoot), $"Legacy Code-first source remains at {legacyRoot}.");
    Assert.True(Directory.Exists(publicRoot), $"Public Code-first source is missing at {publicRoot}.");

    var publicSource = string.Join(
        Environment.NewLine,
        Directory.EnumerateFiles(publicRoot, "*.cs", SearchOption.AllDirectories)
            .Order(StringComparer.Ordinal)
            .Select(File.ReadAllText));

    Assert.DoesNotContain("Editor.Core", publicSource, StringComparison.Ordinal);
    Assert.DoesNotContain("Avalonia", publicSource, StringComparison.Ordinal);
}
```

Run it before final cleanup. Expected: FAIL if any empty legacy directory remains; remove only the empty legacy directories or missed files created by this migration, then rerun to PASS.

- [x] **Step 2: Update documentation to current truth**

In `studio-code-framework.md`, state:

- Code-first authoring/tree/state/events/validation now compile into `Asharia.Editor`;
- legacy `Editor` references public API and owns Avalonia/Dock/host implementation;
- Diagnostics/Commands/Panels prerequisite contracts are public;
- contribution descriptors and Host/resolver remain unimplemented.

In the master refactor plan, mark Task 3 complete, replace the old direct-move assumption with the actual prerequisite closure, and preserve uppercase `Tests/` paths. Change this spec status from `Proposed` to `Implemented` only after all gates pass.

- [x] **Step 3: Run public build and test gates**

```powershell
dotnet build apps/studio/src/Asharia.Editor/Asharia.Editor.csproj -c Release --no-restore -warnaserror
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore
dotnet test apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --no-restore
```

Expected: 0 warnings/errors; all public API and architecture tests PASS.

- [x] **Step 4: Run legacy and target Solution gates**

```powershell
dotnet test apps/studio/Editor.sln -c Release --no-restore
dotnet test apps/studio/Asharia.Studio.sln -c Release --no-restore
```

Expected: zero failed tests. The original 58 Code-first cases are now owned by `Asharia.Editor.Tests`; no behavior case is silently dropped.

- [x] **Step 5: Run formatting, encoding, docs, and diff gates**

```powershell
dotnet format apps/studio/src/Asharia.Editor/Asharia.Editor.csproj --verify-no-changes --no-restore
dotnet format apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj --verify-no-changes --no-restore
$editorChanged = @(git diff --diff-filter=ACMR --name-only 2f660fee..HEAD -- apps/studio | Where-Object { $_ -like '*.cs' -and $_ -notlike 'apps/studio/src/*' -and $_ -notlike 'apps/studio/Tests/*' } | ForEach-Object { (Resolve-Path $_).Path })
$editorTestsChanged = @(git diff --diff-filter=ACMR --name-only 2f660fee..HEAD -- apps/studio/Tests/Editor.Tests | Where-Object { $_ -like '*.cs' } | ForEach-Object { (Resolve-Path $_).Path })
dotnet format apps/studio/Editor.csproj --verify-no-changes --no-restore --include $editorChanged
dotnet format apps/studio/Tests/Editor.Tests/Editor.Tests.csproj --verify-no-changes --no-restore --include $editorTestsChanged
powershell -ExecutionPolicy Bypass -File tools/check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools/check-doc-sync.ps1 -NoDocsReason "Studio-local architecture, spec, and plan docs under apps/studio/docs were updated; the repository-level checker only classifies root docs paths."
git diff --check
git diff --cached --check
```

The two legacy format commands are intentionally scoped because untouched legacy files have pre-existing format/charset failures. Also verify every changed managed/Markdown file with strict `UTF8Encoding(false, true)` and reject a leading `EF BB BF` BOM.

- [x] **Step 6: Prepare Issue evidence and commit**

Record on #233:

- every RED failure reason and corresponding GREEN command;
- final test counts by project;
- 58 original Code-first cases preserved under the public test assembly;
- formatting/encoding/doc/diff results;
- any intentionally deferred contribution/Host work.

Then commit:

```powershell
git add apps/studio/Tests/Asharia.Studio.Architecture.Tests apps/studio/docs
git commit -m "docs(studio): record code first api extraction"
```

- [x] **Step 7: Prepare the Draft PR handoff**

Prepare the PR summary and validation evidence for the controller, but do not push or open the PR before the required final whole-branch review. After that review is clean, the controller pushes the implementation branch and opens a Draft PR with `Closes #233`. The PR body must list the prerequisite contract promotion, full Code-first ownership move, host SPI, TDD evidence, test counts, and the explicit non-goals. Keep it Draft until review confirms no public API accidentally exposes legacy Workbench, Avalonia, Dock implementation, or native vocabulary.
