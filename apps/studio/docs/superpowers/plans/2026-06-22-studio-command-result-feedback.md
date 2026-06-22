# Studio Command Result Feedback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish the latest Studio command execution result to Shell status chrome through a UI-neutral feedback snapshot.

**Architecture:** Keep `WorkbenchCommandExecutionResult` as the command execution fact. Add a Core feedback snapshot mapper, then wrap the existing Shell command router so menu, shortcut, and command palette execution all publish to `MainWindowViewModel` without changing command semantics. Render a compact status binding in `MainWindow.axaml`.

**Tech Stack:** C#/.NET 10, Avalonia 12.0.4, CommunityToolkit.Mvvm, xUnit, existing Studio Core/Shell/ViewModel patterns.

---

## Source Spec

- `docs/superpowers/specs/2026-06-22-studio-command-result-feedback-design.md`

## File Structure

- Create: `Core/Models/EditorCommandFeedbackSeverity.cs`
  - Defines UI-neutral command feedback severity values.
- Create: `Core/Models/EditorCommandFeedbackSnapshot.cs`
  - Converts `WorkbenchCommandExecutionResult` into deterministic message/severity snapshots.
- Create: `Shell/Commands/WorkbenchCommandFeedbackRouter.cs`
  - Decorates `IWorkbenchCommandRouter`, publishes every non-null result, and returns the original result.
- Modify: `Shell/ViewModels/MainWindowViewModel.cs`
  - Owns the latest command feedback snapshot and passes the decorated command router to palette, shortcuts, and generated menu items.
- Modify: `Shell/Views/MainWindow.axaml`
  - Shows the latest command feedback in the status bar beside `ActivityIndicator`.
- Create: `Tests/Editor.Tests/Core/EditorCommandFeedbackSnapshotTests.cs`
  - Covers status-to-severity and fallback message mapping.
- Modify: `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`
  - Covers menu, shortcut, and palette feedback publication.
- Modify: `docs/Dock系统指南.md`
  - Records the implemented command feedback surface and non-goals.

## Task 1: Feedback Snapshot Mapping

**Files:**
- Create: `Core/Models/EditorCommandFeedbackSeverity.cs`
- Create: `Core/Models/EditorCommandFeedbackSnapshot.cs`
- Create: `Tests/Editor.Tests/Core/EditorCommandFeedbackSnapshotTests.cs`

- [ ] **Step 1: Write failing mapping tests**

Create `Tests/Editor.Tests/Core/EditorCommandFeedbackSnapshotTests.cs`:

```csharp
using Editor.Core.Models;
using Xunit;

namespace Editor.Tests.Core;

public sealed class EditorCommandFeedbackSnapshotTests
{
    [Fact]
    public void FromResult_maps_success_to_success_feedback()
    {
        var snapshot = EditorCommandFeedbackSnapshot.FromResult(
            WorkbenchCommandExecutionResult.Success("workbench.panel.console"));

        Assert.Equal(EditorCommandFeedbackSeverity.Success, snapshot.Severity);
        Assert.Equal(WorkbenchCommandExecutionStatus.Succeeded, snapshot.Status);
        Assert.Equal("workbench.panel.console", snapshot.CommandId);
        Assert.Equal("Command 'workbench.panel.console' completed.", snapshot.Message);
    }

    [Fact]
    public void FromResult_maps_disabled_to_warning_feedback()
    {
        var snapshot = EditorCommandFeedbackSnapshot.FromResult(
            WorkbenchCommandExecutionResult.Disabled("workbench.panel.console", "Disabled by test"));

        Assert.Equal(EditorCommandFeedbackSeverity.Warning, snapshot.Severity);
        Assert.Equal(WorkbenchCommandExecutionStatus.Disabled, snapshot.Status);
        Assert.Equal("Disabled by test", snapshot.Message);
    }

    [Fact]
    public void FromResult_maps_not_found_to_error_feedback()
    {
        var snapshot = EditorCommandFeedbackSnapshot.FromResult(
            WorkbenchCommandExecutionResult.NotFound("missing.command"));

        Assert.Equal(EditorCommandFeedbackSeverity.Error, snapshot.Severity);
        Assert.Equal(WorkbenchCommandExecutionStatus.NotFound, snapshot.Status);
        Assert.Contains("is not registered", snapshot.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void FromResult_maps_failed_to_error_feedback()
    {
        var snapshot = EditorCommandFeedbackSnapshot.FromResult(
            WorkbenchCommandExecutionResult.Failed("workbench.panel.console", "Failed by test"));

        Assert.Equal(EditorCommandFeedbackSeverity.Error, snapshot.Severity);
        Assert.Equal(WorkbenchCommandExecutionStatus.Failed, snapshot.Status);
        Assert.Equal("Failed by test", snapshot.Message);
    }

    [Fact]
    public void FromResult_uses_fallback_message_for_blank_failure()
    {
        var snapshot = EditorCommandFeedbackSnapshot.FromResult(
            WorkbenchCommandExecutionResult.Failed("workbench.panel.console", "   "));

        Assert.Equal("Command 'workbench.panel.console' did not complete.", snapshot.Message);
    }
}
```

