using Asharia.Editor.Panels;
using Editor.Core.Models.Panels;

namespace Editor.Core.Abstractions;

public interface IEditorPanelFrameUpdateSink
{
    EditorPanelFrameUpdateRequest FrameUpdateRequest { get; }

    void OnEditorPanelFrame(EditorPanelFrameContext context);
}
