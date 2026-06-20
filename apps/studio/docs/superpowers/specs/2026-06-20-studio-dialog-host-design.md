# Studio Modal Dialog Host Design

Issue: [#190](https://github.com/66six11/Engine/issues/190)
Branch: `codex/studio-dialog-host`

## Context

Studio now has command catalog metadata, command routing with typed execution results, shortcut routing, and catalog-backed command menu entries. The next editor UI foundation should not be scene/runtime work yet. The missing surface is a small, shared way for editor commands to ask the user for confirmation or show information without using ad hoc panel UI.

The existing `CommandPaletteView` already proves the main-window overlay pattern: a scrim, a centered/top-level surface, Escape handling, and view-only focus/key bridging in code-behind. The dialog host should follow that approach but expose a reusable request/result model instead of being tied to command search.

## Goals

- Add a main-window-level modal dialog host for Studio editor UI.
- Support one active in-app modal dialog at a time.
- Represent dialog requests and completion results with typed models.
- Support basic informational and confirmation dialogs.
- Render title, message, and explicit action buttons with existing theme resources.
- Support cancellation for cancelable dialogs, including Escape from the dialog view.
- Add a manual entry point by enabling `Help > About` as a catalog-backed command that opens an informational dialog.
- Cover the host and About command path with focused tests.

## Non-goals

- No toast or notification queue.
- No background loading/progress feedback.
- No native OS dialogs or file pickers.
- No multi-dialog stacking.
- No custom form content injection.
- No command-result toast integration.
- No undo/redo transaction prompts.
- No scene runtime, C++ ABI, renderer, asset, or native bridge work.

## Recommended Approach

Use an in-app modal host owned by `MainWindowViewModel`.

### Core Models

Add small dialog models under `Editor.Core.Models`:

- `EditorDialogKind`
  - `Information`
  - `Confirmation`
- `EditorDialogResultKind`
  - `Accepted`
  - `Rejected`
  - `Canceled`
- `EditorDialogButtonRole`
  - `Accept`
  - `Reject`
  - `Cancel`
- `EditorDialogButtonDescriptor`
  - stable button id.
  - display text.
  - role.
  - default-button flag.
- `EditorDialogRequest`
  - kind.
  - title.
  - message.
  - cancelable flag.
  - ordered buttons.
- `EditorDialogResult`
  - result kind.
  - selected button id.

The models stay editor-facing and UI-agnostic. They do not depend on Avalonia controls.

### Host View Model

Add `EditorDialogHostViewModel` under `Editor.Shell.ViewModels`.

Responsibilities:

- Hold the active request.
- Expose `IsOpen`.
- Expose projected button view models.
- Start a dialog with `ShowAsync(EditorDialogRequest request)`.
- Complete the active dialog when a button is clicked.
- Cancel the active dialog when Escape is pressed and the request is cancelable.

`ShowAsync` returns `Task<EditorDialogResult>`. Showing a dialog completes only when the user chooses a button or cancels. If a second dialog is requested while one is active, the host returns a failed operation by throwing `InvalidOperationException`; command routing will convert that to a failed command result through the existing router. This keeps the first slice honest about one active modal at a time.

### Command Integration

Add a new workbench action kind:

- `OpenAboutDialog`

Register an action in `WorkbenchFeatureModule`:

- id: `workbench.about.open`
- title: `About`
- kind: `OpenAboutDialog`
- menu path: `Help/About`
- category: `Help`

Extend `WorkbenchActionExecutor` with a small callback for opening About. `MainWindowViewModel` owns the callback and uses the dialog host to show an informational dialog. The command is considered successful when the dialog is shown; it does not wait for the user to close the dialog.

Project `Help/*` catalog actions into `HelpMenuItems`, mirroring the existing `ToolsMenuItems` path. Keep `Help > Documentation` as a disabled static placeholder for now.

### View Integration

Add `EditorDialogHostView` as the final overlay child in `MainWindow.axaml`, above the command palette. This gives modal dialogs priority over command search if both are open.

The view should use:

- `EditorBrushOverlayScrim` for the scrim.
- `EditorBrushSurfaceOverlay` for the dialog surface.
- `EditorBrushBorderDefault` for the surface border.
- compact title, body, and button layout consistent with the editor shell.

Code-behind is allowed only for view bridging:

- Escape key handling.
- optional focus placement on the default button after opening.

Dialog state and results stay in the view model.

## Error Handling

- Disabled or missing commands continue to be handled by `WorkbenchCommandRouter`.
- If About is requested while another modal is active, `WorkbenchActionExecutor` returns failure through the router's exception handling path.
- Canceling a non-cancelable dialog returns `false` and leaves the dialog open.
- Dialog button descriptors are validated when constructing requests so empty ids/text are caught early.

## Testing Strategy

Add focused unit tests before implementation:

- `EditorDialogHostViewModelTests`
  - showing a request opens the host.
  - clicking an accept button completes the result and closes the host.
  - Escape/cancel completes cancelable dialogs with `Canceled`.
  - non-cancelable dialogs ignore cancel.
  - a second `ShowAsync` while active throws.
- `MainWindowViewModelTests`
  - `HelpMenuItems` contains `About` from the command catalog.
  - executing the About menu item opens the dialog host.
- Existing command palette/menu/shortcut tests should continue to pass.

No visual snapshot test is required in this slice; the view uses simple Avalonia layout and existing theme resources. Build and view model tests are enough for the first foundation.

## Acceptance Criteria

- `MainWindowViewModel` exposes a dialog host.
- The dialog host can show a typed request and complete with a typed result.
- Cancelable dialogs can be canceled through the host.
- Non-cancelable dialogs are not dismissed by cancel.
- `Help > About` is catalog-backed and opens an informational modal dialog.
- Existing command palette, menu, shortcut, and panel behavior remains intact.

## Validation

- `dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter "FullyQualifiedName~Dialog|FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~MainWindowShortcutTests"`
- `dotnet test apps/studio/Editor.sln -c Release`
- `dotnet test apps/studio/Editor.sln`
- `powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1`
- `git diff --check`

## Follow-up Slices

- Toast/notification queue for non-blocking feedback.
- Background task/progress feedback.
- Command execution result presentation.
- Confirmation prompts for destructive editor operations once those commands exist.
