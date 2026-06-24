using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface IEditorPanelFrameUpdateSink
{
    EditorPanelFrameUpdateRequest FrameUpdateRequest { get; }

    void OnEditorPanelFrame(EditorPanelFrameContext context);
}
