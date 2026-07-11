using Asharia.Editor.Panels;
using Asharia.Editor.UI.CodeFirst.Authoring;

namespace Asharia.Editor.UI.CodeFirst.Abstractions;

public abstract class CodeFirstEditorPanel : ICodeFirstEditorPanelHost
{
    public virtual EditorPanelFrameUpdateRequest FrameUpdateRequest =>
        EditorPanelFrameUpdateRequest.Manual;

    EditorPanelFrameUpdateRequest ICodeFirstEditorPanelHost.FrameUpdateRequest =>
        FrameUpdateRequest;

    void ICodeFirstEditorPanelHost.Create(EditorPanelLifecycleContext context) => OnCreate(context);

    void ICodeFirstEditorPanelHost.Enable() => OnEnable();

    void ICodeFirstEditorPanelHost.BuildGui(EditorGui gui) => OnGui(gui);

    void ICodeFirstEditorPanelHost.Frame(EditorPanelFrameContext context) => OnFrame(context);

    void ICodeFirstEditorPanelHost.Disable() => OnDisable();

    void ICodeFirstEditorPanelHost.Destroy() => OnDestroy();

    protected virtual void OnCreate(EditorPanelLifecycleContext context)
    {
    }

    protected virtual void OnEnable()
    {
    }

    protected abstract void OnGui(EditorGui gui);

    protected virtual void OnFrame(EditorPanelFrameContext context)
    {
    }

    protected virtual void OnDisable()
    {
    }

    protected virtual void OnDestroy()
    {
    }
}
