# Studio Command Router Results Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a central Studio command route that executes registered workbench commands by stable id and returns typed execution results.

**Architecture:** Keep `WorkbenchActionExecutor` as the descriptor-level dispatcher, add registry lookup plus a thin `WorkbenchCommandRouter` above it, and move command palette / panel menu execution to command ids. This keeps future shortcuts, command menus, and feedback UI on one route without adding those systems in this slice.

**Tech Stack:** C#/.NET, Avalonia MVVM, CommunityToolkit.Mvvm, xUnit.

---

## File Structure

- Create: `apps/studio/Core/Models/WorkbenchCommandExecutionStatus.cs`
  - Holds the result status enum.
- Create: `apps/studio/Core/Models/WorkbenchCommandExecutionResult.cs`
  - Holds the typed execution result record and static factory helpers.
- Modify: `apps/studio/Core/Abstractions/IWorkbenchActionRegistry.cs`
  - Add `FindById(string id)`.
- Modify: `apps/studio/Shell/Commands/WorkbenchActionRegistry.cs`
  - Implement lookup through the existing dictionary.
- Create: `apps/studio/Shell/Commands/WorkbenchCommandRouter.cs`
  - Resolve ids, guard disabled commands, dispatch through `IWorkbenchActionExecutor`, return typed results.
- Modify: `apps/studio/Shell/ViewModels/CommandPaletteViewModel.cs`
  - Execute selected command by id and close only on `Succeeded`.
- Modify: `apps/studio/Shell/ViewModels/PanelMenuItemViewModel.cs`
  - Execute panel menu command by id through the same route.
- Modify: `apps/studio/Shell/ViewModels/MainWindowViewModel.cs`
  - Compose one router and pass it to UI surfaces.
- Modify: `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionRegistryTests.cs`
  - Cover lookup behavior.
- Create: `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchCommandRouterTests.cs`
  - Cover success, not found, disabled, executor false, and exception outcomes.
- Modify: `apps/studio/Tests/Editor.Tests/Shell/ViewModels/CommandPaletteViewModelTests.cs`
  - Assert the palette executes by command id and respects typed results.
- Modify: `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`
  - Keep integration coverage for command palette and panel menu command routes.

## Task 1: Registry Lookup And Result Model

**Files:**
- Create: `apps/studio/Core/Models/WorkbenchCommandExecutionStatus.cs`
- Create: `apps/studio/Core/Models/WorkbenchCommandExecutionResult.cs`
- Modify: `apps/studio/Core/Abstractions/IWorkbenchActionRegistry.cs`
- Modify: `apps/studio/Shell/Commands/WorkbenchActionRegistry.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionRegistryTests.cs`

- [ ] **Step 1: Write failing registry lookup tests**

Add these tests to `WorkbenchActionRegistryTests`:

```csharp
[Fact]
public void FindById_returns_registered_action()
{
    var registry = new WorkbenchActionRegistry();
    var action = CreatePanelAction("workbench.panel.console", "Console", "console");
    registry.Register(action);

    var actual = registry.FindById("workbench.panel.console");

    Assert.Equal(action, actual);
}

[Fact]
public void FindById_returns_null_for_missing_action()
{
    var registry = new WorkbenchActionRegistry();

    Assert.Null(registry.FindById("missing.command"));
}

[Fact]
public void FindById_rejects_null_id()
{
    var registry = new WorkbenchActionRegistry();

    Assert.Throws<ArgumentNullException>(() => registry.FindById(null!));
}
```

- [ ] **Step 2: Run focused tests and verify RED**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter WorkbenchActionRegistryTests
```

Expected: FAIL because `IWorkbenchActionRegistry` / `WorkbenchActionRegistry` do not expose `FindById`.

- [ ] **Step 3: Add result model files**

Create `WorkbenchCommandExecutionStatus.cs`:

```csharp
namespace Editor.Core.Models;

public enum WorkbenchCommandExecutionStatus
{
    Succeeded,
    NotFound,
    Disabled,
    Failed,
}
```

Create `WorkbenchCommandExecutionResult.cs`:

```csharp
namespace Editor.Core.Models;

