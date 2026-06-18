using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.ViewModels;

namespace Editor.Features.SceneView.ViewModels;

public sealed class SceneViewPanelViewModel : ViewModelBase
{
    private const string SelectionContextId = "scene-view";
    private readonly IEditorSelectionService selectionService_;

    public SceneViewPanelViewModel(IEditorSelectionService selectionService)
    {
        selectionService_ = selectionService;
    }

    public void SelectItem(EditorSelectionItem item)
    {
        selectionService_.ReplaceSelection(SelectionContextId, [item]);
    }

    public void ClearSelection()
    {
        selectionService_.ClearSelection(SelectionContextId);
    }
}
