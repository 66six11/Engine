# Studio Status Debug Message Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename and generalize Studio's command-specific status feedback into a UI-neutral latest status/debug message surface that can later be fed by Console/debug messages.

**Architecture:** Replace `EditorCommandFeedbackSnapshot` with `EditorStatusMessageSnapshot`, keeping command execution results as the first producer. Shell owns the latest message and optional panel target routing; XAML only binds message text, visibility, severity classes, and the open-target command. Console remains a debug display concept and Problems remains a separate diagnostics concept.

**Tech Stack:** C#/.NET 10, Avalonia 12.0.4, CommunityToolkit.Mvvm, xUnit, existing Studio Core/Shell/ViewModel patterns.

---

## Source Spec

- `docs/superpowers/specs/2026-06-23-studio-status-debug-message-design.md`

## File Structure

- Rename: `Core/Models/EditorCommandFeedbackSeverity.cs` -> `Core/Models/EditorStatusMessageSeverity.cs`
  - Defines UI-neutral latest status/debug message severity values.
- Create: `Core/Models/EditorStatusMessageSource.cs`
  - Defines UI-level sources: command, console, diagnostics, background task.
- Rename: `Core/Models/EditorCommandFeedbackSnapshot.cs` -> `Core/Models/EditorStatusMessageSnapshot.cs`
  - Converts `WorkbenchCommandExecutionResult` into status messages without exposing command-only status fields.
- Rename: `Shell/Commands/WorkbenchCommandFeedbackRouter.cs` -> `Shell/Commands/WorkbenchCommandStatusMessageRouter.cs`
  - Decorates `IWorkbenchCommandRouter`, maps command results to status messages, publishes them, and returns the original result unchanged.
- Modify: `Shell/ViewModels/MainWindowViewModel.cs`
  - Owns `LastStatusMessage`, status message binding properties, and `OpenStatusMessageTargetCommand`.
- Modify: `Shell/Views/MainWindow.axaml`
  - Replaces command-feedback status text with generic status-message binding and optional command target click behavior.
- Rename: `Tests/Editor.Tests/Core/EditorCommandFeedbackSnapshotTests.cs` -> `Tests/Editor.Tests/Core/EditorStatusMessageSnapshotTests.cs`
  - Covers command result mapping into the generic status message contract.
- Modify: `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`
  - Updates command producer assertions to the generic status message surface and adds target-routing coverage.
- Modify: `Tests/Editor.Tests/Shell/Views/MainWindowXamlTests.cs`
  - Ensures XAML no longer binds to command-feedback names.
- Modify: `docs/Dock系统指南.md`
  - Records status/debug message v0 and explicit Console/Problems/shell boundaries.
- Modify: `docs/编辑器UI平台规范.md`
  - Updates sequence and status feedback contract wording.

## Task 1: Core Status Message Contract

**Files:**
- Rename: `Core/Models/EditorCommandFeedbackSeverity.cs` -> `Core/Models/EditorStatusMessageSeverity.cs`
- Create: `Core/Models/EditorStatusMessageSource.cs`
- Rename: `Core/Models/EditorCommandFeedbackSnapshot.cs` -> `Core/Models/EditorStatusMessageSnapshot.cs`
- Rename: `Tests/Editor.Tests/Core/EditorCommandFeedbackSnapshotTests.cs` -> `Tests/Editor.Tests/Core/EditorStatusMessageSnapshotTests.cs`

- [ ] **Step 1: Rename the test file and write failing status-message tests**

Run:

```powershell
git mv Tests\Editor.Tests\Core\EditorCommandFeedbackSnapshotTests.cs Tests\Editor.Tests\Core\EditorStatusMessageSnapshotTests.cs
```

Replace `Tests/Editor.Tests/Core/EditorStatusMessageSnapshotTests.cs` with:

