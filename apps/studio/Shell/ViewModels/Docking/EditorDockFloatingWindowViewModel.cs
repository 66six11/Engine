using System;
using Editor.Shell.Docking.Lifecycle;
using Editor.Shell.ViewModels;

namespace Editor.Shell.ViewModels.Docking;

public sealed class EditorDockFloatingWindowViewModel : ViewModelBase, IDisposable
{
    private bool isDisposed_;

    public EditorDockFloatingWindowViewModel(
        EditorDockWorkspaceViewModel dockWorkspace,
        IEditorLifecycleEventService? lifecycleEvents = null)
    {
        DockWorkspace = dockWorkspace;
        LifecycleEvents = lifecycleEvents ?? dockWorkspace.LifecycleEvents;
    }

    public EditorDockWorkspaceViewModel DockWorkspace { get; }

    public IEditorLifecycleEventService LifecycleEvents { get; }

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        DockWorkspace.Dispose();
    }
}
