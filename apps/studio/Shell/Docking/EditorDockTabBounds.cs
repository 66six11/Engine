using Avalonia;

namespace Editor.Shell.Docking;

public readonly record struct EditorDockTabBounds(
    string TabId,
    int TabIndex,
    Rect Bounds,
    bool IsDragSource);
