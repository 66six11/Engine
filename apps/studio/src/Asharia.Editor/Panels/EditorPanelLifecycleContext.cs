namespace Asharia.Editor.Panels;

public sealed record EditorPanelLifecycleContext(
    string PanelId,
    string Title,
    EditorDockArea EditorDockArea,
    bool IsFloatingWorkspace)
{
    public bool IsMainWorkspace => !IsFloatingWorkspace;
}
