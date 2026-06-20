# Studio Shortcut Command Routing Design

## Context

Issue: [#186](https://github.com/66six11/Engine/issues/186)
Branch: `codex/studio-shortcut-command-routing`

#182 / PR #183 added command catalog metadata, including display-only `DefaultShortcut` text. #184 / PR #185 added command-id execution through `WorkbenchCommandRouter`. The remaining gap is keyboard input: `MainWindow.axaml.cs` still hard-codes `Ctrl+Shift+P` and opens the command palette directly.

This slice adds the next narrow editor UI infrastructure layer: built-in shortcut routing from Avalonia key input to command ids. It does not add user-editable shortcuts, settings persistence, command menu redesign, popups, or background task feedback.

## Goals

- Parse built-in shortcut strings from `WorkbenchActionDescriptor.DefaultShortcut`.
- Match Avalonia key input against registered command shortcuts.
- Execute matched shortcuts through `WorkbenchCommandRouter`.
- Preserve the existing `Ctrl+Shift+P` command palette shortcut through the new route.
- Avoid stealing plain text input from text-editing controls.

## Current State

- `WorkbenchActionDescriptor.DefaultShortcut` is a nullable string used for display only.
- `WorkbenchActionKind` only has `OpenPanel`.
- `WorkbenchFeatureModule.RegisterActions()` only registers panel actions.
- `WorkbenchActionExecutor` can execute panel actions through `PanelCommandService`.
- `MainWindow.axaml.cs` hard-codes `IsCommandPaletteShortcut(Key.P, Control | Shift)`.
- `MainWindow.axaml` also has `InputGesture="Ctrl+Shift+P"` on the Tools menu item.

## Design

### Command Palette Action

Add a catalog action for opening the command palette:

- Id: `workbench.commandPalette.open`
- Title: `Command Palette`
- Kind: `OpenCommandPalette`
- MenuPath: `Tools/Command Palette`
- Category: `Tools`
- DefaultShortcut: `Ctrl+Shift+P`
- SearchText: `command palette launcher`

`WorkbenchActionExecutor` will support `OpenCommandPalette` through a small callback provided by `MainWindowViewModel`. This keeps the action executor as the descriptor-level dispatcher while avoiding a dependency from command infrastructure to Avalonia controls.

### Shortcut Gesture

Add `WorkbenchShortcutGesture` under `Editor.Shell.Commands`:

- `Key`
- `KeyModifiers`
- `TryParse(string? text, out WorkbenchShortcutGesture gesture)`
- `Matches(Key key, KeyModifiers modifiers)`

The parser accepts simple `+`-separated shortcuts such as `Ctrl+Shift+P`, `Control+Alt+C`, and `F1`. It ignores empty or invalid shortcut text. This slice does not support chord sequences, locale-specific key text, or user-defined bindings.

### Shortcut Router

Add `WorkbenchShortcutRouter` under `Editor.Shell.Commands`.

Responsibilities:

- Build shortcut bindings from registered actions with valid `DefaultShortcut` values.
- Preserve registration order for deterministic duplicate handling; the first matching binding wins.
- Ignore plain-text shortcuts when a text-input control owns the event.
- Execute matched commands through `IWorkbenchCommandRouter.Execute(commandId)`.
- Return `null` when no shortcut should handle the input.

The router returns `WorkbenchCommandExecutionResult?`. A non-null result means the key event matched a known shortcut and should be marked handled, regardless of success, disabled, or failed status.

### MainWindow Wiring

`MainWindowViewModel` owns one shortcut router alongside the command router:

```text
KeyDown in MainWindow
  -> MainWindowViewModel.ExecuteShortcut(key, modifiers, isTextInputFocused)
  -> WorkbenchShortcutRouter
  -> WorkbenchCommandRouter.Execute(commandId)
  -> WorkbenchActionExecutor
```

`MainWindow.axaml.cs` remains a view-only bridge:

- It reads `KeyEventArgs.Key`, `KeyEventArgs.KeyModifiers`, and whether the original event source is a text input.
- It calls `MainWindowViewModel.ExecuteShortcut(...)`.
- It sets `e.Handled = true` only when the view model returns a non-null result.

Remove the hard-coded menu `InputGesture="Ctrl+Shift+P"` so keyboard execution has one path. The menu click can still call `CommandPalette.OpenCommand`; moving the entire menu system behind command ids is a later slice.

## Non-goals

- No user-editable shortcut settings.
- No shortcut persistence.
- No shortcut conflict UI.
- No command menu redesign.
- No popup/dialog/toast host.
- No background task service.
- No undo/redo command transaction system.
- No scene runtime, native ABI, renderer, asset, or C++ changes.

## Acceptance Criteria

- `Ctrl+Shift+P` opens the command palette through `WorkbenchShortcutRouter -> WorkbenchCommandRouter`.
- Shortcut parsing and matching are unit-tested.
- Duplicate shortcuts are deterministic and first registration wins.
- Invalid shortcut text is ignored rather than crashing.
- Text-input focused controls do not trigger plain-text shortcuts.
- Existing panel commands and command palette command execution continue to work.

## Validation

- `dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchShortcut"`
- `dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~WorkbenchActionExecutorTests|FullyQualifiedName~MainWindowShortcutTests|FullyQualifiedName~MainWindowViewModelTests"`
- `dotnet test apps/studio/Editor.sln -c Release`
- `dotnet test apps/studio/Editor.sln`
- `powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1`
- `git diff --check`
