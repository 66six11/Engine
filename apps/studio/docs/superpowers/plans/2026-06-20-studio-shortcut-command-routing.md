# Studio Shortcut Command Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Route built-in Studio keyboard shortcuts to registered workbench command ids and execute them through `WorkbenchCommandRouter`.

**Architecture:** Add one command catalog action for opening the command palette, then add a small shortcut parser/router in `Editor.Shell.Commands`. Keep Avalonia event plumbing in `MainWindow.axaml.cs`; keep command matching and execution testable outside the view.

**Tech Stack:** C#/.NET 10, Avalonia 12.0.4, CommunityToolkit.Mvvm, xUnit.

---

## File Structure

- Modify: `apps/studio/Core/Models/WorkbenchActionKind.cs`
  - Add `OpenCommandPalette`.
- Modify: `apps/studio/Shell/Commands/WorkbenchActionExecutor.cs`
  - Execute `OpenCommandPalette` through an optional callback.
- Modify: `apps/studio/Features/Workbench/WorkbenchFeatureModule.cs`
  - Register `workbench.commandPalette.open` with `DefaultShortcut="Ctrl+Shift+P"`.
- Create: `apps/studio/Shell/Commands/WorkbenchShortcutGesture.cs`
  - Parse and match shortcut strings.
- Create: `apps/studio/Shell/Commands/WorkbenchShortcutRouter.cs`
  - Build bindings from actions and execute matches through command router.
- Modify: `apps/studio/Shell/ViewModels/MainWindowViewModel.cs`
  - Create the shortcut router and expose `ExecuteShortcut(...)`.
- Modify: `apps/studio/Shell/Views/MainWindow.axaml.cs`
  - Replace hard-coded command palette shortcut detection with shortcut router delegation.
- Modify: `apps/studio/Shell/Views/MainWindow.axaml`
  - Remove `InputGesture="Ctrl+Shift+P"` from the command palette menu item.
- Modify: focused tests under `apps/studio/Tests/Editor.Tests/`.

## Task 1: Catalog Command Palette Action

**Files:**
- Modify: `apps/studio/Core/Models/WorkbenchActionKind.cs`
- Modify: `apps/studio/Shell/Commands/WorkbenchActionExecutor.cs`
- Modify: `apps/studio/Features/Workbench/WorkbenchFeatureModule.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionExecutorTests.cs`

- [ ] **Step 1: Write failing tests**

Add a command palette action to the expected `RegisterActions_registers_stable_workbench_panel_actions` array:

```csharp
new WorkbenchActionDescriptor(
    "workbench.commandPalette.open",
    "Command Palette",
    WorkbenchActionKind.OpenCommandPalette,
    "Tools/Command Palette",
    IconKey: EditorIconKey.UiSearch,
    Category: "Tools",
    DefaultShortcut: "Ctrl+Shift+P",
    SearchText: "command palette launcher"),
```

Add an executor test:

```csharp
[Fact]
public void Execute_open_command_palette_action_invokes_callback()
{
    var openCount = 0;
    var executor = new WorkbenchActionExecutor(
        new PanelCommandService(CreateWorkspace()),
        () =>
        {
            openCount++;
            return true;
        });

    var action = new WorkbenchActionDescriptor(
        "workbench.commandPalette.open",
        "Command Palette",
        WorkbenchActionKind.OpenCommandPalette,
        "Tools/Command Palette");

    Assert.True(executor.Execute(action));
    Assert.Equal(1, openCount);
}
```

- [ ] **Step 2: Run tests and verify RED**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~WorkbenchActionExecutorTests"
```

Expected: FAIL because `OpenCommandPalette` does not exist and the feature module does not register the action.

- [ ] **Step 3: Implement the command palette action**

Update `WorkbenchActionKind`:

```csharp
namespace Editor.Core.Models;

public enum WorkbenchActionKind
{
    OpenPanel,
    OpenCommandPalette,
}
```

Update `WorkbenchActionExecutor` constructor and switch:

```csharp
private readonly Func<bool>? openCommandPalette_;

public WorkbenchActionExecutor(
    PanelCommandService panelCommandService,
    Func<bool>? openCommandPalette = null)
{
    ArgumentNullException.ThrowIfNull(panelCommandService);

    panelCommandService_ = panelCommandService;
    openCommandPalette_ = openCommandPalette;
}

return action.Kind switch
{
    WorkbenchActionKind.OpenPanel => panelCommandService_.OpenOrFocusPanel(action.TargetId),
    WorkbenchActionKind.OpenCommandPalette => openCommandPalette_?.Invoke() ?? false,
    _ => false,
};
```

In `WorkbenchFeatureModule.RegisterActions`, register the command palette action before panel actions.

- [ ] **Step 4: Run tests and commit**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~WorkbenchActionExecutorTests"
```

Expected: PASS.

Commit:

```powershell
git add apps/studio/Core/Models/WorkbenchActionKind.cs apps/studio/Shell/Commands/WorkbenchActionExecutor.cs apps/studio/Features/Workbench/WorkbenchFeatureModule.cs apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionExecutorTests.cs
git commit -m "feat(studio): register command palette action"
```

## Task 2: Shortcut Parser And Router

**Files:**
- Create: `apps/studio/Shell/Commands/WorkbenchShortcutGesture.cs`
- Create: `apps/studio/Shell/Commands/WorkbenchShortcutRouter.cs`
- Create: `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchShortcutGestureTests.cs`
- Create: `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchShortcutRouterTests.cs`

- [ ] **Step 1: Write failing parser tests**

Create `WorkbenchShortcutGestureTests` with tests for:

