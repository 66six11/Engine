# Studio Command Result Feedback Design

## Intent

Add a shared Studio command feedback surface so command execution results are visible outside the command palette's local message. This slice stays in the editor framework layer: it consumes `WorkbenchCommandExecutionResult`, publishes a small UI-neutral feedback snapshot, and renders the latest result in the shell status area.

## Context

The current command path is already centralized:

```text
WorkbenchActionDescriptor
  -> WorkbenchActionRegistry
  -> WorkbenchCommandRouter.Execute(commandId)
  -> WorkbenchActionExecutor
  -> WorkbenchCommandExecutionResult
```

Current gaps:

- `CommandPaletteViewModel` has local feedback for failed, disabled, or missing palette executions.
- Generated menu items dispatch commands but discard the returned result.
- Shortcuts return `WorkbenchCommandExecutionResult?` to `MainWindow.axaml.cs`, but the result is only used to mark the key event handled.
- The status bar currently shows background task activity only.

## External Patterns

- Avalonia compiled bindings favor typed ViewModel properties and `x:DataType`, so the status surface should consume stable Shell ViewModel properties rather than view-side result interpretation.
- Avalonia design-time guidance supports data-only view models and previewable visual states, so command feedback should remain a plain snapshot that can be bound in XAML.
- Unity, Godot, and Unreal editor tooling all separate editor command entry points from heavy runtime/plugin ownership. Studio should copy the editor-shell command/status pattern, not the extension ABI.

## Goals

- Convert every `WorkbenchCommandExecutionResult` status into a deterministic UI-neutral feedback snapshot.
- Publish the latest command feedback through `MainWindowViewModel`.
- Route menu, shortcut, and command palette command execution through the same feedback publishing path where practical.
- Show the latest command feedback in the existing shell status chrome with severity and concise text.
- Preserve the palette's local failure message for in-overlay context.
- Keep ordinary command failure non-modal and non-blocking.

## Non-Goals

- No toast animation framework.
- No notification center or feedback history list.
- No Problems, Console, or diagnostics panel implementation.
- No native log ingestion, renderer/runtime integration, asset pipeline, Play Session, provider lifecycle, plugin loader, hot reload, or native bridge.
- No shortcut/settings UI.
- No modal dialog for ordinary command failure.

## Design

### Feedback Snapshot

Add a Core UI-neutral snapshot:

```text
EditorCommandFeedbackSnapshot
  Severity: EditorCommandFeedbackSeverity
  Status: WorkbenchCommandExecutionStatus
  CommandId: string
  Message: string
```

Severity mapping:

| Result status | Feedback severity | Message rule |
| --- | --- | --- |
| `Succeeded` | `Success` | `Command '<id>' completed.` |
| `Disabled` | `Warning` | result message, or `Command '<id>' is disabled.` |
| `NotFound` | `Error` | result message, or `Command '<id>' is not registered.` |
| `Failed` | `Error` | result message, or `Command '<id>' did not complete.` |

The snapshot intentionally stores command id rather than title. Resolving friendly titles can be added later if a status history or diagnostics record needs action metadata.

### Publishing Path

`MainWindowViewModel` should wrap the existing `WorkbenchCommandRouter` with a small Shell router decorator that:

1. Executes the inner command router.
2. Converts the result to `EditorCommandFeedbackSnapshot`.
3. Publishes it through a callback.
4. Returns the original `WorkbenchCommandExecutionResult` unchanged.

The wrapped router should be passed to:

- `CommandPaletteViewModel`
- `WorkbenchShortcutRouter`
- generated Tools and Help menu items
- generated Panel menu items

This keeps command execution semantics unchanged while ensuring all shared command entry points publish feedback.

### Status Surface

`MainWindowViewModel` exposes latest feedback properties for binding:

- `LastCommandFeedback`
- `HasCommandFeedback`
- `CommandFeedbackMessage`
- severity booleans such as `IsCommandFeedbackSuccess`, `IsCommandFeedbackWarning`, `IsCommandFeedbackError`, and `IsCommandFeedbackInfo`

`MainWindow.axaml` adds a compact status text next to the existing `ActivityIndicator`. The view only selects visual styling from bound severity booleans; it does not interpret command statuses.

### Palette Behavior

The command palette keeps its local `LastResultMessage` behavior for failed, disabled, and not-found command execution. Because the palette receives the wrapped command delegate, the same result also appears in the global status surface.

Disabled palette rows that are ignored before routing remain ignored. Disabled results are still visible when a disabled command reaches the router, such as through a registered shortcut or direct command route.

## Data Flow

```text
Menu / shortcut / command palette
  -> feedback router decorator
  -> WorkbenchCommandRouter.Execute(commandId)
  -> WorkbenchCommandExecutionResult
  -> EditorCommandFeedbackSnapshot.FromResult(result)
  -> MainWindowViewModel.LastCommandFeedback
  -> MainWindow status chrome binding
```

## Testing

Focused tests should cover:

- snapshot mapping for success, disabled, not-found, failed, and blank failure messages
- menu command execution updates latest command feedback
- shortcut command execution updates latest command feedback
- command palette failure updates both local palette message and global latest feedback
- feedback property notifications include message, visibility, and severity flags

Integration coverage should use existing ViewModel-level tests. No browser/native/runtime validation is required for this editor-framework-only slice.

## Documentation Updates

Update `docs/Dock系统指南.md` after implementation to record:

- Command result feedback v0 consumes `WorkbenchCommandExecutionResult`.
- Menu, shortcut, and command palette commands publish latest feedback through Shell status chrome.
- The slice intentionally does not add toast history, Problems/Console, native logs, plugin APIs, or modal failure dialogs.

## References

- Avalonia compiled bindings: https://docs.avaloniaui.net/docs/data-binding/compiled-bindings
- Avalonia XAML preview and design settings: https://docs.avaloniaui.net/docs/app-development/xaml-preview-and-design-settings
- Unity Editor windows: https://docs.unity3d.com/Manual/editor-EditorWindows.html
- Godot editor dock plugins: https://docs.godotengine.org/en/stable/tutorials/plugins/editor/making_plugins.html#a-custom-dock
- Unreal Editor Utility Widgets: https://dev.epicgames.com/documentation/en-us/unreal-engine/editor-utility-widgets-in-unreal-engine