```csharp
using System;
using Editor.Core.Models;
using Xunit;

namespace Editor.Tests.CoreContracts;

public sealed class EditorStatusMessageSnapshotTests
{
    [Fact]
    public void FromCommandResult_maps_success_to_success_command_message()
    {
        var snapshot = EditorStatusMessageSnapshot.FromCommandResult(
            WorkbenchCommandExecutionResult.Success("workbench.panel.console"));

        Assert.Equal(EditorStatusMessageSeverity.Success, snapshot.Severity);
        Assert.Equal(EditorStatusMessageSource.Command, snapshot.Source);
        Assert.Equal("Command 'workbench.panel.console' completed.", snapshot.Message);
        Assert.Null(snapshot.TargetPanelId);
    }

    [Fact]
    public void FromCommandResult_maps_disabled_to_warning_command_message()
    {
        var snapshot = EditorStatusMessageSnapshot.FromCommandResult(
            WorkbenchCommandExecutionResult.Disabled("workbench.panel.console", "Disabled by test"));

        Assert.Equal(EditorStatusMessageSeverity.Warning, snapshot.Severity);
        Assert.Equal(EditorStatusMessageSource.Command, snapshot.Source);
        Assert.Equal("Disabled by test", snapshot.Message);
        Assert.Null(snapshot.TargetPanelId);
    }

    [Fact]
    public void FromCommandResult_maps_not_found_to_error_command_message()
    {
        var snapshot = EditorStatusMessageSnapshot.FromCommandResult(
            WorkbenchCommandExecutionResult.NotFound("missing.command"));

        Assert.Equal(EditorStatusMessageSeverity.Error, snapshot.Severity);
        Assert.Equal(EditorStatusMessageSource.Command, snapshot.Source);
        Assert.Contains("is not registered", snapshot.Message, StringComparison.Ordinal);
        Assert.Null(snapshot.TargetPanelId);
    }

    [Fact]
    public void FromCommandResult_maps_failed_to_error_command_message()
    {
        var snapshot = EditorStatusMessageSnapshot.FromCommandResult(
            WorkbenchCommandExecutionResult.Failed("workbench.panel.console", "Failed by test"));

        Assert.Equal(EditorStatusMessageSeverity.Error, snapshot.Severity);
        Assert.Equal(EditorStatusMessageSource.Command, snapshot.Source);
        Assert.Equal("Failed by test", snapshot.Message);
        Assert.Null(snapshot.TargetPanelId);
    }

    [Fact]
    public void FromCommandResult_uses_fallback_message_for_blank_failure()
    {
        var snapshot = EditorStatusMessageSnapshot.FromCommandResult(
            WorkbenchCommandExecutionResult.Failed("workbench.panel.console", "   "));

        Assert.Equal("Command 'workbench.panel.console' did not complete.", snapshot.Message);
        Assert.Null(snapshot.TargetPanelId);
    }

    [Fact]
    public void Constructor_preserves_console_source_and_target_panel()
    {
        var snapshot = new EditorStatusMessageSnapshot(
            EditorStatusMessageSeverity.Debug,
            EditorStatusMessageSource.Console,
            "Console debug line",
            TargetPanelId: "console");

        Assert.Equal(EditorStatusMessageSeverity.Debug, snapshot.Severity);
        Assert.Equal(EditorStatusMessageSource.Console, snapshot.Source);
        Assert.Equal("Console debug line", snapshot.Message);
        Assert.Equal("console", snapshot.TargetPanelId);
    }
}
```

- [ ] **Step 2: Run focused tests and verify RED**

Run from `D:\TechArt\VkEngine-studio-frontend\apps\studio`:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorStatusMessageSnapshotTests"
```

Expected: FAIL because `EditorStatusMessageSnapshot`, `EditorStatusMessageSeverity`, and `EditorStatusMessageSource` do not exist yet.

- [ ] **Step 3: Rename Core files and add the status message enums**

Run:

```powershell
git mv Core\Models\EditorCommandFeedbackSeverity.cs Core\Models\EditorStatusMessageSeverity.cs
git mv Core\Models\EditorCommandFeedbackSnapshot.cs Core\Models\EditorStatusMessageSnapshot.cs
```

Replace `Core/Models/EditorStatusMessageSeverity.cs` with:

```csharp
namespace Editor.Core.Models;

