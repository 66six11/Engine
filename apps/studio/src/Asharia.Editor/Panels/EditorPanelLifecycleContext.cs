namespace Asharia.Editor.Panels;

public sealed record EditorPanelLifecycleContext(
    string PanelId,
    string Title,
    EditorDockArea DockArea,
    bool IsFloatingWorkspace)
{
    public bool IsMainWorkspace => !IsFloatingWorkspace;
}
