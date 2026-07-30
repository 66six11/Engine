using System;
using Editor.Core.Abstractions;
using Asharia.Editor.Diagnostics;
using Asharia.Editor.Selection;
using Asharia.Editor.Viewports;
using Asharia.Editor.Worlds.Snapshots;
using Editor.Core.Models.Viewports;
using Editor.Core.Services;
using Editor.UI.ViewModels;

namespace Editor.Features.SceneView.ViewModels;

public sealed class SceneViewPanelViewModel : ViewModelBase, IDisposable
{
    private const string SelectionContextId = "scene-view";
    private const string DiagnosticSource = "scene-view";
    private const string NativeViewportDiagnosticCategory = "native-viewport";
    private static readonly ViewportId DefaultViewportId = new("scene-view/main");
    private readonly IEditorSelectionService selectionService_;
    private readonly IEditorDiagnosticService? diagnostics_;
    private readonly ISceneSnapshotProvider sceneSnapshots_;
    private readonly IEditorUiDispatcher uiDispatcher_;
    private NativePresentDiagnosticKey? lastPublishedNativePresentDiagnostic_;
    private bool isDisposed_;

    public SceneViewPanelViewModel(
        IEditorSelectionService selectionService,
        IEditorDiagnosticService? diagnostics = null,
        ISceneSnapshotProvider? sceneSnapshots = null)
        : this(
            selectionService,
            diagnostics,
            sceneSnapshots,
            new ImmediateEditorUiDispatcher())
    {
    }

    internal SceneViewPanelViewModel(
        IEditorSelectionService selectionService,
        IEditorDiagnosticService? diagnostics,
        ISceneSnapshotProvider? sceneSnapshots,
        IEditorUiDispatcher uiDispatcher)
    {
        ArgumentNullException.ThrowIfNull(selectionService);
        ArgumentNullException.ThrowIfNull(uiDispatcher);

        selectionService_ = selectionService;
        diagnostics_ = diagnostics;
        sceneSnapshots_ = sceneSnapshots
            ?? new InMemorySceneSnapshotProvider(SceneSnapshot.Empty);
        uiDispatcher_ = uiDispatcher;
        sceneSnapshots_.SnapshotChanged += OnSceneSnapshotChanged;
    }

    public ViewportId ViewportId => DefaultViewportId;

    public ViewportCompositionCapabilitiesSnapshot? CompositionCapabilities { get; private set; }

    public ViewportNativePresentSnapshot? NativePresent { get; private set; }

    public event EventHandler? RenderRequested;

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

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        sceneSnapshots_.SnapshotChanged -= OnSceneSnapshotChanged;
    }

    internal (bool HasScene, ulong Revision) GetSceneRenderState()
    {
        var snapshot = sceneSnapshots_.GetCurrentSnapshot();
        return (
            snapshot.Revision > 0 || snapshot.Objects.Count > 0,
            checked((ulong)snapshot.Revision));
    }

    private void OnSceneSnapshotChanged(object? sender, EventArgs e)
    {
        if (uiDispatcher_.CheckAccess())
        {
            RequestRender();
            return;
        }

        uiDispatcher_.Post(RequestRender);
    }

    private void RequestRender()
    {
        if (!isDisposed_)
        {
            RenderRequested?.Invoke(this, EventArgs.Empty);
        }
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
