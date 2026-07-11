using System;
using Editor.Core.Abstractions;
using Asharia.Editor.Diagnostics;
using Editor.Core.Models.Diagnostics;
using Editor.Core.Models.Panels;
using Editor.Core.Models.Selection;
using Editor.Core.Models.Viewports;
using Editor.UI.ViewModels;

namespace Editor.Features.SceneView.ViewModels;

public sealed class SceneViewPanelViewModel : ViewModelBase, IEditorPanelFrameUpdateSink
{
    private const string SelectionContextId = "scene-view";
    private const string DiagnosticSource = "scene-view";
    private const string NativeViewportDiagnosticCategory = "native-viewport";
    private static readonly ViewportId DefaultViewportId = new("scene-view/main");
    private readonly IEditorSelectionService selectionService_;
    private readonly IEditorDiagnosticService? diagnostics_;
    private NativePresentDiagnosticKey? lastPublishedNativePresentDiagnostic_;

    public SceneViewPanelViewModel(
        IEditorSelectionService selectionService,
        IEditorDiagnosticService? diagnostics = null)
    {
        ArgumentNullException.ThrowIfNull(selectionService);

        selectionService_ = selectionService;
        diagnostics_ = diagnostics;
    }

    public string ViewportStateTitle => "Viewport backend deferred";

    public ViewportId ViewportId => DefaultViewportId;

    public ViewportCompositionCapabilitiesSnapshot? CompositionCapabilities { get; private set; }

    public ViewportNativePresentSnapshot? NativePresent { get; private set; }

    public EditorPanelFrameUpdateRequest FrameUpdateRequest { get; } =
        EditorPanelFrameUpdateRequest.Active(targetFramesPerSecond: 30d);

    public event EventHandler<EditorPanelFrameContext>? FrameRequested;

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
        lastPublishedNativePresentDiagnostic_ = null;
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
        PublishNativePresentDiagnosticIfNeeded(snapshot);
    }

    public void SelectItem(EditorSelectionItem item)
    {
        selectionService_.ReplaceSelection(SelectionContextId, [item]);
    }

    public void ClearSelection()
    {
        selectionService_.ClearSelection(SelectionContextId);
    }

    public void OnEditorPanelFrame(EditorPanelFrameContext context)
    {
        ArgumentNullException.ThrowIfNull(context);

        FrameRequested?.Invoke(this, context);
    }

    private void PublishNativePresentDiagnosticIfNeeded(ViewportNativePresentSnapshot snapshot)
    {
        if (snapshot.Status == ViewportNativePresentStatus.Success)
        {
            lastPublishedNativePresentDiagnostic_ = null;
            return;
        }

        if (diagnostics_ is null)
        {
            return;
        }

        var key = new NativePresentDiagnosticKey(snapshot.Status, snapshot.Message);
        if (lastPublishedNativePresentDiagnostic_ == key)
        {
            return;
        }

        diagnostics_.Publish(
            MapNativePresentDiagnosticSeverity(snapshot.Status),
            EditorDiagnosticChannel.Problem,
            DiagnosticSource,
            NativeViewportDiagnosticCategory,
            snapshot.Message);
        lastPublishedNativePresentDiagnostic_ = key;
    }

    private static EditorDiagnosticSeverity MapNativePresentDiagnosticSeverity(ViewportNativePresentStatus status)
    {
        return status switch
        {
            ViewportNativePresentStatus.DeviceLost => EditorDiagnosticSeverity.Error,
            ViewportNativePresentStatus.ImportFailed => EditorDiagnosticSeverity.Error,
            ViewportNativePresentStatus.RenderFailed => EditorDiagnosticSeverity.Error,
            _ => EditorDiagnosticSeverity.Warning,
        };
    }

    private readonly record struct NativePresentDiagnosticKey(
        ViewportNativePresentStatus Status,
        string Message);
}