public enum EditorStatusMessageSeverity
{
    Debug,
    Info,
    Success,
    Warning,
    Error,
}
```

Create `Core/Models/EditorStatusMessageSource.cs`:

```csharp
namespace Editor.Core.Models;

public enum EditorStatusMessageSource
{
    Command,
    Console,
    Diagnostics,
    BackgroundTask,
}
```

- [ ] **Step 4: Replace the snapshot mapper**

Replace `Core/Models/EditorStatusMessageSnapshot.cs` with:

```csharp
using System;

namespace Editor.Core.Models;

public sealed record EditorStatusMessageSnapshot(
    EditorStatusMessageSeverity Severity,
    EditorStatusMessageSource Source,
    string Message,
    string? TargetPanelId = null)
{
    public static EditorStatusMessageSnapshot FromCommandResult(WorkbenchCommandExecutionResult result)
    {
        ArgumentNullException.ThrowIfNull(result);

        return new EditorStatusMessageSnapshot(
            MapSeverity(result.Status),
            EditorStatusMessageSource.Command,
            CreateMessage(result));
    }

    private static EditorStatusMessageSeverity MapSeverity(WorkbenchCommandExecutionStatus status)
    {
        return status switch
        {
            WorkbenchCommandExecutionStatus.Succeeded => EditorStatusMessageSeverity.Success,
            WorkbenchCommandExecutionStatus.Disabled => EditorStatusMessageSeverity.Warning,
            WorkbenchCommandExecutionStatus.NotFound => EditorStatusMessageSeverity.Error,
            WorkbenchCommandExecutionStatus.Failed => EditorStatusMessageSeverity.Error,
            _ => EditorStatusMessageSeverity.Info,
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
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorStatusMessageSnapshotTests"
```

Expected: PASS.

Commit:

```powershell
git add Core\Models\EditorStatusMessageSeverity.cs Core\Models\EditorStatusMessageSource.cs Core\Models\EditorStatusMessageSnapshot.cs Tests\Editor.Tests\Core\EditorStatusMessageSnapshotTests.cs
git add -u Core\Models Tests\Editor.Tests\Core
git commit -m "feat: add studio status message snapshots"
```

## Task 2: Shell Status Message Publication And Target Routing

**Files:**
- Rename: `Shell/Commands/WorkbenchCommandFeedbackRouter.cs` -> `Shell/Commands/WorkbenchCommandStatusMessageRouter.cs`
- Modify: `Shell/ViewModels/MainWindowViewModel.cs`
- Modify: `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`

- [ ] **Step 1: Update MainWindow tests to the generic status surface**

In `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`, replace the three command-feedback tests with these status-message tests:

```csharp
[Fact]
public void Tools_menu_command_updates_latest_status_message()
{
    var viewModel = CreateMainWindowViewModel();
    var item = Assert.Single(viewModel.ToolsMenuItems);

    item.OpenCommand.Execute(null);

    Assert.True(viewModel.HasStatusMessage);
    Assert.True(viewModel.IsStatusMessageSuccess);
    Assert.Equal(EditorStatusMessageSeverity.Success, viewModel.LastStatusMessage?.Severity);
    Assert.Equal(EditorStatusMessageSource.Command, viewModel.LastStatusMessage?.Source);
    Assert.Equal("Command 'workbench.commandPalette.open' completed.", viewModel.StatusMessageText);
    Assert.False(viewModel.CanOpenStatusMessageTarget);
    Assert.False(viewModel.OpenStatusMessageTargetCommand.CanExecute(null));
}

[Fact]
public void Command_palette_failure_updates_local_and_global_status_message()
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
    Assert.True(viewModel.HasStatusMessage);
    Assert.True(viewModel.IsStatusMessageError);
    Assert.Equal(EditorStatusMessageSource.Command, viewModel.LastStatusMessage?.Source);
    Assert.Equal(viewModel.CommandPalette.LastResultMessage, viewModel.StatusMessageText);
    Assert.False(viewModel.CanOpenStatusMessageTarget);
}

[Fact]
public void Shortcut_command_updates_latest_status_message()
{
    var viewModel = CreateMainWindowViewModel();

    var result = viewModel.ExecuteShortcut(
        Key.P,
        KeyModifiers.Control | KeyModifiers.Shift,
        isTextInputFocused: false);

    Assert.NotNull(result);
    Assert.True(viewModel.HasStatusMessage);
    Assert.True(viewModel.IsStatusMessageSuccess);
    Assert.Equal(EditorStatusMessageSource.Command, viewModel.LastStatusMessage?.Source);
    Assert.Equal("Command 'workbench.commandPalette.open' completed.", viewModel.StatusMessageText);
    Assert.Null(viewModel.LastStatusMessage?.TargetPanelId);
}
```

Then replace `Command_feedback_raises_visibility_message_and_severity_notifications` with:

```csharp
[Fact]
public void Status_message_raises_visibility_message_severity_and_target_notifications()
{
    var changedProperties = new List<string>();
    var viewModel = CreateMainWindowViewModel();
    viewModel.PropertyChanged += (_, args) => changedProperties.Add(args.PropertyName ?? string.Empty);

    viewModel.PublishStatusMessage(new EditorStatusMessageSnapshot(
        EditorStatusMessageSeverity.Debug,
        EditorStatusMessageSource.Console,
        "Console debug line",
        TargetPanelId: "console"));

    Assert.Contains(nameof(MainWindowViewModel.LastStatusMessage), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.HasStatusMessage), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.StatusMessageText), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.IsStatusMessageDebug), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.IsStatusMessageInfo), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.IsStatusMessageSuccess), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.IsStatusMessageWarning), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.IsStatusMessageError), changedProperties);
    Assert.Contains(nameof(MainWindowViewModel.CanOpenStatusMessageTarget), changedProperties);
}
```

Add these two tests after `OpenPanelCommand_opens_feature_panel_content`:

```csharp
[Fact]
public void Passive_command_status_message_does_not_open_target_panel()
{
    var viewModel = CreateMainWindowViewModel();
    var console = viewModel.DockWorkspace.BottomWindow.Tabs.Single(tab => tab.Id == "console");
    Assert.True(viewModel.DockWorkspace.CloseTab(console));

    viewModel.ToolsMenuItems.Single().OpenCommand.Execute(null);

    Assert.False(viewModel.CanOpenStatusMessageTarget);
    Assert.False(viewModel.OpenStatusMessageTargetCommand.CanExecute(null));
    viewModel.OpenStatusMessageTargetCommand.Execute(null);
    Assert.False(viewModel.DockWorkspace.ContainsPanel("console"));
}

