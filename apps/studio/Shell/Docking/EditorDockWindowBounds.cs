using System.Collections.Generic;
using Avalonia;
using Editor.Core.Models;

namespace Editor.Shell.Docking;

public readonly record struct EditorDockWindowBounds(
    string WindowId,
    DockArea Area,
    Rect Bounds,
    Rect TabWellBounds,
    int TabCount,
    IReadOnlyList<EditorDockTabBounds> TabBounds,
    int? DragSourceTabIndex,
    bool AllowsWindowInsertion,
    bool IsDragSource);