public sealed record WorkbenchCommandExecutionResult(
    WorkbenchCommandExecutionStatus Status,
    string CommandId,
    string? Message = null)
{
    public bool Succeeded => Status == WorkbenchCommandExecutionStatus.Succeeded;

    public static WorkbenchCommandExecutionResult Success(string commandId)
    {
        return new WorkbenchCommandExecutionResult(
            WorkbenchCommandExecutionStatus.Succeeded,
            commandId);
    }

    public static WorkbenchCommandExecutionResult NotFound(string commandId)
    {
        return new WorkbenchCommandExecutionResult(
            WorkbenchCommandExecutionStatus.NotFound,
            commandId,
            $"Command '{commandId}' is not registered.");
    }

    public static WorkbenchCommandExecutionResult Disabled(string commandId, string disabledReason)
    {
        return new WorkbenchCommandExecutionResult(
            WorkbenchCommandExecutionStatus.Disabled,
            commandId,
            disabledReason);
    }

    public static WorkbenchCommandExecutionResult Failed(string commandId, string message)
    {
        return new WorkbenchCommandExecutionResult(
            WorkbenchCommandExecutionStatus.Failed,
            commandId,
            message);
    }
}
```

- [ ] **Step 4: Add registry lookup**

Update `IWorkbenchActionRegistry`:

```csharp
using System.Collections.Generic;
using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface IWorkbenchActionRegistry
{
    void Register(WorkbenchActionDescriptor descriptor);

    IReadOnlyList<WorkbenchActionDescriptor> GetAll();

    WorkbenchActionDescriptor? FindById(string id);
}
```

Add to `WorkbenchActionRegistry`:

```csharp
public WorkbenchActionDescriptor? FindById(string id)
{
    ArgumentNullException.ThrowIfNull(id);

    return descriptors_.GetValueOrDefault(id);
}
```

- [ ] **Step 5: Run focused tests and commit**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter WorkbenchActionRegistryTests
```

Expected: PASS.

Commit:

```powershell
git add apps/studio/Core/Abstractions/IWorkbenchActionRegistry.cs apps/studio/Core/Models/WorkbenchCommandExecutionStatus.cs apps/studio/Core/Models/WorkbenchCommandExecutionResult.cs apps/studio/Shell/Commands/WorkbenchActionRegistry.cs apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionRegistryTests.cs
git commit -m "feat(studio): add command lookup and execution results"
```

## Task 2: Command Router

**Files:**
- Create: `apps/studio/Shell/Commands/WorkbenchCommandRouter.cs`
- Create: `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchCommandRouterTests.cs`

- [ ] **Step 1: Write failing router tests**

Create `WorkbenchCommandRouterTests.cs`:

```csharp
using System;
using Editor.Core.Models;
using Editor.Shell.Commands;
using Xunit;

namespace Editor.Tests.Shell.Commands;

public sealed class WorkbenchCommandRouterTests
{
    [Fact]
    public void Execute_returns_succeeded_when_executor_completes_command()
    {
        var action = CreateAction("workbench.panel.console");
        var registry = CreateRegistry(action);
        var executor = new CapturingActionExecutor(_ => true);
        var router = new WorkbenchCommandRouter(registry, executor);

        var result = router.Execute("workbench.panel.console");

        Assert.True(result.Succeeded);
        Assert.Equal(WorkbenchCommandExecutionStatus.Succeeded, result.Status);
        Assert.Equal("workbench.panel.console", executor.ExecutedActionId);
    }

    [Fact]
    public void Execute_returns_not_found_for_unknown_command()
    {
        var router = new WorkbenchCommandRouter(new WorkbenchActionRegistry(), new CapturingActionExecutor(_ => true));

        var result = router.Execute("missing.command");

        Assert.Equal(WorkbenchCommandExecutionStatus.NotFound, result.Status);
        Assert.Equal("missing.command", result.CommandId);
        Assert.Contains("not registered", result.Message);
    }

    [Fact]
    public void Execute_returns_disabled_without_dispatching()
    {
        var action = CreateAction("workbench.panel.disabled") with
        {
            IsEnabled = false,
            DisabledReason = "Disabled by test",
        };
        var executor = new CapturingActionExecutor(_ => true);
        var router = new WorkbenchCommandRouter(CreateRegistry(action), executor);

        var result = router.Execute("workbench.panel.disabled");

        Assert.Equal(WorkbenchCommandExecutionStatus.Disabled, result.Status);
        Assert.Equal("Disabled by test", result.Message);
        Assert.Null(executor.ExecutedActionId);
    }

    [Fact]
    public void Execute_returns_failed_when_executor_returns_false()
    {
        var router = new WorkbenchCommandRouter(
            CreateRegistry(CreateAction("workbench.panel.console")),
            new CapturingActionExecutor(_ => false));

        var result = router.Execute("workbench.panel.console");

        Assert.Equal(WorkbenchCommandExecutionStatus.Failed, result.Status);
        Assert.Contains("did not complete", result.Message);
    }

    [Fact]
    public void Execute_returns_failed_when_executor_throws()
    {
        var router = new WorkbenchCommandRouter(
            CreateRegistry(CreateAction("workbench.panel.console")),
            new CapturingActionExecutor(_ => throw new InvalidOperationException("boom")));

        var result = router.Execute("workbench.panel.console");

        Assert.Equal(WorkbenchCommandExecutionStatus.Failed, result.Status);
        Assert.Equal("boom", result.Message);
    }

    private static WorkbenchActionRegistry CreateRegistry(WorkbenchActionDescriptor action)
    {
        var registry = new WorkbenchActionRegistry();
        registry.Register(action);
        return registry;
    }

    private static WorkbenchActionDescriptor CreateAction(string id)
    {
        return new WorkbenchActionDescriptor(
            id,
            "Console",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Console",
            TargetId: "console",
            Category: "Window");
    }

    private sealed class CapturingActionExecutor(Func<WorkbenchActionDescriptor, bool> execute) : IWorkbenchActionExecutor
    {
        public string? ExecutedActionId { get; private set; }

        public bool Execute(WorkbenchActionDescriptor action)
        {
            ExecutedActionId = action.Id;
            return execute(action);
        }
    }
}
```

