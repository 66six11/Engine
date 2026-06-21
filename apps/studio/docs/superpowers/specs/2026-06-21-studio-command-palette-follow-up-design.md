# Studio Command Palette Follow-Up Design

## Intent

Improve the Studio command palette as a shared editor-framework command surface while keeping command execution routed through the existing workbench command catalog. This slice should make registered commands easier to scan, repeat, and diagnose without adding native engine integration, real scene data providers, plugin command APIs, or a full shortcut editor.

## Context

Current Studio command infrastructure already has:

- `WorkbenchActionDescriptor` metadata for id, title, menu path, category, default shortcut, enabled state, disabled reason, and search text.
- `WorkbenchCommandRouter` as the central command-id execution route returning `WorkbenchCommandExecutionResult`.
- `WorkbenchShortcutRouter` routing built-in shortcuts from registered action metadata.
- `CommandPaletteViewModel` filtering registered workbench actions by title, menu path, action id, category, and search text.

The current palette still presents a flat result list, does not preserve recently executed commands, and keeps failed execution results invisible to the palette surface.

## External Patterns

- VS Code's Command Palette guidance emphasizes clear command names, keyboard shortcuts where appropriate, and grouping commands under categories.
- JetBrains Search Everywhere starts from a useful default/recent list, supports action search from the same entry point, and displays action shortcuts beside results.
- Unity's shortcut documentation treats shortcut assignment, profiles, and conflict management as a dedicated editor surface, so Studio should not mix user-editable shortcut management into this palette slice.

## Goals

- Group palette results by command category while keeping the source of truth in `WorkbenchActionDescriptor.Category`.
- Promote recently executed commands when the palette opens with an empty query.
- Preserve stable filtering across title, menu path, command id, category, shortcut text, and search text.
- Surface the last command execution result in a UI-neutral palette property so the view can show failed, disabled, or missing command outcomes.
- Keep command execution on `WorkbenchCommandRouter`; the palette must not know about panels, dialogs, shortcuts, engine data, or native code.

## Non-Goals

- No plugin-contributed command API.
- No user-editable shortcuts, shortcut profile persistence, or shortcut conflict UI.
- No async command/job model.
- No toast/notification framework.
- No native ABI, runtime scene query, asset index, or Transform writeback.
- No command palette redesign beyond the minimum view bindings needed for grouped items and result feedback.

## Design

### Palette Item Projection

`CommandPaletteItemViewModel` remains the projection from `WorkbenchActionDescriptor`. It should expose enough read-only metadata for the palette view to display:

- command id
- title
- detail/menu path
- category
- icon key
- shortcut text
- enabled/disabled state
- disabled reason
- search text
- row opacity
- whether the item is a category header or an executable command

Category headers should be view-model projection objects, not workbench actions. They must not be executable, selected as commands, recorded as recent commands, or routed through `WorkbenchCommandRouter`.

### Grouped Results

`CommandPaletteViewModel` should maintain the existing flat action list internally, but publish grouped display rows for the view. Filtering still runs over executable command items only. After filtering, the palette groups visible commands by category and inserts a header row before each non-empty group.

Grouping order should be stable and deterministic:

1. Recent commands group, when the query is empty and recent commands exist.
2. Remaining groups in the registered action order, using each action's category.
3. Items within each category in registered action order, except recent commands promoted into the Recent group.

When a query is non-empty, recent promotion is disabled so search results are ranked by matching registered command order and grouped by their normal categories.

### Recent Commands

Recent command state is in-memory only for this slice. It belongs to `CommandPaletteViewModel` state and is not written to dock layout snapshots or settings files. On successful execution, the palette records the command id at the front of a bounded recent list, deduplicating existing entries. The initial cap should be small, such as five commands, to avoid turning recency into a second history system.

Only successful command executions become recent. Failed, disabled, not-found, and ignored executions should not change recent ordering.

### Execution Result Feedback

`CommandPaletteViewModel` should keep the latest `WorkbenchCommandExecutionResult` from `ExecuteSelected`. It should expose a short `LastResultMessage` and `HasLastResultMessage`.

Behavior:

- Success closes the palette and clears the result message.
- Disabled selected rows are ignored without invoking the router and may show the disabled reason through row metadata.
- Failed, disabled, or not-found router results keep the palette open and publish the result message.
- Header rows and empty selection do nothing.

This gives the command palette a local feedback seam without creating a global notification system.

### View Boundary

`CommandPaletteView.axaml` should remain a thin binding surface. It may render category headers and the last result message, but it must not perform grouping, recent ordering, command execution decisions, or command result interpretation in XAML code-behind.

## Data Flow

```text
WorkbenchFeatureModule.RegisterActions
  -> WorkbenchActionRegistry.GetAll()
  -> MainWindowViewModel
  -> CommandPaletteViewModel projects command rows
  -> Query filters executable command rows
  -> Palette inserts category headers and recent group rows
  -> ExecuteSelected routes command id to WorkbenchCommandRouter
  -> WorkbenchCommandExecutionResult updates close/message/recent state
```

## Testing

Focused tests should cover:

- opening the palette with no query groups actions by category and selects the first executable command
- successful execution records a command in the Recent group and closes the palette
- recent promotion is disabled while a non-empty query is active
- failed execution keeps the palette open and publishes the result message
- category header rows are not executable and are not selected as command items
- search still matches title, detail/menu path, id, category, shortcut text, and search text

Integration coverage should confirm the main window still opens panels through the command palette route and that shortcut/menu command routing is unchanged.

## Documentation Updates

Update `docs/Dock系统指南.md` after implementation to record:

- Command Palette follow-up now supports category grouping, in-memory recent commands, and local command result feedback.
- It still consumes `WorkbenchActionDescriptor` only.
- It still does not add plugin command APIs, full shortcut editing, native ABI, real provider data, or Transform writeback.

## References

- VS Code Command Palette UX: https://code.visualstudio.com/api/ux-guidelines/command-palette
- JetBrains Search Everywhere: https://www.jetbrains.com/help/idea/searching-everywhere.html
- Unity keyboard shortcuts: https://docs.unity3d.com/6000.4/Documentation/Manual/ShortcutsManager.html