[Fact]
public void Console_targeted_status_message_opens_console_panel()
{
    var viewModel = CreateMainWindowViewModel();
    var console = viewModel.DockWorkspace.BottomWindow.Tabs.Single(tab => tab.Id == "console");
    Assert.True(viewModel.DockWorkspace.CloseTab(console));
    viewModel.PublishStatusMessage(new EditorStatusMessageSnapshot(
        EditorStatusMessageSeverity.Debug,
        EditorStatusMessageSource.Console,
        "Console debug line",
        TargetPanelId: "console"));

    Assert.True(viewModel.CanOpenStatusMessageTarget);
    Assert.True(viewModel.OpenStatusMessageTargetCommand.CanExecute(null));

    viewModel.OpenStatusMessageTargetCommand.Execute(null);

    Assert.True(viewModel.DockWorkspace.ContainsPanel("console"));
}
```

- [ ] **Step 2: Run focused ViewModel tests and verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowViewModelTests"
```

Expected: FAIL because `MainWindowViewModel` still exposes command-feedback properties and no `PublishStatusMessage` or `OpenStatusMessageTargetCommand`.

- [ ] **Step 3: Rename and replace the command wrapper**

Run:

```powershell
git mv Shell\Commands\WorkbenchCommandFeedbackRouter.cs Shell\Commands\WorkbenchCommandStatusMessageRouter.cs
```