- [ ] **Step 2: Run focused tests and verify RED**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter WorkbenchCommandRouterTests
```

Expected: FAIL because `WorkbenchCommandRouter` does not exist.

- [ ] **Step 3: Implement router**

Create `WorkbenchCommandRouter.cs`:

```csharp
using System;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Shell.Commands;

internal interface IWorkbenchCommandRouter
{
    WorkbenchCommandExecutionResult Execute(string commandId);
}

internal sealed class WorkbenchCommandRouter : IWorkbenchCommandRouter
{
    private readonly IWorkbenchActionRegistry actionRegistry_;
    private readonly IWorkbenchActionExecutor actionExecutor_;

    public WorkbenchCommandRouter(
        IWorkbenchActionRegistry actionRegistry,
        IWorkbenchActionExecutor actionExecutor)
    {
        ArgumentNullException.ThrowIfNull(actionRegistry);
        ArgumentNullException.ThrowIfNull(actionExecutor);

        actionRegistry_ = actionRegistry;
        actionExecutor_ = actionExecutor;
    }

    public WorkbenchCommandExecutionResult Execute(string commandId)
    {
        ArgumentNullException.ThrowIfNull(commandId);

        var action = actionRegistry_.FindById(commandId);
        if (action is null)
        {
            return WorkbenchCommandExecutionResult.NotFound(commandId);
        }

        if (!action.IsEnabled)
        {
            return WorkbenchCommandExecutionResult.Disabled(
                commandId,
                action.DisabledReason ?? "Command is disabled.");
        }

        try
        {
            return actionExecutor_.Execute(action)
                ? WorkbenchCommandExecutionResult.Success(commandId)
                : WorkbenchCommandExecutionResult.Failed(commandId, $"Command '{commandId}' did not complete.");
        }
        catch (Exception exception)
        {
            return WorkbenchCommandExecutionResult.Failed(commandId, exception.Message);
        }
    }
}
```

- [ ] **Step 4: Run focused tests and commit**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter WorkbenchCommandRouterTests
```

Expected: PASS.

Commit:

```powershell
git add apps/studio/Shell/Commands/WorkbenchCommandRouter.cs apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchCommandRouterTests.cs
git commit -m "feat(studio): add command router"
```

## Task 3: UI Surface Wiring

**Files:**
- Modify: `apps/studio/Shell/ViewModels/CommandPaletteViewModel.cs`
- Modify: `apps/studio/Shell/ViewModels/PanelMenuItemViewModel.cs`
- Modify: `apps/studio/Shell/ViewModels/MainWindowViewModel.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/ViewModels/CommandPaletteViewModelTests.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`

- [ ] **Step 1: Update palette tests for command-id routing**

Change the helper signature in `CommandPaletteViewModelTests`:

