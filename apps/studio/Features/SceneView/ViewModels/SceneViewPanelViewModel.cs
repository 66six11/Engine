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

    public ViewportNativePresentSnapshot? NativePresent { get; private set; }

    public string ViewportStateMessage =>
        NativePresent is not null
            ? NativePresent.Message
            : CompositionCapabilities is null
            ? "Scene View is waiting for Avalonia composition GPU interop probing."
            : CompositionCapabilities.Message;

    public string ViewportStatusText =>
        NativePresent?.Status.ToString() ?? CompositionCapabilities?.Status.ToString() ?? "composition pending";

    public void UpdateCompositionCapabilities(ViewportCompositionCapabilitiesSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (snapshot.ViewportId != ViewportId)
        {
            throw new ArgumentException(
                "Composition capability snapshot must match the Scene View viewport.",
                nameof(snapshot));
        }

        var hadNativePresent = NativePresent is not null;
        CompositionCapabilities = snapshot;
        NativePresent = null;
        OnPropertyChanged(nameof(CompositionCapabilities));
        if (hadNativePresent)
        {
            OnPropertyChanged(nameof(NativePresent));
        }

        OnPropertyChanged(nameof(ViewportStateMessage));
        OnPropertyChanged(nameof(ViewportStatusText));
    }

    public void UpdateNativePresent(ViewportNativePresentSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (snapshot.ViewportId != ViewportId)
        {
            throw new ArgumentException(
                "Native present snapshot must match the Scene View viewport.",
                nameof(snapshot));
        }

        NativePresent = snapshot;
        OnPropertyChanged(nameof(NativePresent));
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
