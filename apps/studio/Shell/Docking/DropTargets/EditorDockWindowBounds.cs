using System.Collections.Generic;
using Editor.Shell.Docking.Panels;
using Avalonia;
using Editor.Shell.Docking.TabStrips;

namespace Editor.Shell.Docking.DropTargets;

public readonly record struct EditorDockWindowBounds(
    string WindowId,
    EditorDockArea Area,
    Rect Bounds,
    Rect TabWellBounds,
    int TabCount,
    IReadOnlyList<EditorDockTabBounds> TabBounds,
    int? DragSourceTabIndex,
    bool AllowsWindowInsertion,
    bool IsDragSource,
    double TabContentOriginX = double.NaN);
