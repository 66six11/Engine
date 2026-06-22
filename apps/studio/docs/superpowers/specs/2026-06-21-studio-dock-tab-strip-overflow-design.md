# Studio Dock Tab Strip Overflow Design

## Intent

Improve the Studio dock tab strip when a dock window contains more tabs than its visible width can show. This slice keeps the existing custom Dock architecture and only changes editor shell behavior: tab strip overflow, scroll position, active-tab visibility, and drag hit-test stability. It does not connect native runtime data, real providers, asset indexes, Transform editing, or plugin APIs.

## Context

Current Studio Dock already has a layout graph, docked and floating workspaces, tab drag/drop, tab reorder placeholder rows, floating window creation, layout save/restore, and command-backed panel reopening. `EditorDockTabStripView` currently renders `TabStripItems` in a horizontal `StackPanel` inside an `ItemsControl`. When many tabs exist, the strip has no explicit overflow model. `EditorDockWindowView` also computes ideal tab bounds from the visual tab hosts and assumes the tab well's local coordinate system is the insertion coordinate system.

This is the next framework-level Dock slice because `docs/Dock系统指南.md` lists "tab strip overflow、自滚动和多行策略" as the first current missing item. The slice should make the one-line strip behave predictably before any future multi-row strategy, tab menu, or pinned-tab system is added.

## External Patterns

- Avalonia `ScrollViewer` requires explicit horizontal scroll configuration; the official docs show `HorizontalScrollBarVisibility="Auto"` or `Visible` for horizontally overflowing content.
- Avalonia pointer input supports pointer capture and `PointerCaptureLost`, which matches Studio's existing tab drag state machine and should remain the drag interaction backbone.
- Avalonia's `TabStrip` documentation separates tab header presentation from content display. Studio already follows that direction with `EditorDockTabStripItemViewModel` plus a separate active tab content host.
- A recent Avalonia issue about `TabStrip` layout modes calls out that scrollable tab headers are not built in and must be handled explicitly by templates or custom surfaces.
- Godot's editor docs model dock visibility, default slots, layout keys, and layout persistence as dock metadata and layout state, not as feature business logic. Studio should preserve that boundary.

## Goals

- Keep dock tab strips single-line and horizontally scrollable when tab content exceeds the available width.
- Preserve existing tab activation, close button behavior, pointer-captured drag, local reorder preview, cross-window dock drag, and floating-window drag behavior.
- Keep hit testing and reorder insertion based on ideal tab strip coordinates, including tabs that are currently outside the viewport.
- Keep active tab and reorder/drop target visible by adjusting the local tab-strip scroll position from view-only code.
- Show a clear overflow affordance without adding a full tab menu, pinned tabs, multi-row layout, or user-configurable strategy.
- Record the completed scope in `docs/Dock系统指南.md` while keeping future multi-row and advanced strategies explicit.

## Non-Goals

- No native ABI, runtime scene query, asset index, real provider connection, or Transform writeback.
- No plugin dock API, plugin tab API, or command extension API.
- No multi-row tab strip.
- No pinned tabs, preview tabs, tab context menu, hidden-tab menu, or tab search.
- No persistent scroll position in dock layout snapshots.
- No change to `EditorDockWindowViewModel.Tabs` persistence shape.
- No replacement of the custom Dock with Avalonia `TabStrip`, `TabControl`, Dock.Avalonia, or another third-party control.

## Design

### Scroll Host

`EditorDockTabStripView` should wrap the existing `ItemsControl` in a horizontal `ScrollViewer`. The visual row remains a single row with the same `StackPanel` `ItemsPanelTemplate`. The scroll host is part of the view, not the view model, because scroll offset is transient UI state and should not be serialized with the layout graph.

The expected XAML shape is:

```xml
<ScrollViewer x:Name="DockTabStripScrollViewer"
              HorizontalScrollBarVisibility="Hidden"
              VerticalScrollBarVisibility="Disabled">
    <ItemsControl x:Name="DockTabStripItems"
                  Classes="owned-dock-tabs owned-dock-tab-well"
                  IsVisible="{Binding HasTabs}"
                  ItemsSource="{Binding TabStripItems}">
        ...
    </ItemsControl>
</ScrollViewer>
```

