# Studio Command Menu Catalog Implementation Plan

Issue: [#188](https://github.com/66six11/Engine/issues/188)
Spec: `docs/superpowers/specs/2026-06-20-studio-command-menu-catalog-design.md`
Branch: `codex/studio-command-menu-catalog`

## Outcome

`Tools > Command Palette` is generated from the workbench action catalog and executes through `WorkbenchCommandRouter.Execute(commandId)`. Existing `Window > Panels` behavior remains intact, including open-state indicators.

## Scope

- Add a small catalog-backed menu item view model for non-panel command menu entries.
- Expose generated Tools menu entries from `MainWindowViewModel`.
- Rebuild the Tools menu in `MainWindow.axaml.cs` from generated items.
- Keep File/Edit/View/Help placeholders static.
- Keep Window/Panels on `PanelMenuItemViewModel` so panel open state remains explicit.

## TDD Checklist

1. Add `WorkbenchMenuItemViewModel` tests.
   - It exposes command id, header, menu path, icon key, shortcut text, enabled state, and disabled reason from `WorkbenchActionDescriptor`.
   - Enabled items execute the supplied command dispatcher with the action id.
   - Disabled items report `CanExecute == false` and do not dispatch.

2. Add `MainWindowViewModel` tests.
   - `ToolsMenuItems` follows catalog actions under `Tools/`.
   - Executing the generated Command Palette item opens the command palette through the command route.
   - `PanelMenuItems` expectations remain unchanged.

3. Implement the menu model.
   - Add `Shell/ViewModels/WorkbenchMenuItemViewModel.cs`.
   - Validate descriptor and dispatcher arguments.
   - Store immutable descriptor metadata.
   - Use `RelayCommand` with a disabled guard.

4. Implement the projection in `MainWindowViewModel`.
   - Add `ToolsMenuItems`.
   - Filter actions by `MenuPath.StartsWith("Tools/", StringComparison.Ordinal)`.
   - Preserve action registration order.
   - Route clicks through the existing `WorkbenchCommandRouter`.

5. Wire Avalonia menu generation.
   - Name the Tools top-level menu in `MainWindow.axaml`.
   - Remove the static Command Palette child.
   - Rebuild generated Tools items in `MainWindow.axaml.cs` when the data context changes.
   - Keep the disabled Settings placeholder.
   - Render shortcut text in an aligned trailing column when present.

6. Validate.
   - Run focused tests for menu, main-window view model, and shortcut behavior.
   - Run the full Studio test suite in Release.
   - Run the default Studio test suite.
   - Run text encoding check.
   - Run `git diff --check`.

## Files Expected To Change

- `apps/studio/Shell/ViewModels/WorkbenchMenuItemViewModel.cs`
- `apps/studio/Shell/ViewModels/MainWindowViewModel.cs`
- `apps/studio/Shell/Views/MainWindow.axaml`
- `apps/studio/Shell/Views/MainWindow.axaml.cs`
- `apps/studio/Tests/Editor.Tests/Shell/ViewModels/WorkbenchMenuItemViewModelTests.cs`
- `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`

## Notes

The slice intentionally does not add nested menu generation, persistence, command customization, popup feedback, background loading, or scene/runtime work. Those are follow-up editor UI foundations once the shared command surfaces are stable.
