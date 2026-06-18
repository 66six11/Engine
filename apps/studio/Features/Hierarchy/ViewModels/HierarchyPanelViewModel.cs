using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.ViewModels;

namespace Editor.Features.Hierarchy.ViewModels;

public sealed class HierarchyPanelViewModel : ViewModelBase
{
    private const string SelectionContextId = "hierarchy";
    private readonly IEditorSelectionService selectionService_;

    public HierarchyPanelViewModel(IEditorSelectionService selectionService)
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
