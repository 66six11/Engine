using System.Collections.Generic;
using Avalonia;
using Editor.Core.Models;

namespace Editor.Shell.Docking;

public readonly record struct EditorDockWindowBounds(
    string WindowId,
    DockArea Area,
    Rect Bounds,
    Rect TabWellBounds,
    IReadOnlyList<EditorDockTabBounds> TabBounds,
    bool AllowsWindowInsertion,
    bool IsDragSource);
