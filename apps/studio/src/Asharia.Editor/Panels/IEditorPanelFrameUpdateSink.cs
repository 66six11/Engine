namespace Asharia.Editor.Panels;

public interface IEditorPanelFrameUpdateSink
{
    EditorPanelFrameUpdateRequest FrameUpdateRequest { get; }

    void OnEditorPanelFrame(EditorPanelFrameContext context);
}
