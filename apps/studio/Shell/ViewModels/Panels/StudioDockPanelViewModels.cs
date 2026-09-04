using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Threading;
using Editor.Shell.ViewModels.Windowing;

namespace Editor.Shell.ViewModels.Panels;

internal abstract class StudioDockPanelViewModel
{
    protected StudioDockPanelViewModel(StudioShellViewModel shell)
    {
        ArgumentNullException.ThrowIfNull(shell);
        Shell = shell;
    }

    public StudioShellViewModel Shell { get; }
}

internal sealed class StudioScenePanelViewModel :
    StudioDockPanelViewModel,
    INotifyPropertyChanged,
    IDisposable
{
    private readonly IProjectSession projectSession_;
    private readonly IEditorSelectionService selection_;
    private ViewportSession? session_;
    private ulong viewportRevision_;
    private ulong selectionSourceRevision_;
    private ulong viewStateRevision_;
    private Guid? selectedObjectId_;
    private IViewportTransformGizmoInteraction? transformGizmoInteraction_;
    private ViewportTransformGizmoKind transformGizmoKind_ =
        ViewportTransformGizmoKind.Translate;
    private ViewportGizmoAxis hoveredGizmoAxis_;
    private bool isTransformGizmoCommitPending_;
    private bool isRealtime_ = true;
    private bool isWireframe_;
    private bool isDisposed_;

    public StudioScenePanelViewModel(StudioShellViewModel shell)
        : base(shell)
    {
        projectSession_ = shell.ProjectSession;
        selection_ = shell.EditorSelection;
        projectSession_.SnapshotChanged += OnProjectSnapshotChanged;
        selection_.Changed += OnSelectionChanged;
        ApplyProjectSnapshot(projectSession_.Current);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public ViewportSession? Session => session_;

    public ulong ViewportRevision => viewportRevision_;

    public bool IsRealtime
    {
        get => isRealtime_;
        set
        {
            if (isRealtime_ == value)
            {
                return;
            }
            isRealtime_ = value;
            OnPropertyChanged();
        }
    }

    public bool IsWireframe
    {
        get => isWireframe_;
        set
        {
            if (isWireframe_ == value)
            {
                return;
            }
            isWireframe_ = value;
            session_?.SetSceneRasterMode(
                value
                    ? ViewportSceneRasterMode.Wireframe
                    : ViewportSceneRasterMode.Solid);
            OnPropertyChanged();
        }
    }

    public bool IsTranslateGizmoMode
    {
        get => transformGizmoKind_ == ViewportTransformGizmoKind.Translate;
        set
        {
            if (value)
            {
                SetTransformGizmoMode(ViewportTransformGizmoKind.Translate);
            }
        }
    }

    public bool IsRotateGizmoMode
    {
        get => transformGizmoKind_ == ViewportTransformGizmoKind.Rotate;
        set
        {
            if (value)
            {
                SetTransformGizmoMode(ViewportTransformGizmoKind.Rotate);
            }
        }
    }

    public ViewportPresentationLifetime PresentationLifetime =>
        Shell.ViewportPresentationLifetime;

    public bool TryApplyViewportPick(
        ViewportPresentedInteractionContext context,
        ViewportPickRequest request)
    {
        if (isDisposed_ || session_ is not { } session || request.Extent != context.Extent)
        {
            return false;
        }

        var project = projectSession_.Current;
        if (project.Project is not { } activeProject || project.Document is not { } document ||
            document.SceneId != context.TargetId ||
            document.Revision != context.TargetRevision ||
            !session.TryCapturePickSnapshot(
                context.SessionId,
                context.TargetId,
                context.TargetRevision,
                context.FrameSequence,
                out var snapshot))
        {
            return false;
        }

        var result = ViewportScenePicker.Pick(snapshot, request);
        if (result.ObjectId is { } objectId)
        {
            _ = selection_.Replace(new SceneObjectSelectionTarget(
                activeProject.SessionId,
                document.SceneId,
                objectId));
        }
        else
        {
            _ = selection_.Clear();
        }

        return true;
    }

    public bool TryApplyCameraNavigation(ViewportCameraNavigationDelta delta)
    {
        if (isDisposed_ || session_ is not { } session)
        {
            return false;
        }

        var camera = session.Camera;
        var nextCamera = ViewportSceneCameraNavigation.Apply(camera, delta);
        if (ReferenceEquals(camera, nextCamera))
        {
            return false;
        }

        session.SetCamera(nextCamera);
        return true;
    }

    public void SetTransformGizmoMode(ViewportTransformGizmoKind kind)
    {
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, null);
        }
        if (isDisposed_ || isTransformGizmoCommitPending_ ||
            transformGizmoKind_ == kind)
        {
            return;
        }

        transformGizmoInteraction_ = null;
        hoveredGizmoAxis_ = ViewportGizmoAxis.None;
        transformGizmoKind_ = kind;
        session_?.SetTransformGizmoKind(kind);
        OnPropertyChanged(nameof(IsTranslateGizmoMode));
        OnPropertyChanged(nameof(IsRotateGizmoMode));
    }

    public bool TryBeginTransformGizmo(
        ViewportPresentedInteractionContext context,
        ViewportPickRequest request)
    {
        if (isDisposed_ || isTransformGizmoCommitPending_ ||
            transformGizmoInteraction_ is not null ||
            session_ is not { } session || request.Extent != context.Extent)
        {
            return false;
        }

        var project = projectSession_.Current;
        if (project.Document is not { } document ||
            document.SceneId != context.TargetId ||
            document.Revision != context.TargetRevision ||
            !session.TryCaptureTransformGizmoSnapshot(
                context.SessionId,
                context.TargetId,
                context.TargetRevision,
                context.FrameSequence,
                out var snapshot) ||
            !TryBeginTransformGizmo(snapshot, request, out var interaction))
        {
            return false;
        }

        transformGizmoInteraction_ = interaction;
        hoveredGizmoAxis_ = interaction.Axis;
        session.SetTransformGizmo(new ViewportTransformGizmoState(
            snapshot.Kind,
            interaction.ObjectId,
            interaction.CurrentTransform,
            interaction.Axis,
            interaction.Axis));
        return true;
    }

    public bool TryUpdateTransformGizmo(ViewportPickPoint point)
    {
        if (isDisposed_ || transformGizmoInteraction_ is not { } interaction ||
            session_ is not { } session || !interaction.TryUpdate(point, out var transform))
        {
            return false;
        }

        session.SetTransformGizmo(new ViewportTransformGizmoState(
            transformGizmoKind_,
            interaction.ObjectId,
            transform,
            interaction.Axis,
            interaction.Axis));
        return true;
    }

    public bool TryUpdateTransformGizmoHover(
        ViewportPresentedInteractionContext context,
        ViewportPickRequest request)
    {
        if (isDisposed_ || isTransformGizmoCommitPending_ ||
            transformGizmoInteraction_ is not null ||
            session_ is not { } session || request.Extent != context.Extent ||
            !session.TryCaptureTransformGizmoSnapshot(
                context.SessionId,
                context.TargetId,
                context.TargetRevision,
                context.FrameSequence,
                out var snapshot))
        {
            return false;
        }

        var axis = HitTestTransformGizmo(snapshot, request);
        if (hoveredGizmoAxis_ == axis)
        {
            return true;
        }

        hoveredGizmoAxis_ = axis;
        session.SetTransformGizmo(new ViewportTransformGizmoState(
            snapshot.Kind,
            snapshot.ObjectId,
            snapshot.Transform,
            axis));
        return true;
    }

    public async ValueTask<bool> CompleteTransformGizmoAsync()
    {
        if (isDisposed_ || transformGizmoInteraction_ is not { } interaction ||
            session_ is not { } session)
        {
            return false;
        }

        transformGizmoInteraction_ = null;
        hoveredGizmoAxis_ = ViewportGizmoAxis.None;
        if (!interaction.HasChanged)
        {
            session.ResetTransformGizmo();
            return true;
        }

        var current = projectSession_.Current;
        if (current.Document is not { } document ||
            document.SceneId != session.Current.TargetId ||
            document.Revision != interaction.ExpectedRevision ||
            selectedObjectId_ != interaction.ObjectId)
        {
            session.ResetTransformGizmo();
            return false;
        }

        isTransformGizmoCommitPending_ = true;
        session.SetTransformGizmo(new ViewportTransformGizmoState(
            transformGizmoKind_,
            interaction.ObjectId,
            interaction.CurrentTransform));
        var editId = ProjectEditId.CreateNew();
        try
        {
            var result = await projectSession_.SetEntityTransformAsync(
                interaction.ObjectId,
                interaction.CurrentTransform,
                new ProjectSessionEditContext(editId, interaction.ExpectedRevision));
            if (isDisposed_)
            {
                return result.Succeeded;
            }

            ApplyProjectSnapshot(result.Current);
            if (!result.Succeeded)
            {
                session_?.ResetTransformGizmo();
            }
            Shell.PresentProjectOperationMessage(result.Message);
            return result.Succeeded;
        }
        catch (Exception exception)
        {
            if (!isDisposed_)
            {
                session_?.ResetTransformGizmo();
                Shell.PresentProjectOperationMessage(
                    $"Could not transform the selected entity. {exception.Message}");
            }
            return false;
        }
        finally
        {
            isTransformGizmoCommitPending_ = false;
        }
    }

    public void CancelTransformGizmo()
    {
        if (isDisposed_ || transformGizmoInteraction_ is null)
        {
            return;
        }

        transformGizmoInteraction_ = null;
        hoveredGizmoAxis_ = ViewportGizmoAxis.None;
        session_?.ResetTransformGizmo();
    }

    public void ClearTransformGizmoHover()
    {
        if (isDisposed_ || isTransformGizmoCommitPending_ ||
            transformGizmoInteraction_ is not null ||
            hoveredGizmoAxis_ == ViewportGizmoAxis.None)
        {
            return;
        }

        hoveredGizmoAxis_ = ViewportGizmoAxis.None;
        session_?.ResetTransformGizmo();
    }

    private static bool TryBeginTransformGizmo(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request,
        out IViewportTransformGizmoInteraction interaction)
    {
        interaction = null!;
        switch (snapshot.Kind)
        {
            case ViewportTransformGizmoKind.Translate
                when ViewportTranslateGizmoManipulator.TryBegin(
                    snapshot,
                    request,
                    out var translate):
                interaction = translate;
                return true;
            case ViewportTransformGizmoKind.Rotate
                when ViewportRotateGizmoManipulator.TryBegin(
                    snapshot,
                    request,
                    out var rotate):
                interaction = rotate;
                return true;
            default:
                return false;
        }
    }

    private static ViewportGizmoAxis HitTestTransformGizmo(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request) => snapshot.Kind switch
        {
            ViewportTransformGizmoKind.Translate =>
                ViewportTranslateGizmoManipulator.HitTest(snapshot, request),
            ViewportTransformGizmoKind.Rotate =>
                ViewportRotateGizmoManipulator.HitTest(snapshot, request),
            _ => ViewportGizmoAxis.None,
        };

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        transformGizmoInteraction_ = null;
        projectSession_.SnapshotChanged -= OnProjectSnapshotChanged;
        selection_.Changed -= OnSelectionChanged;
        session_?.Close();
        session_ = null;
    }

    private void OnProjectSnapshotChanged(
        object? sender,
        ProjectSessionSnapshotChangedEventArgs e)
    {
        var snapshot = e.Snapshot;
        if (Dispatcher.UIThread.CheckAccess())
        {
            ApplyProjectSnapshot(snapshot);
            return;
        }

        Dispatcher.UIThread.Post(() =>
        {
            if (!isDisposed_)
            {
                ApplyProjectSnapshot(snapshot);
            }
        });
    }

    private void ApplyProjectSnapshot(ProjectSessionSnapshot snapshot)
    {
        var document = snapshot.Document;
        if (document is null)
        {
            transformGizmoInteraction_ = null;
            hoveredGizmoAxis_ = ViewportGizmoAxis.None;
            ReplaceSession(null);
            return;
        }

        if (session_ is { } session && session.Current.TargetId == document.SceneId)
        {
            transformGizmoInteraction_ = null;
            hoveredGizmoAxis_ = ViewportGizmoAxis.None;
            session.SynchronizeDocument(document);
            ApplySelection(selection_.Current, snapshot);
            viewportRevision_ = document.Revision;
            OnPropertyChanged(nameof(ViewportRevision));
            return;
        }

        var replacement = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);
        replacement.SetTransformGizmoKind(transformGizmoKind_);
        replacement.SetSceneRasterMode(
            isWireframe_
                ? ViewportSceneRasterMode.Wireframe
                : ViewportSceneRasterMode.Solid);
        ReplaceSession(replacement);
        ApplySelection(selection_.Current, snapshot);
        viewportRevision_ = document.Revision;
        OnPropertyChanged(nameof(ViewportRevision));
    }

    private void OnSelectionChanged(object? sender, EditorSelectionChangedEventArgs e)
    {
        if (Dispatcher.UIThread.CheckAccess())
        {
            ApplySelection(e.Snapshot, projectSession_.Current);
            return;
        }

        Dispatcher.UIThread.Post(() =>
        {
            if (!isDisposed_)
            {
                ApplySelection(e.Snapshot, projectSession_.Current);
            }
        });
    }

    private void ApplySelection(
        EditorSelectionSnapshot selection,
        ProjectSessionSnapshot project)
    {
        if (session_ is not { } session || project.Project is not { } activeProject ||
            project.Document is not { } document)
        {
            return;
        }

        var selectedObjectId = selection.Primary is SceneObjectSelectionTarget target &&
            target.SessionId == activeProject.SessionId && target.SceneId == document.SceneId
                ? target.ObjectId
                : (Guid?)null;
        if (selection.Revision < selectionSourceRevision_)
        {
            return;
        }
        selectionSourceRevision_ = selection.Revision;
        if (selectedObjectId_ != selectedObjectId)
        {
            transformGizmoInteraction_ = null;
            hoveredGizmoAxis_ = ViewportGizmoAxis.None;
            selectedObjectId_ = selectedObjectId;
            viewStateRevision_ = checked(viewStateRevision_ + 1);
        }
        session.SetSelection(viewStateRevision_, selectedObjectId_);
    }

    private void ReplaceSession(ViewportSession? session)
    {
        if (ReferenceEquals(session_, session))
        {
            return;
        }

        session_?.Close();
        session_ = session;
        if (session is null)
        {
            viewportRevision_ = 0;
        }
        NotifyViewportChanged();
    }

    private void NotifyViewportChanged()
    {
        OnPropertyChanged(nameof(Session));
        OnPropertyChanged(nameof(ViewportRevision));
    }

    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

