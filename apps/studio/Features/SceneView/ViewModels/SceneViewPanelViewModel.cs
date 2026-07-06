using System;
using Editor.Core.Abstractions;
using Editor.Core.Models.Selection;
using Editor.Core.Models.Viewports;
using Editor.UI.ViewModels;

namespace Editor.Features.SceneView.ViewModels;

public sealed class SceneViewPanelViewModel : ViewModelBase
{
    private const string SelectionContextId = "scene-view";
    private static readonly ViewportId DefaultViewportId = new("scene-view/main");
    private readonly IEditorSelectionService selectionService_;

    public SceneViewPanelViewModel(IEditorSelectionService selectionService)
    {
        selectionService_ = selectionService;
    }

    public string ViewportStateTitle => "Viewport backend deferred";

    public ViewportId ViewportId => DefaultViewportId;

    public ViewportCompositionCapabilitiesSnapshot? CompositionCapabilities { get; private set; }

    public string ViewportStateMessage =>
        CompositionCapabilities is null
            ? "Scene View is waiting for Avalonia composition GPU interop probing."
            : CompositionCapabilities.Message;

    public string ViewportStatusText => CompositionCapabilities?.Status.ToString() ?? "composition pending";

    public void UpdateCompositionCapabilities(ViewportCompositionCapabilitiesSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (snapshot.ViewportId != ViewportId)
        {
            throw new ArgumentException(
                "Composition capability snapshot must match the Scene View viewport.",
                nameof(snapshot));
        }

        CompositionCapabilities = snapshot;
        OnPropertyChanged(nameof(CompositionCapabilities));
        OnPropertyChanged(nameof(ViewportStateMessage));
        OnPropertyChanged(nameof(ViewportStatusText));
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
