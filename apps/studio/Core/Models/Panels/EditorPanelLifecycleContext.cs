namespace Editor.Core.Models.Panels;

public sealed record EditorPanelLifecycleContext(
    string PanelId,
    string Title,
    DockArea DockArea,
    bool IsFloatingWorkspace)
{
    public bool IsMainWorkspace => !IsFloatingWorkspace;
}