internal sealed class StudioInspectorPanelViewModel :
    StudioDockPanelViewModel,
    INotifyPropertyChanged,
    IDisposable
{
    private readonly IProjectAssetCatalog assetCatalog_;
    private readonly IEditorSelectionService selection_;
    private readonly IStudioResourceBrowserUiScheduler scheduler_;
    private bool isDisposed_;
    private bool isEntitySelection_;
    private bool isAssetSelection_;
    private StudioAssetInspectorViewModel? asset_;

    public StudioInspectorPanelViewModel(
        StudioShellViewModel shell,
        IProjectAssetCatalog assetCatalog,
        IEditorSelectionService selection,
        IStudioResourceBrowserUiScheduler? scheduler = null)
        : base(shell)
    {
        ArgumentNullException.ThrowIfNull(assetCatalog);
        ArgumentNullException.ThrowIfNull(selection);
        assetCatalog_ = assetCatalog;
        selection_ = selection;
        scheduler_ = scheduler ?? StudioAvaloniaResourceBrowserUiScheduler.Instance;
        selection_.Changed += OnSelectionChanged;
        assetCatalog_.SnapshotChanged += OnCatalogSnapshotChanged;
        Shell.PropertyChanged += OnShellPropertyChanged;
        ApplyProjection(selection_.Current, assetCatalog_.Current);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public bool HasSelection => IsEntitySelection || IsAssetSelection;

    public bool IsEmptySelection => !HasSelection;

    public bool IsEntitySelection => isEntitySelection_;

    public bool IsAssetSelection => isAssetSelection_;

    public StudioAssetInspectorViewModel? Asset => asset_;

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        selection_.Changed -= OnSelectionChanged;
        assetCatalog_.SnapshotChanged -= OnCatalogSnapshotChanged;
        Shell.PropertyChanged -= OnShellPropertyChanged;
    }

    private void OnSelectionChanged(object? sender, EditorSelectionChangedEventArgs e) =>
        PostProjection();

    private void OnCatalogSnapshotChanged(
        object? sender,
        AssetCatalogSessionSnapshotChangedEventArgs e) =>
        PostProjection();

    private void OnShellPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (string.Equals(
                e.PropertyName,
                nameof(StudioShellViewModel.SelectedEntity),
                StringComparison.Ordinal))
        {
            PostProjection();
        }
    }

    private void PostProjection() =>
        scheduler_.Post(() =>
        {
            if (!isDisposed_)
            {
                ApplyProjection(selection_.Current, assetCatalog_.Current);
            }
        });

    private void ApplyProjection(
        EditorSelectionSnapshot selection,
        AssetCatalogSessionSnapshot catalog)
    {
        var entitySelected = selection.Primary is SceneObjectSelectionTarget;
        StudioAssetInspectorViewModel? asset = null;
        if (selection.Primary is AssetSelectionTarget target
            && SameScope(target, catalog.Scope))
        {
            var entry = catalog.Catalog?.Entries.FirstOrDefault(
                candidate => candidate.SelectionKey.Equals(target.Asset));
            if (entry is not null)
            {
                asset = new StudioAssetInspectorViewModel(
                    entry,
                    target.TargetProfile,
                    catalog);
            }
        }

        isEntitySelection_ = entitySelected;
        isAssetSelection_ = asset is not null;
        asset_ = asset;
        OnPropertyChanged(nameof(HasSelection));
        OnPropertyChanged(nameof(IsEmptySelection));
        OnPropertyChanged(nameof(IsEntitySelection));
        OnPropertyChanged(nameof(IsAssetSelection));
        OnPropertyChanged(nameof(Asset));
    }

    private static bool SameScope(
        AssetSelectionTarget selection,
        AssetCatalogQueryScope? scope) =>
        scope is not null
        && selection.SessionId == scope.SessionId
        && selection.ProjectId == scope.ProjectId
        && string.Equals(
            selection.TargetProfile,
            scope.TargetProfile,
            StringComparison.Ordinal);

    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

