using Asharia.Editor.Panels;
using Asharia.Editor.UI.CodeFirst.Authoring;

namespace Asharia.Editor.UI.CodeFirst.Abstractions;

public interface ICodeFirstEditorPanelHost
{
    EditorPanelFrameUpdateRequest FrameUpdateRequest { get; }

    void Create(EditorPanelLifecycleContext context);

    void Enable();

    void BuildGui(EditorGui gui);

    void Frame(EditorPanelFrameContext context);

    void Disable();

    void Destroy();
}
