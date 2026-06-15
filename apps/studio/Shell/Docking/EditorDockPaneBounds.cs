using Avalonia;
using Editor.Core.Models;

namespace Editor.Shell.Docking;

public readonly record struct EditorDockPaneBounds(
    string PaneId,
    DockArea Area,
    Rect Bounds);
