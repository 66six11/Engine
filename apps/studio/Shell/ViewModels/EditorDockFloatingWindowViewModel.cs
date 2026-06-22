using Editor.Core.Abstractions;

namespace Editor.Shell.ViewModels;

public sealed class EditorDockFloatingWindowViewModel : ViewModelBase
{
    public EditorDockFloatingWindowViewModel(
        EditorDockWorkspaceViewModel dockWorkspace,
        IEditorLifecycleEventService? lifecycleEvents = null)
    {
        DockWorkspace = dockWorkspace;
        LifecycleEvents = lifecycleEvents ?? dockWorkspace.LifecycleEvents;
    }

    public EditorDockWorkspaceViewModel DockWorkspace { get; }

    public IEditorLifecycleEventService LifecycleEvents { get; }
}