internal sealed class StudioAssetInspectorViewModel
{
    public StudioAssetInspectorViewModel(
        AssetCatalogEntry entry,
        string targetProfile,
        AssetCatalogSessionSnapshot catalog)
    {
        ArgumentNullException.ThrowIfNull(entry);
        ArgumentException.ThrowIfNullOrWhiteSpace(targetProfile);
        Name = entry.DisplayName;
        TypeName = entry.AssetTypeName;
        ProductState = entry.ProductState switch
        {
            AssetCatalogProductState.NotTracked => "Not tracked",
            AssetCatalogProductState.Current => "Current",
            AssetCatalogProductState.Missing => "Missing",
            AssetCatalogProductState.Stale => "Stale",
            AssetCatalogProductState.Invalid => "Invalid",
            _ => "Unknown",
        };
        SourcePath = entry.SourcePath;
        Extension = string.IsNullOrWhiteSpace(entry.Extension) ? "None" : entry.Extension;
        GuidText = string.IsNullOrWhiteSpace(entry.GuidText) ? "Untracked" : entry.GuidText;
        Importer = string.IsNullOrWhiteSpace(entry.ImporterName)
            ? "No importer"
            : $"{entry.ImporterName} v{entry.ImporterVersion.ToString(CultureInfo.InvariantCulture)}";
        Profile = string.IsNullOrWhiteSpace(entry.ImportProfileName)
            ? "Default profile"
            : entry.ImportProfileName;
        Role = string.IsNullOrWhiteSpace(entry.AssetRoleName)
            ? "Unspecified role"
            : entry.AssetRoleName;
        TargetProfile = targetProfile;
        CatalogState = catalog.State switch
        {
            AssetCatalogSessionState.Ready => "Ready",
            AssetCatalogSessionState.Degraded => "Degraded",
            AssetCatalogSessionState.Loading => "Refreshing",
            _ => "Unavailable",
        };
        CatalogRevision = catalog.Catalog?.Revision.ToString(CultureInfo.InvariantCulture)
            ?? "Unavailable";
        Products = $"{entry.CurrentProductCount.ToString(CultureInfo.InvariantCulture)} current · "
            + $"{entry.StaleProductCount.ToString(CultureInfo.InvariantCulture)} stale";
        SubAssets = entry.SubAssets
            .Select(static item => new StudioAssetInspectorSubAssetViewModel(
                item.DisplayName,
                item.StableId,
                item.AssetRoleName))
            .ToArray();
        Diagnostics = entry.Diagnostics
            .Select(static item => new StudioAssetInspectorDiagnosticViewModel(
                item.Severity.ToString(),
                item.Code,
                item.Message))
            .ToArray();
    }

    public string Name { get; }
    public string TypeName { get; }
    public string ProductState { get; }
    public string SourcePath { get; }
    public string Extension { get; }
    public string GuidText { get; }
    public string Importer { get; }
    public string Profile { get; }
    public string Role { get; }
    public string TargetProfile { get; }
    public string CatalogState { get; }
    public string CatalogRevision { get; }
    public string Products { get; }
    public IReadOnlyList<StudioAssetInspectorSubAssetViewModel> SubAssets { get; }
    public IReadOnlyList<StudioAssetInspectorDiagnosticViewModel> Diagnostics { get; }
    public bool HasSubAssets => SubAssets.Count != 0;
    public bool HasDiagnostics => Diagnostics.Count != 0;
}

internal sealed record StudioAssetInspectorSubAssetViewModel(
    string Name,
    string StableId,
    string Role)
{
    public string AutomationName => $"{Name}, {Role}, {StableId}";
}

internal sealed record StudioAssetInspectorDiagnosticViewModel(
    string Severity,
    string Code,
    string Message)
{
    public string Header => $"{Severity}  {Code}";

    public string AutomationName => $"{Severity}, {Code}, {Message}";
}