```csharp
private static CommandPaletteViewModel CreatePalette(
    Func<string, WorkbenchCommandExecutionResult>? execute = null)
{
    return new CommandPaletteViewModel(
        [
            // existing descriptors
        ],
        execute ?? (commandId => WorkbenchCommandExecutionResult.Success(commandId)));
}
```

Update execution tests:

```csharp
[Fact]
public void ExecuteSelected_runs_command_by_id_and_closes_on_success()
{
    string? executedCommandId = null;
    var viewModel = CreatePalette(commandId =>
    {
        executedCommandId = commandId;
        return WorkbenchCommandExecutionResult.Success(commandId);
    });
    viewModel.OpenCommand.Execute(null);
    viewModel.Query = "console";

    viewModel.ExecuteSelectedCommand.Execute(null);

    Assert.Equal("workbench.panel.console", executedCommandId);
    Assert.False(viewModel.IsOpen);
}

[Fact]
public void ExecuteSelected_keeps_palette_open_when_command_fails()
{
    var viewModel = CreatePalette(commandId => WorkbenchCommandExecutionResult.Failed(commandId, "Failed by test"));
    viewModel.OpenCommand.Execute(null);

    viewModel.ExecuteSelectedCommand.Execute(null);

    Assert.True(viewModel.IsOpen);
}
```

Keep `ExecuteSelected_ignores_disabled_action`, but change its captured value to `string? executedCommandId`.

- [ ] **Step 2: Run palette tests and verify RED**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter CommandPaletteViewModelTests
```

Expected: FAIL because the palette still expects `Func<WorkbenchActionDescriptor, bool>`.

- [ ] **Step 3: Update palette and panel menu view models**

In `CommandPaletteViewModel`, replace the delegate field and constructor parameter:

```csharp
private readonly Func<string, WorkbenchCommandExecutionResult> executeCommand_;
```

Execute selected command by id:

```csharp
var result = executeCommand_(SelectedItem.Id);
if (result.Succeeded)
{
    Close();
}
```

In `PanelMenuItemViewModel`, replace the constructor delegate with:

```csharp
Func<string, WorkbenchCommandExecutionResult> executeCommand
```

and create the command with:

```csharp
OpenCommand = new RelayCommand(() => executeCommand(action.Id));
```

- [ ] **Step 4: Wire router in `MainWindowViewModel`**

After creating `WorkbenchActionExecutor`, create the router:

```csharp
var commandRouter = new WorkbenchCommandRouter(actionRegistry, actionExecutor);
```

Pass `commandRouter.Execute` to `CommandPaletteViewModel` and `CreatePanelMenuItems`.

Change `CreatePanelMenuItems` to accept `IWorkbenchCommandRouter commandRouter` and pass `commandRouter.Execute` into `PanelMenuItemViewModel`.

- [ ] **Step 5: Run focused tests and commit**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter "FullyQualifiedName~CommandPaletteViewModelTests|FullyQualifiedName~MainWindowViewModelTests"
```

Expected: PASS.

Commit:

```powershell
git add apps/studio/Shell/ViewModels/CommandPaletteViewModel.cs apps/studio/Shell/ViewModels/PanelMenuItemViewModel.cs apps/studio/Shell/ViewModels/MainWindowViewModel.cs apps/studio/Tests/Editor.Tests/Shell/ViewModels/CommandPaletteViewModelTests.cs apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs
git commit -m "feat(studio): route UI commands by id"
```

## Task 4: Full Validation

**Files:**
- No production file changes unless validation exposes an issue.

- [ ] **Step 1: Run focused command-router validation**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchActionRegistryTests|FullyQualifiedName~WorkbenchCommandRouterTests|FullyQualifiedName~CommandPaletteViewModelTests|FullyQualifiedName~MainWindowViewModelTests"
```

Expected: PASS.

- [ ] **Step 2: Run full Studio test suite**

Run:

```powershell
dotnet test apps/studio/Editor.sln -c Release
dotnet test apps/studio/Editor.sln
```

Expected: PASS.

- [ ] **Step 3: Run repository checks**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Expected: PASS. C# files must keep UTF-8 with BOM; markdown files must remain UTF-8 without BOM.

- [ ] **Step 4: Update #184 and prepare PR**

Comment on #184 with validation output, push `codex/studio-command-router-results`, and open a draft PR with `Refs #184` until final validation evidence is recorded in the issue. Use `Closes #184` only when the PR fully satisfies acceptance criteria and Done evidence is present.
