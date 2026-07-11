using Asharia.Editor.Panels;
using Editor.Core.Models.Panels;
using Editor.Core.CodeFirstUI.Authoring;

namespace Editor.Core.CodeFirstUI.Abstractions;

public abstract class CodeFirstEditorPanel
{
    public virtual EditorPanelFrameUpdateRequest FrameUpdateRequest =>
        EditorPanelFrameUpdateRequest.Manual;

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

    internal void DispatchCreate(EditorPanelLifecycleContext context)
    {
        OnCreate(context);
    }

    internal void DispatchEnable()
    {
        OnEnable();
    }

    internal void DispatchGui(EditorGui gui)
    {
        OnGui(gui);
    }

    internal void DispatchFrame(EditorPanelFrameContext context)
    {
        OnFrame(context);
    }

    internal void DispatchDisable()
    {
        OnDisable();
    }

    internal void DispatchDestroy()
    {
        OnDestroy();
    }
}
