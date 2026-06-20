# Studio Command Menu Catalog Design

## Context

Issue: [#188](https://github.com/66six11/Engine/issues/188)
Branch: `codex/studio-command-menu-catalog`

The Studio editor now has three command foundations:

- #182 / PR #183: workbench action catalog metadata.
- #184 / PR #185: command-id execution and typed execution results.
- #186 / PR #187: built-in shortcut routing through command ids.

The main menu is the remaining command surface that still mixes static XAML and custom code-behind. `Window > Panels` is generated from panel actions, but `Tools > Command Palette` is still a direct binding to `CommandPalette.OpenCommand`. This slice moves command-backed menu items onto the command catalog so menu clicks, shortcuts, and the command palette use the same command ids.

## Goals

- Project catalog actions into command menu item view models.
- Execute command menu clicks through `WorkbenchCommandRouter.Execute(commandId)`.
- Move `Tools > Command Palette` onto the catalog-backed route.
- Preserve `Window > Panels` dynamic behavior and open-state indicators.
- Keep static placeholder menus such as File/Edit unchanged in this slice.

## Non-goals

- No full main menu redesign.
- No user-editable menus.
- No shortcut settings or persistence.
- No command palette redesign.
- No popup, dialog, toast, or notification host.
- No background task service or async job UI.
- No undo/redo transaction framework.
- No scene runtime, native ABI, renderer, asset, or C++ changes.

## Current State

- `MainWindow.axaml` declares top-level `File`, `Edit`, `View`, `Window`, `Tools`, and `Help` menus.
- `Tools > Command Palette` is a static XAML menu item bound directly to `CommandPalette.OpenCommand`.
- `Window > Panels` is a named submenu populated in `MainWindow.axaml.cs` from `PanelMenuItems`.
- `PanelMenuItemViewModel` already captures action metadata and executes through command id.
- `WorkbenchActionDescriptor.MenuPath` provides paths such as `Tools/Command Palette` and `Window/Panels/Hierarchy`.

## Recommended Approach

Use a small menu projection layer rather than rebuilding the whole Avalonia menu system.

Create a command-menu view model under `Editor.Shell.ViewModels` that represents only catalog-backed menu entries:

- `WorkbenchMenuItemViewModel`
  - `CommandId`
  - `Header`
  - `MenuPath`
  - `IconKey`
  - `ShortcutText`
  - `IsEnabled`
  - `DisabledReason`
  - `OpenCommand`

Create a projection helper under `Editor.Shell.Commands`:

- `WorkbenchMenuProjection`
  - filters catalog actions by menu path prefix.
  - preserves action registration order.
  - creates item view models that execute by command id.

The first implementation should only project:

- `Tools/*`
- `Window/Panels/*`

Static `File`, `Edit`, `View`, and `Help` placeholder entries stay in XAML. This avoids pretending unfinished actions like save/undo/settings are real command catalog entries.

## Menu Wiring

`MainWindowViewModel` will expose:

- `ToolsMenuItems`
- `PanelMenuItems` remains available, or is replaced by a compatible catalog-backed projection that still exposes panel id/open state.

`MainWindow.axaml.cs` should keep its current role as an Avalonia view bridge:

- clear and rebuild named menu item collections when `DataContext` changes.
- bind each generated menu item to an item view model.
- keep the panel open-state indicator for `Window > Panels`.

`MainWindow.axaml` should remove the static `Tools > Command Palette` item and replace it with a named empty `ToolsMenu` submenu that code-behind populates from `ToolsMenuItems`.

## Error Handling

Menu clicks do not throw or show UI feedback in this slice. If `WorkbenchCommandRouter.Execute` returns a non-success result, the command has still completed from the menu surface's perspective. Popup/toast feedback belongs to a later slice and will consume the same typed result model.

Disabled catalog actions should render disabled in the menu and should not execute from the view model. The router still guards disabled commands as a second line of protection.

## Alternatives Considered

### Keep Static XAML

This is cheapest, but it keeps the menu as a separate command path. Shortcuts and command palette would use command ids while menu clicks bypass them.

### Generate The Entire Main Menu

This is eventually attractive, but it would force placeholder items such as Save, Undo, Settings, and Documentation into the command catalog before they have real command behavior.

### Recommended: Generate Only Catalog-Backed Command Submenus

This keeps the slice small and makes the real command-backed surfaces consistent without expanding scope into unfinished editor systems.

## Acceptance Criteria

- `Tools > Command Palette` is represented by a catalog-backed menu item.
- Clicking the generated command palette menu item opens the command palette through `WorkbenchCommandRouter`.
- `Window > Panels` remains generated from catalog panel actions and preserves open-state indicators.
- Disabled catalog menu items are disabled in the menu model and do not execute.
- Existing command palette, shortcut routing, and panel-opening tests continue to pass.

## Validation

- `dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchMenu|FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~MainWindowShortcutTests"`
- `dotnet test apps/studio/Editor.sln -c Release`
- `dotnet test apps/studio/Editor.sln`
- `powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1`
- `git diff --check`
