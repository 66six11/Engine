namespace Editor.Core.Models;

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
