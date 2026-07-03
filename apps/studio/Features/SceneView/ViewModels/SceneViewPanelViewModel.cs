using Editor.Core.Abstractions;
using Editor.Core.Models.Selection;
using Editor.UI.ViewModels;

namespace Editor.Features.SceneView.ViewModels;

public sealed class SceneViewPanelViewModel : ViewModelBase
{
    private const string SelectionContextId = "scene-view";
    private readonly IEditorSelectionService selectionService_;

    public SceneViewPanelViewModel(IEditorSelectionService selectionService)
    {
        selectionService_ = selectionService;
    }

    public string ViewportStateTitle => "Viewport backend deferred";

    public string ViewportStateMessage =>
        "Scene snapshot and selection shell are available; native Vulkan viewport is a separate integration slice.";

    public string ViewportStatusText => "deferred";

    public void SelectItem(EditorSelectionItem item)
    {
        selectionService_.ReplaceSelection(SelectionContextId, [item]);
    }

    public void ClearSelection()
    {
        selectionService_.ClearSelection(SelectionContextId);
    }
}
