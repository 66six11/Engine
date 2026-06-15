using Avalonia;
using Avalonia.Layout;

namespace Editor.Shell.Docking;

public readonly record struct EditorDockSplitterBounds(
    string SplitterId,
    Orientation Orientation,
    Rect Bounds);