Replace `Shell/Commands/WorkbenchCommandStatusMessageRouter.cs` with:

```csharp
using System;
using Editor.Core.Models;

namespace Editor.Shell.Commands;

internal sealed class WorkbenchCommandStatusMessageRouter : IWorkbenchCommandRouter
{
    private readonly IWorkbenchCommandRouter inner_;
    private readonly Action<EditorStatusMessageSnapshot> publishStatusMessage_;

    public WorkbenchCommandStatusMessageRouter(
        IWorkbenchCommandRouter inner,
        Action<EditorStatusMessageSnapshot> publishStatusMessage)
    {
        ArgumentNullException.ThrowIfNull(inner);
        ArgumentNullException.ThrowIfNull(publishStatusMessage);

        inner_ = inner;
        publishStatusMessage_ = publishStatusMessage;
    }

    public WorkbenchCommandExecutionResult Execute(string commandId)
    {
        var result = inner_.Execute(commandId);
        publishStatusMessage_(EditorStatusMessageSnapshot.FromCommandResult(result));
        return result;
    }
}
```

- [ ] **Step 4: Replace the MainWindow status message members**

In `Shell/ViewModels/MainWindowViewModel.cs`, replace:

```csharp
private EditorCommandFeedbackSnapshot? lastCommandFeedback_;
```

with:

```csharp
private EditorStatusMessageSnapshot? lastStatusMessage_;
private readonly RelayCommand openStatusMessageTargetCommand_;
```

Replace the command router construction:

```csharp
var commandRouter = new WorkbenchCommandFeedbackRouter(
    new WorkbenchCommandRouter(actionRegistry, actionExecutor),
    PublishCommandFeedback);
```

with:

```csharp
var commandRouter = new WorkbenchCommandStatusMessageRouter(
    new WorkbenchCommandRouter(actionRegistry, actionExecutor),
    PublishStatusMessage);
```

After `OpenPanelCommand = new RelayCommand<string?>(...)`, add:

```csharp
openStatusMessageTargetCommand_ = new RelayCommand(
    OpenStatusMessageTarget,
    () => CanOpenStatusMessageTarget);
OpenStatusMessageTargetCommand = openStatusMessageTargetCommand_;
```

Replace the old command-feedback property block with:

```csharp
public EditorStatusMessageSnapshot? LastStatusMessage
{
    get => lastStatusMessage_;
    private set
    {
        if (SetProperty(ref lastStatusMessage_, value))
        {
            OnPropertyChanged(nameof(HasStatusMessage));
            OnPropertyChanged(nameof(StatusMessageText));
            OnPropertyChanged(nameof(IsStatusMessageDebug));
            OnPropertyChanged(nameof(IsStatusMessageInfo));
            OnPropertyChanged(nameof(IsStatusMessageSuccess));
            OnPropertyChanged(nameof(IsStatusMessageWarning));
            OnPropertyChanged(nameof(IsStatusMessageError));
            OnPropertyChanged(nameof(CanOpenStatusMessageTarget));
            openStatusMessageTargetCommand_.NotifyCanExecuteChanged();
        }
    }
}

public bool HasStatusMessage => LastStatusMessage is not null;

public string StatusMessageText => LastStatusMessage?.Message ?? string.Empty;

public bool IsStatusMessageDebug =>
    LastStatusMessage?.Severity == EditorStatusMessageSeverity.Debug;

public bool IsStatusMessageInfo =>
    LastStatusMessage?.Severity == EditorStatusMessageSeverity.Info;

public bool IsStatusMessageSuccess =>
    LastStatusMessage?.Severity == EditorStatusMessageSeverity.Success;

public bool IsStatusMessageWarning =>
    LastStatusMessage?.Severity == EditorStatusMessageSeverity.Warning;

public bool IsStatusMessageError =>
    LastStatusMessage?.Severity == EditorStatusMessageSeverity.Error;

public bool CanOpenStatusMessageTarget =>
    !string.IsNullOrWhiteSpace(LastStatusMessage?.TargetPanelId);

public IRelayCommand OpenStatusMessageTargetCommand { get; }
```

