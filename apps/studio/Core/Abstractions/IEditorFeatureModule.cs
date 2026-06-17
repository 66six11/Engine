namespace Editor.Core.Abstractions;

public interface IEditorFeatureModule
{
    void RegisterPanels(IPanelRegistry panels);
}