- [ ] **Step 2: Run focused tests and verify RED**

Run from `D:\TechArt\VkEngine-studio-frontend\apps\studio`:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorCommandFeedbackSnapshotTests"
```

Expected: FAIL because `EditorCommandFeedbackSnapshot` and `EditorCommandFeedbackSeverity` do not exist.

- [ ] **Step 3: Add severity enum**

Create `Core/Models/EditorCommandFeedbackSeverity.cs`:

```csharp
namespace Editor.Core.Models;

public enum EditorCommandFeedbackSeverity
{
    Info,
    Success,
    Warning,
    Error,
}
```

- [ ] **Step 4: Add snapshot mapper**

Create `Core/Models/EditorCommandFeedbackSnapshot.cs`:

```csharp
using System;

namespace Editor.Core.Models;

public sealed record EditorCommandFeedbackSnapshot(
    EditorCommandFeedbackSeverity Severity,
    WorkbenchCommandExecutionStatus Status,
    string CommandId,
    string Message)
{
    public static EditorCommandFeedbackSnapshot FromResult(WorkbenchCommandExecutionResult result)
    {
        ArgumentNullException.ThrowIfNull(result);

        return new EditorCommandFeedbackSnapshot(
            MapSeverity(result.Status),
            result.Status,
            result.CommandId,
            CreateMessage(result));
    }

    private static EditorCommandFeedbackSeverity MapSeverity(WorkbenchCommandExecutionStatus status)
    {
        return status switch
        {
            WorkbenchCommandExecutionStatus.Succeeded => EditorCommandFeedbackSeverity.Success,
            WorkbenchCommandExecutionStatus.Disabled => EditorCommandFeedbackSeverity.Warning,
            WorkbenchCommandExecutionStatus.NotFound => EditorCommandFeedbackSeverity.Error,
            WorkbenchCommandExecutionStatus.Failed => EditorCommandFeedbackSeverity.Error,
            _ => EditorCommandFeedbackSeverity.Info,
        };
    }

    private static string CreateMessage(WorkbenchCommandExecutionResult result)
    {
        if (!string.IsNullOrWhiteSpace(result.Message))
        {
            return result.Message;
        }

        return result.Status switch
        {
            WorkbenchCommandExecutionStatus.Succeeded => $"Command '{result.CommandId}' completed.",
            WorkbenchCommandExecutionStatus.Disabled => $"Command '{result.CommandId}' is disabled.",
            WorkbenchCommandExecutionStatus.NotFound => $"Command '{result.CommandId}' is not registered.",
            WorkbenchCommandExecutionStatus.Failed => $"Command '{result.CommandId}' did not complete.",
            _ => $"Command '{result.CommandId}' finished with status {result.Status}.",
        };
    }
}
```

- [ ] **Step 5: Run focused tests and commit**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorCommandFeedbackSnapshotTests"
```

Expected: PASS.

Commit:

```powershell
git add Core\Models\EditorCommandFeedbackSeverity.cs Core\Models\EditorCommandFeedbackSnapshot.cs Tests\Editor.Tests\Core\EditorCommandFeedbackSnapshotTests.cs
git commit -m "feat: add studio command feedback snapshots"
```

## Task 2: Shell Command Feedback Publication