Replace:

```csharp
private void PublishCommandFeedback(WorkbenchCommandExecutionResult result)
{
    LastCommandFeedback = EditorCommandFeedbackSnapshot.FromResult(result);
}
```

with:

```csharp
internal void PublishStatusMessage(EditorStatusMessageSnapshot snapshot)
{
    ArgumentNullException.ThrowIfNull(snapshot);
    LastStatusMessage = snapshot;
}

private void OpenStatusMessageTarget()
{
    if (LastStatusMessage?.TargetPanelId is not { } targetPanelId)
    {
        return;
    }

    _ = panelCommandService_.OpenOrFocusPanel(targetPanelId);
}
```

- [ ] **Step 5: Run focused tests and commit**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~EditorStatusMessageSnapshotTests"
```

Expected: PASS.

Commit:

```powershell
git add Shell\Commands\WorkbenchCommandStatusMessageRouter.cs Shell\ViewModels\MainWindowViewModel.cs Tests\Editor.Tests\Shell\ViewModels\MainWindowViewModelTests.cs
git add -u Shell\Commands Tests\Editor.Tests\Shell\ViewModels
git commit -m "feat: publish latest studio status messages"
```

## Task 3: Status Bar XAML Binding Rename

**Files:**
- Modify: `Shell/Views/MainWindow.axaml`
- Modify: `Tests/Editor.Tests/Shell/Views/MainWindowXamlTests.cs`

- [ ] **Step 1: Replace XAML structure test**

Replace the body of `Status_bar_binds_latest_command_feedback_with_severity_classes` in `Tests/Editor.Tests/Shell/Views/MainWindowXamlTests.cs` and rename the test:

```csharp
[Fact]
public void Status_bar_binds_latest_status_message_with_severity_classes_and_target_command()
{
    var xaml = LoadMainWindowXaml();

    Assert.Contains("Classes=\"status-message-status\"", xaml);
    Assert.Contains("Content=\"{Binding StatusMessageText}\"", xaml);
    Assert.Contains("Command=\"{Binding OpenStatusMessageTargetCommand}\"", xaml);
    Assert.Contains("IsHitTestVisible=\"{Binding CanOpenStatusMessageTarget}\"", xaml);
    Assert.Contains("IsVisible=\"{Binding HasStatusMessage}\"", xaml);
    Assert.Contains("Classes.debug=\"{Binding IsStatusMessageDebug}\"", xaml);
    Assert.Contains("Classes.success=\"{Binding IsStatusMessageSuccess}\"", xaml);
    Assert.Contains("Classes.warning=\"{Binding IsStatusMessageWarning}\"", xaml);
    Assert.Contains("Classes.error=\"{Binding IsStatusMessageError}\"", xaml);
    Assert.Contains("Classes.info=\"{Binding IsStatusMessageInfo}\"", xaml);
    Assert.Contains("EditorBrushSuccess", xaml);
    Assert.Contains("EditorBrushWarning", xaml);
    Assert.Contains("EditorBrushError", xaml);
    Assert.Contains("EditorBrushInfo", xaml);
    Assert.DoesNotContain("command-feedback-status", xaml, StringComparison.Ordinal);
    Assert.DoesNotContain("CommandFeedback", xaml, StringComparison.Ordinal);
}
```

- [ ] **Step 2: Run XAML test and verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowXamlTests"
```

Expected: FAIL because `MainWindow.axaml` still uses command-feedback class and bindings.

- [ ] **Step 3: Replace status style selectors**

In `Shell/Views/MainWindow.axaml`, replace the `TextBlock.command-feedback-status` style block with:

