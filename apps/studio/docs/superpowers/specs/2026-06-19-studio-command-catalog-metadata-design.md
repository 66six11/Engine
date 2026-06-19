# Studio Command Catalog Metadata Design

## Goal

Stabilize the first editor UI infrastructure step by turning the current Workbench action list into a richer command catalog that can later drive shortcuts, menus, command palette entries, popups, and background-task feedback through one shared command identity.

This slice is intentionally metadata-first. It should keep existing panel-opening behavior working while adding the command facts needed by later slices.

## Current Context

Studio already has these foundations:

- `Core/Models/WorkbenchActionDescriptor.cs` defines the registered action facts.
- `Core/Models/WorkbenchActionKind.cs` currently only contains `OpenPanel`.
- `Shell/Commands/WorkbenchActionRegistry.cs` validates and stores action descriptors.
- `Shell/Commands/WorkbenchActionExecutor.cs` executes descriptors by delegating `OpenPanel` actions to `PanelCommandService`.
- `Shell/ViewModels/CommandPaletteViewModel.cs` lists and searches actions.
- `Shell/ViewModels/MainWindowViewModel.cs` builds panel menu items and the command palette from the same action registry.
- `Shell/Views/MainWindow.axaml.cs` currently hard-codes the `Ctrl+Shift+P` command palette shortcut.

The next stable step is not a full command framework. The next step is to make the command catalog expressive enough that future routers and surfaces do not invent separate metadata models.

## Non-goals

- Do not implement a shortcut router.
- Do not implement a shortcut editor or persisted user keybindings.
- Do not implement popup, dialog, toast, or background task services.
- Do not implement async command execution.
- Do not implement undo/redo, transaction, dirty state, or document mutation.
- Do not add plugin commands or extension contribution loading.
- Do not rename the existing workbench action files unless the implementation needs it for clarity.
- Do not change scene, asset, renderer, native ABI, or C++ code.

## Design

Extend `WorkbenchActionDescriptor` as the command catalog record for now. Keep the current name to avoid churn, but make its shape command-ready.

Add metadata fields:

- `Category`: a short grouping label such as `Window`, `Layout`, `Tools`, or `Help`.
- `DefaultShortcut`: a display-and-routing string such as `Ctrl+Shift+P`; v1 only stores and displays it.
- `Scope`: command availability scope. v1 supports `Global` and reserves future `FocusedPanel`.
- `IsEnabled`: static command availability for v1.
- `DisabledReason`: short text shown by command palette or menus when disabled.
- `SearchText`: optional extra searchable text for aliases or hidden keywords.

Keep existing fields:

- `Id`: stable command id.
- `Title`: user-facing command title.
- `Kind`: execution category.
- `MenuPath`: menu placement path.
- `TargetId`: target panel id for `OpenPanel`.
- `IconKey`: visual affordance for menus and palette rows.

Add enum support:

- `WorkbenchActionScope.Global`
- `WorkbenchActionScope.FocusedPanel`

`FocusedPanel` is only metadata in this slice. It must not add focus routing behavior yet.

## Validation Rules

`WorkbenchActionRegistry.Register()` should continue rejecting invalid descriptors and add these checks:

- `Id`, `Title`, and `MenuPath` must not be empty.
- `Category` must not be empty.
- `OpenPanel` actions must still provide `TargetId`.
- Disabled actions should provide `DisabledReason`.
- Enabled actions may omit `DisabledReason`.
- Duplicate command ids remain rejected.
- `DefaultShortcut` may be empty in this slice.

Shortcut conflict detection belongs to the shortcut-router slice, not this one.

## UI Consumption

Command palette:

- Shows all catalog commands, including disabled commands.
- Filters by `Title`, `MenuPath`, `Id`, `Category`, and `SearchText`.
- Keeps disabled commands visible but non-executable.
- Displays `DisabledReason` when present.
- Displays `DefaultShortcut` when present.

Menu generation:

- Continues to build panel menu items from `OpenPanel` actions.
- Can display `DefaultShortcut` text where Avalonia menu support is already straightforward.
- Disabled metadata may be passed through to future general menu generation, but this slice does not replace the manually authored top-level menu.

Execution:

- `WorkbenchActionExecutor` should refuse disabled commands and return `false`.
- Existing enabled `OpenPanel` behavior remains unchanged.
- Disabled command result diagnostics are deferred until the command-router slice.

## First Command Set

Keep the first command set limited to the panel commands already registered by `WorkbenchFeatureModule`.

This slice must not move `ResetLayoutCommand`, `SaveLayoutCommand`, or `CommandPalette.OpenCommand` behind command ids. Those commands become catalog commands in the command-router slice, after execute-by-id result semantics exist.

## Data Flow

```text
Feature modules / shell setup
  -> WorkbenchActionRegistry.Register(descriptor)
  -> MainWindowViewModel reads registry.GetAll()
  -> CommandPaletteViewModel projects descriptors to rows
  -> Panel menu projects OpenPanel descriptors
  -> WorkbenchActionExecutor executes enabled descriptors
```

Later slices should reuse this same catalog:

```text
Command Catalog
  -> Command Router
  -> Shortcut Router
  -> Menus / Command Palette / Toolbar / Popup Buttons
  -> Notifications / Background Task Feedback
```

## Testing

Update or add unit tests for:

- Registry accepts valid metadata-rich commands.
- Registry rejects empty `Category`.
- Registry rejects disabled commands without `DisabledReason`.
- Registry keeps rejecting duplicate ids.
- Command palette searches `Category` and `SearchText`.
- Command palette exposes disabled commands but does not execute them.
- Existing panel command tests still pass.
- Main window panel menu tests still pass.

Verification commands:

```powershell
dotnet test Editor.sln -c Release
git diff --check
```

Repository-wide encoding verification should still pass before any PR:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
```

## Follow-up Slices

1. `Studio: add command router execute results`
   - Execute by command id.
   - Return structured success, disabled, failed, and unknown-command results.
   - Keep menu and palette on the same execution path.

2. `Studio: add shortcut router v1`
   - Move `Ctrl+Shift+P` out of `MainWindow.axaml.cs`.
   - Resolve default shortcuts from catalog metadata.
   - Avoid stealing shortcuts from text input.
   - Detect shortcut conflicts.

3. `Studio: improve command menu and palette`
   - Add grouping, recent commands, disabled reason display, keyboard movement, and shortcut text.

4. `Studio: add notification host v1`
   - Convert command results into non-blocking status/toast feedback.

5. `Studio: add dialog host v1`
   - Add blocking confirmation/error dialogs only after command result routing exists.

6. `Studio: add background task feedback v1`
   - Add task snapshots and progress display after commands can produce structured results.

## Acceptance Criteria

- Command metadata is centralized in `WorkbenchActionDescriptor`.
- Existing panel opening still works.
- Command palette and panel menu still use the same registered action list.
- Disabled command metadata is represented and tested without adding a full dialog or notification system.
- Shortcut strings can be stored and displayed but are not routed yet.
- No C++ engine, scene, asset, renderer, native bridge, or plugin code changes are required.