`Hidden` is intentional: the strip remains scrollable but does not consume extra vertical tab chrome height with a visible scrollbar. A future slice can add explicit scroll buttons if screenshots or manual testing show wheel/trackpad access is insufficient.

### Overflow Affordance

The first implementation should expose small left/right edge affordances when the scroll offset is not at the corresponding edge. The affordances can be non-interactive gradient or border overlays in `EditorDockTabStripView`; they must not block tab hit testing. They should use existing color tokens and should not add text labels.

The view should expose no new public editor state for these affordances. If code-behind needs to respond to `ScrollViewer.Offset`, `Extent`, or `Viewport`, it can toggle local visual properties/classes on view controls only.

### Active Tab Visibility

When `EditorDockWindowViewModel.ActiveTab` changes or `TabStripItems` changes, `EditorDockTabStripView` should bring the active tab host into view. This is view-only behavior and should use `BringIntoView()` or a small helper that adjusts the local `ScrollViewer.Offset`. It must not change active tab, dock layout, or tab ordering.

### Drag And Reorder Behavior

`EditorDockWindowView` already captures pointers and defers drag start until movement passes `TabDragStartThreshold`. This remains unchanged. The overflow slice should only adjust coordinate handling around a scrolled tab strip:

- `GetIdealTabBounds` and local reorder entries should continue to describe the full logical tab row, not only the visible viewport.
- The tab strip's scroll offset must be accounted for when translating between the visual scroll viewport and the logical tab row.
- `InsertTabAtIndex` previews should be able to target the logical first and last positions even when those tabs are outside the visible area.
- When a local reorder preview moves near the left or right viewport edge, the scroll host should auto-scroll by a small fixed step. This should happen only while a pointer-captured tab interaction is active and only in the view layer.

If auto-scroll becomes unreliable under headless tests, it can be guarded by deterministic helper methods for offset calculation and verified with unit tests plus a manual checklist. The production behavior should still be present.

### Error And Boundary Handling

The scroll helpers should tolerate missing visual children, zero-size bounds, and detached views by doing nothing. No exception should be thrown when a tab is closed, moved to another workspace, or when a floating host closes during a drag.

### Documentation Boundary

After implementation, `docs/Dock系统指南.md` should move single-line horizontal overflow and edge auto-scroll from "current missing" into "current implemented", while leaving multi-row strategy, hidden-tab menus, pinned tabs, and full advanced tab management as future work.

## Data Flow

```text
EditorDockWindowViewModel.TabStripItems
  -> EditorDockTabStripView ItemsControl
  -> ScrollViewer hosts one-line overflow viewport
  -> EditorDockWindowView captures tab pointer interaction
  -> visual tab bounds + scroll offset produce logical tab row bounds
  -> EditorDockHitTestService / EditorDockTabReorderResolver keep using logical tab positions
  -> view-only scroll helpers bring active or drag target tabs into view
```

## Testing

Focused tests should cover:

- tab strip overflow geometry helper clamps offsets to the valid horizontal range
- active tab visibility helper computes the minimal offset needed to show a tab fully when possible
- near-edge drag auto-scroll helper moves left or right only while overflow exists
- logical tab bounds remain stable when the scroll viewport has a non-zero horizontal offset
- existing `EditorDockTabReorderResolverTests`, `EditorDockHitTestServiceTests`, and `EditorDockWindowViewModelTests` still pass

The plan should prefer deterministic helper tests for geometry math and only rely on Avalonia view tests where the existing test harness already supports them. Final validation should include focused Dock tests, the full Release solution test suite, `git diff --check`, and the text encoding gate.

## References

- Avalonia ScrollViewer horizontal scrolling: https://docs.avaloniaui.net/controls/layout/containers/scrollviewer
- Avalonia input events and pointer capture: https://docs.avaloniaui.net/docs/events/input-events
- Avalonia TabStrip responsibilities: https://docs.avaloniaui.net/controls/navigation/tabstrip
- Avalonia TabStrip scrollable layout discussion: https://github.com/AvaloniaUI/Avalonia/issues/20570
- Godot EditorDock layout and visibility model: https://docs.godotengine.org/en/stable/classes/class_editordock.html
- Godot editor layout customization: https://docs.godotengine.org/en/latest/tutorials/editor/customizing_editor.html
