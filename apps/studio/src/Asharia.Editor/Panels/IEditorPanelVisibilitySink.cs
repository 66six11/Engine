namespace Asharia.Editor.Panels;

public interface IEditorPanelVisibilitySink
{
    void OnPanelShown(EditorPanelLifecycleContext context);

    void OnPanelHidden(EditorPanelLifecycleContext context);
}
