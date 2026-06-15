using Avalonia;

namespace Editor.Shell.ViewModels;

public sealed record EditorDockFloatingWindowRequest(
    EditorDockFloatingWindowViewModel Window,
    Rect Bounds);
