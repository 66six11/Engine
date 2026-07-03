namespace Editor.Core.Models.Lifecycle;

public enum EditorLifecycleEventKind
{
    ApplicationOpened,
    ApplicationClosing,
    ApplicationClosed,
    HostActivated,
    HostDeactivated,
    WorkspaceRestored,
    FloatingWindowOpened,
    FloatingWindowClosed,
    FloatingWindowActivated,
    FloatingWindowDeactivated,
}