```csharp
Assert.True(WorkbenchShortcutGesture.TryParse("Ctrl+Shift+P", out var gesture));
Assert.Equal(Key.P, gesture.Key);
Assert.Equal(KeyModifiers.Control | KeyModifiers.Shift, gesture.Modifiers);
Assert.True(gesture.Matches(Key.P, KeyModifiers.Control | KeyModifiers.Shift));
Assert.False(WorkbenchShortcutGesture.TryParse("", out _));
Assert.False(WorkbenchShortcutGesture.TryParse("Ctrl+NotAKey", out _));
```

- [ ] **Step 2: Write failing router tests**

Create `WorkbenchShortcutRouterTests` with tests for:

```csharp
// Match executes the command id through IWorkbenchCommandRouter.
// Duplicate shortcuts execute the first registered action.
// Invalid shortcut text is ignored.
// Plain shortcuts do not execute when text input is focused.
// Modifier shortcuts still execute when text input is focused.
```

Use a fake `IWorkbenchCommandRouter` that records the executed command id and returns `WorkbenchCommandExecutionResult.Success(commandId)`.

- [ ] **Step 3: Run tests and verify RED**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter WorkbenchShortcut
```

Expected: FAIL because shortcut gesture and router types do not exist.

- [ ] **Step 4: Implement shortcut gesture and router**

Implement parser and router in `Editor.Shell.Commands`. Use `StringComparer.OrdinalIgnoreCase` for modifier text and `Enum.TryParse<Key>` for keys. Router should expose:

```csharp
public WorkbenchCommandExecutionResult? TryExecute(
    Key key,
    KeyModifiers modifiers,
    bool isTextInputFocused)
```

Return `null` for no match or ignored plain-text input.

- [ ] **Step 5: Run tests and commit**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter WorkbenchShortcut
```

Expected: PASS.

Commit:

```powershell
git add apps/studio/Shell/Commands/WorkbenchShortcutGesture.cs apps/studio/Shell/Commands/WorkbenchShortcutRouter.cs apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchShortcutGestureTests.cs apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchShortcutRouterTests.cs
git commit -m "feat(studio): add shortcut command router"
```

## Task 3: MainWindow Shortcut Wiring

**Files:**
- Modify: `apps/studio/Shell/ViewModels/MainWindowViewModel.cs`
- Modify: `apps/studio/Shell/Views/MainWindow.axaml.cs`
- Modify: `apps/studio/Shell/Views/MainWindow.axaml`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/Views/MainWindowShortcutTests.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`

- [ ] **Step 1: Write failing integration tests**

Replace hard-coded `IsCommandPaletteShortcut` tests with tests proving:

```csharp
Assert.True(MainWindow.IsTextInputShortcutSource(new TextBox()));
Assert.False(MainWindow.IsTextInputShortcutSource(new Border()));
```

Add a `MainWindowViewModelTests` case:

```csharp
[Fact]
public void ExecuteShortcut_opens_command_palette_through_registered_shortcut()
{
    var viewModel = CreateMainWindowViewModel();

    var result = viewModel.ExecuteShortcut(
        Key.P,
        KeyModifiers.Control | KeyModifiers.Shift,
        isTextInputFocused: false);

    Assert.NotNull(result);
    Assert.True(result.Succeeded);
    Assert.True(viewModel.CommandPalette.IsOpen);
}
```

- [ ] **Step 2: Run tests and verify RED**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowShortcutTests|FullyQualifiedName~MainWindowViewModelTests"
```

Expected: FAIL because `ExecuteShortcut` and `IsTextInputShortcutSource` do not exist.

- [ ] **Step 3: Wire shortcut router**

In `MainWindowViewModel`, create `WorkbenchShortcutRouter` from `actions` and `commandRouter`, and add:

```csharp
internal WorkbenchCommandExecutionResult? ExecuteShortcut(
    Key key,
    KeyModifiers keyModifiers,
    bool isTextInputFocused)
{
    return shortcutRouter_.TryExecute(key, keyModifiers, isTextInputFocused);
}
```

In `MainWindow.axaml.cs`, replace hard-coded shortcut logic with:

```csharp
var result = viewModel.ExecuteShortcut(
    e.Key,
    e.KeyModifiers,
    IsTextInputShortcutSource(e.Source));
if (result is not null)
{
    e.Handled = true;
}
```

Remove `InputGesture="Ctrl+Shift+P"` from `MainWindow.axaml`.

- [ ] **Step 4: Run tests and commit**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchShortcut|FullyQualifiedName~MainWindowShortcutTests|FullyQualifiedName~MainWindowViewModelTests"
```

Expected: PASS.

Commit:

```powershell
git add apps/studio/Shell/ViewModels/MainWindowViewModel.cs apps/studio/Shell/Views/MainWindow.axaml.cs apps/studio/Shell/Views/MainWindow.axaml apps/studio/Tests/Editor.Tests/Shell/Views/MainWindowShortcutTests.cs apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs
git commit -m "feat(studio): route main window shortcuts"
```

## Task 4: Full Validation And PR

**Files:**
- No production file changes unless validation exposes an issue.

- [ ] **Step 1: Run focused validation**

Run:

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchShortcut|FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~WorkbenchActionExecutorTests|FullyQualifiedName~MainWindowShortcutTests|FullyQualifiedName~MainWindowViewModelTests"
```

Expected: PASS.

- [ ] **Step 2: Run full Studio test suites**

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

- [ ] **Step 4: Update #186 and create PR**

Comment validation output on #186, push `codex/studio-shortcut-command-routing`, and open a PR. Use `Refs #186` until final Done evidence is recorded, then switch to `Closes #186` when acceptance criteria and validation evidence are complete.
