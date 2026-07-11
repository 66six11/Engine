using Asharia.Editor.Panels;

namespace Editor.Core.Abstractions;

public interface IEditorPanelLifecycleSink
{
    void OnPanelAttached(EditorPanelLifecycleContext context);

    void OnPanelActivated(EditorPanelLifecycleContext context);

    void OnPanelDeactivated(EditorPanelLifecycleContext context);

    void OnPanelDetached(EditorPanelLifecycleContext context);
}
