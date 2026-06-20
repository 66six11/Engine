# Studio Command Router Results Design

## Context

Issue: [#184](https://github.com/66six11/Engine/issues/184)
Branch: `codex/studio-command-router-results`

The previous slice, #182 / PR #183, made Studio workbench actions searchable and self-describing. The command palette still executes by receiving a `WorkbenchActionDescriptor` delegate that returns `bool`. That is enough for the current palette, but it is not a stable base for shortcuts, command menus, popups, or background loading feedback because each surface would need its own way to resolve command ids and interpret failure.

This slice adds the next narrow infrastructure layer: execute a command by stable id and return a typed result. It deliberately does not implement shortcuts, command menu polish, popups, notifications, background tasks, scene runtime, or native C++ integration.

## Goals

- Provide one central Studio route for executing registered workbench commands by id.
- Return an explicit command execution result instead of a bare `bool`.
- Preserve existing panel-opening behavior from the command palette and panel menu.
- Keep disabled-command reasons available to callers.
- Keep the implementation local to Studio editor UI infrastructure.

## Current State

- `WorkbenchActionRegistry` stores descriptors in a dictionary but only exposes `GetAll()`.
- `WorkbenchActionExecutor.Execute(WorkbenchActionDescriptor)` delegates `OpenPanel` actions to `PanelCommandService` and returns `bool`.
- `CommandPaletteViewModel` receives `IReadOnlyList<WorkbenchActionDescriptor>` plus `Func<WorkbenchActionDescriptor, bool>`.
- `PanelMenuItemViewModel` receives a descriptor and executes the descriptor directly through a boolean delegate.
- `MainWindowViewModel` is the composition point for registry, executor, command palette, and panel menu items.

## Design

### Result Contract

Add a small command execution result model under `Editor.Core.Models`:

- `WorkbenchCommandExecutionStatus`
  - `Succeeded`
  - `NotFound`
  - `Disabled`
  - `Failed`
- `WorkbenchCommandExecutionResult`
  - `Status`
  - `CommandId`
  - `Message`
  - `Succeeded`

The message is optional for success, required by convention for disabled, not-found, and failed outcomes. Disabled results use the descriptor's `DisabledReason`.

### Registry Lookup

Extend `IWorkbenchActionRegistry` with:

```csharp
WorkbenchActionDescriptor? FindById(string id);
```

`WorkbenchActionRegistry` already has a dictionary keyed by id, so this is a small exposure of existing state. The method returns `null` for missing ids and throws `ArgumentNullException` for a null id. Empty strings are treated as missing commands rather than registration errors because command execution requests can originate from UI/input surfaces.

### Router

Add `WorkbenchCommandRouter` under `Editor.Shell.Commands`.

Responsibilities:

- Resolve command ids through `IWorkbenchActionRegistry.FindById`.
- Return `NotFound` when no descriptor exists.
- Return `Disabled` before dispatch when the descriptor is disabled.
- Dispatch enabled descriptors through the existing `IWorkbenchActionExecutor`.
- Return `Succeeded` when the executor returns `true`.
- Return `Failed` when the executor returns `false` or throws.

The router does not know about panels, shortcuts, menus, dialogs, or background jobs. Those remain separate layers.

### UI Wiring

Change UI surfaces to execute by command id:

- `CommandPaletteViewModel` receives `Func<string, WorkbenchCommandExecutionResult>` and calls it with `SelectedItem.Id`.
- `PanelMenuItemViewModel` receives the same command-id route and calls it with the action id captured at construction.
- `MainWindowViewModel` creates one `WorkbenchCommandRouter` and passes `router.Execute` to the palette and panel menu items.

The command palette still keeps disabled rows non-executable from the UI. The router also handles disabled commands so later non-palette surfaces, such as shortcuts, get the same result semantics.

## Data Flow

```text
EditorFeatureModule.RegisterActions
  -> WorkbenchActionRegistry
  -> MainWindowViewModel creates WorkbenchCommandRouter
  -> CommandPaletteViewModel / PanelMenuItemViewModel call Execute(commandId)
  -> WorkbenchCommandRouter resolves descriptor
  -> WorkbenchActionExecutor dispatches descriptor
  -> WorkbenchCommandExecutionResult returns to UI surface
```

## Error Handling

- Missing command id: return `NotFound` with a short diagnostic message.
- Disabled command: return `Disabled` with the descriptor disabled reason.
- Executor returns `false`: return `Failed` with a short diagnostic message.
- Executor throws: return `Failed` with the exception message.

The router catches executor exceptions because UI command surfaces should receive a result they can display or log later. This does not add a logging or notification system in this slice.

## Non-goals

- No shortcut binding/router implementation.
- No command palette visual redesign.
- No popup, dialog, toast, notification, or background task UI.
- No async command/job model.
- No undo/redo or document dirty-state command transactions.
- No scene runtime, native ABI, renderer, asset, or C++ changes.

## Acceptance Criteria

- Commands can be executed by stable id through one central route.
- Results distinguish success, not found, disabled, and failed outcomes.
- Disabled results preserve the disabled reason.
- The command palette and panel menu still open/focus existing panels.
- Tests cover registry lookup, router outcomes, palette routing, and main-window integration.

## Validation

- `dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchActionRegistryTests|FullyQualifiedName~WorkbenchCommandRouterTests|FullyQualifiedName~CommandPaletteViewModelTests|FullyQualifiedName~MainWindowViewModelTests"`
- `dotnet test apps/studio/Editor.sln -c Release`
- `dotnet test apps/studio/Editor.sln`
- `powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1`
- `git diff --check`