**Files:**
- Create: `Shell/Commands/WorkbenchCommandFeedbackRouter.cs`
- Modify: `Shell/ViewModels/MainWindowViewModel.cs`
- Modify: `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`

- [ ] **Step 1: Write failing MainWindow feedback tests**

Add these tests to `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`:

```csharp
[Fact]
public void Tools_menu_command_updates_latest_command_feedback()
{
    var viewModel = CreateMainWindowViewModel();
    var item = Assert.Single(viewModel.ToolsMenuItems);

    item.OpenCommand.Execute(null);

    Assert.True(viewModel.HasCommandFeedback);
    Assert.True(viewModel.IsCommandFeedbackSuccess);
    Assert.Equal(EditorCommandFeedbackSeverity.Success, viewModel.LastCommandFeedback?.Severity);
    Assert.Equal("workbench.commandPalette.open", viewModel.LastCommandFeedback?.CommandId);
    Assert.Equal("Command 'workbench.commandPalette.open' completed.", viewModel.CommandFeedbackMessage);
}

[Fact]
public void Shortcut_command_updates_latest_command_feedback()
{
    var viewModel = CreateMainWindowViewModel();

    var result = viewModel.ExecuteShortcut(
        Key.P,
        KeyModifiers.Control | KeyModifiers.Shift,
        isTextInputFocused: false);

    Assert.NotNull(result);
    Assert.True(viewModel.HasCommandFeedback);
    Assert.True(viewModel.IsCommandFeedbackSuccess);
    Assert.Equal("workbench.commandPalette.open", viewModel.LastCommandFeedback?.CommandId);
}

[Fact]
public void Command_palette_failure_updates_local_and_global_feedback()
{
    var actions = new WorkbenchActionRegistry();
    actions.Register(new WorkbenchActionDescriptor(
        "workbench.panel.missing",
        "Missing Panel",
        WorkbenchActionKind.OpenPanel,
        "Window/Panels/Missing",
        TargetId: "missing-panel",
        Category: "Window"));
    var viewModel = new MainWindowViewModel(
        MainWindowViewModel.CreatePanelRegistry(),
        actions,
        savedLayout: null);

    viewModel.CommandPalette.OpenCommand.Execute(null);
    viewModel.CommandPalette.Query = "missing";
    viewModel.CommandPalette.ExecuteSelectedCommand.Execute(null);

    Assert.True(viewModel.CommandPalette.HasLastResultMessage);
    Assert.True(viewModel.HasCommandFeedback);
    Assert.True(viewModel.IsCommandFeedbackError);
    Assert.Equal(WorkbenchCommandExecutionStatus.Failed, viewModel.LastCommandFeedback?.Status);
    Assert.Equal(viewModel.CommandPalette.LastResultMessage, viewModel.CommandFeedbackMessage);
}

[Fact]
public void Command_feedback_raises_visibility_message_and_severity_notifications()
{
    var changedProperties = new List<string>();
    var viewModel = CreateMainWindowViewModel();
    viewModel.PropertyChanged += (_, args) => changedProperties.Add(args.PropertyName ?? string.Empty);

    viewModel.ToolsMenuItems.Single().OpenCommand.Execute(null);

    Assert.Contains(nameof(MainWindowViewModel.LastCommandFeedback), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.HasCommandFeedback), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.CommandFeedbackMessage), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.IsCommandFeedbackSuccess), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.IsCommandFeedbackWarning), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.IsCommandFeedbackError), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.IsCommandFeedbackInfo), changedProperties);
}
```

- [ ] **Step 2: Run focused tests and verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowViewModelTests"
```

Expected: FAIL because `MainWindowViewModel` does not expose command feedback properties and command routes do not publish feedback.

- [ ] **Step 3: Add feedback router decorator**

Create `Shell/Commands/WorkbenchCommandFeedbackRouter.cs`:

```csharp
using System;
using Editor.Core.Models;

namespace Editor.Shell.Commands;

internal sealed class WorkbenchCommandFeedbackRouter : IWorkbenchCommandRouter
{
    private readonly IWorkbenchCommandRouter inner_;
    private readonly Action<WorkbenchCommandExecutionResult> publishResult_;

