using Avalonia;

namespace Editor.Shell.ViewModels.Docking;

public sealed record EditorDockFloatingWindowRequest(
    EditorDockFloatingWindowViewModel Window,
    Rect Bounds);
