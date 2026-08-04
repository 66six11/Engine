using Avalonia;

namespace Editor.Shell.Docking.TabStrips;

public readonly record struct EditorDockTabBounds(
    string TabId,
    int TabIndex,
    Rect Bounds,
    bool IsDragSource);