```xml
        <Style Selector="Button.status-message-status">
            <Setter Property="FontSize" Value="{DynamicResource EditorFontSizeSmall}" />
            <Setter Property="Foreground" Value="{DynamicResource EditorBrushTextMuted}" />
            <Setter Property="Background" Value="Transparent" />
            <Setter Property="BorderThickness" Value="0" />
            <Setter Property="Padding" Value="0" />
            <Setter Property="Margin" Value="12,0,0,0" />
            <Setter Property="MaxWidth" Value="520" />
            <Setter Property="HorizontalContentAlignment" Value="Right" />
            <Setter Property="VerticalContentAlignment" Value="Center" />
            <Setter Property="Focusable" Value="False" />
        </Style>
        <Style Selector="Button.status-message-status:pointerover">
            <Setter Property="Background" Value="Transparent" />
        </Style>
        <Style Selector="Button.status-message-status:pressed">
            <Setter Property="Background" Value="Transparent" />
        </Style>
        <Style Selector="Button.status-message-status.debug">
            <Setter Property="Foreground" Value="{DynamicResource EditorBrushInfo}" />
        </Style>
        <Style Selector="Button.status-message-status.success">
            <Setter Property="Foreground" Value="{DynamicResource EditorBrushSuccess}" />
        </Style>
        <Style Selector="Button.status-message-status.warning">
            <Setter Property="Foreground" Value="{DynamicResource EditorBrushWarning}" />
        </Style>
        <Style Selector="Button.status-message-status.error">
            <Setter Property="Foreground" Value="{DynamicResource EditorBrushError}" />
        </Style>
        <Style Selector="Button.status-message-status.info">
            <Setter Property="Foreground" Value="{DynamicResource EditorBrushInfo}" />
        </Style>
```

- [ ] **Step 4: Replace the status element binding**

In `Shell/Views/MainWindow.axaml`, replace the status `TextBlock` inside the status bar `DockPanel` with:

```xml
                <Button DockPanel.Dock="Right"
                        Classes="status-message-status"
                        Classes.debug="{Binding IsStatusMessageDebug}"
                        Classes.success="{Binding IsStatusMessageSuccess}"
                        Classes.warning="{Binding IsStatusMessageWarning}"
                        Classes.error="{Binding IsStatusMessageError}"
                        Classes.info="{Binding IsStatusMessageInfo}"
                        Command="{Binding OpenStatusMessageTargetCommand}"
                        Content="{Binding StatusMessageText}"
                        HorizontalAlignment="Right"
                        IsHitTestVisible="{Binding CanOpenStatusMessageTarget}"
                        IsVisible="{Binding HasStatusMessage}"
                        VerticalAlignment="Center" />
```