    public WorkbenchCommandFeedbackRouter(
        IWorkbenchCommandRouter inner,
        Action<WorkbenchCommandExecutionResult> publishResult)
    {
        ArgumentNullException.ThrowIfNull(inner);
        ArgumentNullException.ThrowIfNull(publishResult);

        inner_ = inner;
        publishResult_ = publishResult;
    }

    public WorkbenchCommandExecutionResult Execute(string commandId)
    {
        var result = inner_.Execute(commandId);
        publishResult_(result);
        return result;
    }
}
```

- [ ] **Step 4: Add MainWindow feedback properties and publish method**

In `Shell/ViewModels/MainWindowViewModel.cs`, add a field:

```csharp
private EditorCommandFeedbackSnapshot? lastCommandFeedback_;
```

Add properties:

```csharp
public EditorCommandFeedbackSnapshot? LastCommandFeedback
{
    get => lastCommandFeedback_;
    private set
    {
        if (SetProperty(ref lastCommandFeedback_, value))
        {
            OnPropertyChanged(nameof(HasCommandFeedback));
            OnPropertyChanged(nameof(CommandFeedbackMessage));
            OnPropertyChanged(nameof(IsCommandFeedbackSuccess));
            OnPropertyChanged(nameof(IsCommandFeedbackWarning));
            OnPropertyChanged(nameof(IsCommandFeedbackError));
            OnPropertyChanged(nameof(IsCommandFeedbackInfo));
        }
    }
}

public bool HasCommandFeedback => LastCommandFeedback is not null;

public string CommandFeedbackMessage => LastCommandFeedback?.Message ?? string.Empty;

public bool IsCommandFeedbackSuccess => LastCommandFeedback?.Severity == EditorCommandFeedbackSeverity.Success;

public bool IsCommandFeedbackWarning => LastCommandFeedback?.Severity == EditorCommandFeedbackSeverity.Warning;

public bool IsCommandFeedbackError => LastCommandFeedback?.Severity == EditorCommandFeedbackSeverity.Error;

public bool IsCommandFeedbackInfo => LastCommandFeedback?.Severity == EditorCommandFeedbackSeverity.Info;
```

Add the publisher:

```csharp
private void PublishCommandFeedback(WorkbenchCommandExecutionResult result)
{
    LastCommandFeedback = EditorCommandFeedbackSnapshot.FromResult(result);
}
```

- [ ] **Step 5: Wire all command routes through the feedback router**

In the constructor, replace:

```csharp
var commandRouter = new WorkbenchCommandRouter(actionRegistry, actionExecutor);
```

with:

```csharp
var commandRouter = new WorkbenchCommandFeedbackRouter(
    new WorkbenchCommandRouter(actionRegistry, actionExecutor),
    PublishCommandFeedback);
```

Leave the existing downstream uses of `commandRouter` in place:

```csharp
CommandPalette = new CommandPaletteViewModel(actions, commandRouter.Execute);
shortcutRouter_ = WorkbenchShortcutRouter.FromActions(actions, commandRouter);
ToolsMenuItems = CreateCommandMenuItems(actions, "Tools/", commandRouter);
HelpMenuItems = CreateCommandMenuItems(actions, "Help/", commandRouter);
PanelMenuItems = CreatePanelMenuItems(actions, commandRouter);
```

- [ ] **Step 6: Run focused tests and commit**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~EditorCommandFeedbackSnapshotTests"
```

Expected: PASS.

Commit:

```powershell
git add Shell\Commands\WorkbenchCommandFeedbackRouter.cs Shell\ViewModels\MainWindowViewModel.cs Tests\Editor.Tests\Shell\ViewModels\MainWindowViewModelTests.cs
git commit -m "feat: publish studio command feedback"
```

## Task 3: Status Binding And Documentation

**Files:**
- Modify: `Shell/Views/MainWindow.axaml`
- Modify: `docs/Dock系统指南.md`

- [ ] **Step 1: Add status XAML binding**

In `Shell/Views/MainWindow.axaml`, replace the single `ActivityIndicator` child inside the bottom status `Border` with:

