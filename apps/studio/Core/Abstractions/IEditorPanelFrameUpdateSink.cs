using Asharia.Editor.Panels;

namespace Editor.Core.Abstractions;

public interface IEditorPanelFrameUpdateSink
{
    EditorPanelFrameUpdateRequest FrameUpdateRequest { get; }

    void OnEditorPanelFrame(EditorPanelFrameContext context);
}