- [ ] **Step 5: Run focused XAML/ViewModel tests and commit**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowXamlTests|FullyQualifiedName~MainWindowViewModelTests"
```

Expected: PASS.

Commit:

```powershell
git add Shell\Views\MainWindow.axaml Tests\Editor.Tests\Shell\Views\MainWindowXamlTests.cs
git commit -m "feat: bind status bar to latest status message"
```

## Task 4: Documentation Sync

**Files:**
- Modify: `docs/Dock系统指南.md`
- Modify: `docs/编辑器UI平台规范.md`

- [ ] **Step 1: Update Dock guide implemented fact**

In `docs/Dock系统指南.md`, replace fact 44:

```markdown
44. Command result feedback v0 consumes `WorkbenchCommandExecutionResult`, maps it to UI-neutral `EditorCommandFeedbackSnapshot`, and publishes the latest menu/shortcut/palette command result through Shell status chrome; it intentionally does not add toast history, Problems/Console, native logs, plugin APIs, or modal failure dialogs.
```

with:

```markdown
44. Status/debug message v0 consumes `WorkbenchCommandExecutionResult` as the first producer, maps it to UI-neutral `EditorStatusMessageSnapshot`, and publishes latest status text through Shell status chrome; Console-targeted messages can open/focus Console, while command-result messages remain passive unless a target is explicitly provided. This intentionally does not add shell command input, toast history, combined Problems/Console panels, native logs, plugin APIs, or modal failure dialogs.
```

- [ ] **Step 2: Update UI platform sequence and status contract wording**

In `docs/编辑器UI平台规范.md`, replace:

```markdown
Command result feedback -> Background Tasks panel -> Diagnostics/Problems route -> Shortcut/Command settings
```

with:

```markdown
Status debug message surface -> Background Tasks panel -> Diagnostics/Problems route -> Shortcut/Command settings
```

In the status feedback section, ensure the wording says:

```markdown
Status feedback records are UI-level status/debug messages. Command execution results are the first producer; future Console/debug producers should set `TargetPanelId = "console"` so the status bar can open or focus Console. This layer does not ingest native engine logs and does not imply a shell command line.
```

Place that paragraph near the existing `Status feedback record` heading so future work does not treat command feedback as the whole model.

- [ ] **Step 3: Run documentation searches and commit**

Run:

```powershell
rg -n "EditorCommandFeedback|CommandFeedback|command-feedback-status|Command result feedback v0" Core Shell Tests docs
rg -n 'Status/debug message v0|Status debug message surface|TargetPanelId = "console"' docs\Dock系统指南.md docs\编辑器UI平台规范.md
```

Expected:

- First command returns no matches outside older historical plan/spec files under `docs/superpowers`.
- Second command finds the new Dock guide and UI platform wording.

Commit:

```powershell
git add docs\Dock系统指南.md docs\编辑器UI平台规范.md
git commit -m "docs: document status debug message boundary"
```

## Task 5: Full Verification And Cleanup

**Files:**
- All files touched by Tasks 1-4.

- [ ] **Step 1: Run focused regression suite**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorStatusMessageSnapshotTests|FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~MainWindowXamlTests|FullyQualifiedName~CommandPaletteViewModelTests|FullyQualifiedName~WorkbenchCommandRouterTests|FullyQualifiedName~WorkbenchMenuItemViewModelTests"
```

Expected: PASS.

- [ ] **Step 2: Run full Studio test project**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release
```

Expected: PASS.

- [ ] **Step 3: Run repository text and diff checks**

Run from `D:\TechArt\VkEngine-studio-frontend`:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Expected:

- `Missing required UTF-8 BOM: 0`
- `Unexpected UTF-8 BOM: 0`
- `Invalid UTF-8: 0`
- `git diff --check` exits 0 with no output.

- [ ] **Step 4: Confirm no current code still uses command-feedback names**

Run from `D:\TechArt\VkEngine-studio-frontend\apps\studio`:

```powershell
rg -n "EditorCommandFeedback|CommandFeedback|command-feedback-status|WorkbenchCommandFeedbackRouter" Core Shell Tests
```

Expected: no matches.

- [ ] **Step 5: Final integration commit if needed**

If Tasks 1-4 already committed every changed file, do not create an empty commit. If any verification-only fixes were needed, commit them:

```powershell
git add Core Shell Tests docs
git commit -m "test: verify studio status message surface"
```

## Self-Review

- Spec coverage: Tasks 1-3 cover the generic Core model, command-result first producer, ViewModel status surface, optional Console panel target command, and XAML binding rename. Task 4 covers the docs boundary for Console, Problems, and future shell command-line behavior. Task 5 covers focused and full verification.
- Scope check: The plan does not create Console history, Problems ingestion, shell command input, native log ingestion, runtime/renderer integration, plugin API, toast history, or a combined bottom diagnostics panel.
- Type consistency: The plan consistently uses `EditorStatusMessageSnapshot`, `EditorStatusMessageSeverity`, `EditorStatusMessageSource`, `WorkbenchCommandStatusMessageRouter`, `LastStatusMessage`, `StatusMessageText`, and `OpenStatusMessageTargetCommand`.
- Placeholder scan: The plan contains no placeholder markers; every implementation task has concrete file paths, code snippets, commands, and expected outcomes.