```xml
<DockPanel LastChildFill="False">
    <feedback:ActivityIndicator DockPanel.Dock="Left"
                                HorizontalAlignment="Left"
                                IsActive="{Binding HasActiveBackgroundTasks}"
                                Message="{Binding ActiveBackgroundTaskMessage}"
                                Title="{Binding ActiveBackgroundTaskTitle}"
                                VerticalAlignment="Center" />

    <TextBlock DockPanel.Dock="Right"
               Classes.success="{Binding IsCommandFeedbackSuccess}"
               Classes.warning="{Binding IsCommandFeedbackWarning}"
               Classes.error="{Binding IsCommandFeedbackError}"
               Classes.info="{Binding IsCommandFeedbackInfo}"
               Classes="command-feedback-status"
               IsVisible="{Binding HasCommandFeedback}"
               Text="{Binding CommandFeedbackMessage}"
               VerticalAlignment="Center" />
</DockPanel>
```

Add local styles near the top of `MainWindow.axaml`:

```xml
<Window.Styles>
    <Style Selector="TextBlock.command-feedback-status">
        <Setter Property="FontSize" Value="{DynamicResource EditorFontSizeSmall}" />
        <Setter Property="Foreground" Value="{DynamicResource EditorBrushTextMuted}" />
        <Setter Property="TextTrimming" Value="CharacterEllipsis" />
        <Setter Property="MaxWidth" Value="520" />
    </Style>
    <Style Selector="TextBlock.command-feedback-status.success">
        <Setter Property="Foreground" Value="{DynamicResource EditorBrushSuccess}" />
    </Style>
    <Style Selector="TextBlock.command-feedback-status.warning">
        <Setter Property="Foreground" Value="{DynamicResource EditorBrushWarning}" />
    </Style>
    <Style Selector="TextBlock.command-feedback-status.error">
        <Setter Property="Foreground" Value="{DynamicResource EditorBrushError}" />
    </Style>
    <Style Selector="TextBlock.command-feedback-status.info">
        <Setter Property="Foreground" Value="{DynamicResource EditorBrushInfo}" />
    </Style>
</Window.Styles>
```

- [ ] **Step 2: Update Dock guide facts**

Update `docs/Dock系统指南.md` current implementation facts with an ASCII-safe fact:

```text
42. Command result feedback v0 consumes `WorkbenchCommandExecutionResult`, maps it to UI-neutral `EditorCommandFeedbackSnapshot`, and publishes the latest menu/shortcut/palette command result through Shell status chrome; it intentionally does not add toast history, Problems/Console, native logs, plugin APIs, or modal failure dialogs.
```

- [ ] **Step 3: Run focused build tests and commit**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~CommandPaletteViewModelTests|FullyQualifiedName~WorkbenchMenuItemViewModelTests"
```

Expected: PASS, including XAML compile.

Commit:

```powershell
git add Shell\Views\MainWindow.axaml docs\Dock系统指南.md
git commit -m "feat: show command feedback in studio status"
```

## Final Validation

Run after all tasks:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorCommandFeedbackSnapshotTests|FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~CommandPaletteViewModelTests|FullyQualifiedName~WorkbenchCommandRouterTests|FullyQualifiedName~WorkbenchMenuItemViewModelTests"
dotnet test Editor.sln -c Release
dotnet test Editor.sln
powershell -ExecutionPolicy Bypass -File ..\..\tools\check-text-encoding.ps1
git diff --check
```

Expected:

- focused command feedback tests pass
- Release solution tests pass
- Debug solution tests pass
- encoding check reports 0 missing BOM, 0 unexpected BOM, and 0 invalid UTF-8
- `git diff --check` prints no errors

No CMake/Vulkan pre-commit gate is required for this plan because all changes stay under `apps/studio` editor framework files and do not touch C/C++, shaders, CMake, Conan, renderer, native ABI, runtime scene providers, or engine packages.

## Self-Review

- Spec coverage: Task 1 covers deterministic feedback mapping; Task 2 covers menu, shortcut, and palette publication; Task 3 covers status UI and docs.
- Boundary check: the plan keeps command execution facts in `WorkbenchCommandExecutionResult` and feedback snapshots UI-neutral. It does not add runtime/native/plugin/diagnostics/toast systems.
- Type consistency: planned properties are `LastCommandFeedback`, `HasCommandFeedback`, `CommandFeedbackMessage`, and severity booleans; tests and XAML use the same names.
- Verification scope: focused ViewModel/Core tests cover behavior, solution tests cover XAML and regressions, encoding/diff checks cover repository hygiene.
