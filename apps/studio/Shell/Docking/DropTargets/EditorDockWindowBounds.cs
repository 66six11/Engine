using System.Collections.Generic;
using Asharia.Editor.Panels;
using Avalonia;
using Editor.Core.Models.Panels;
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
