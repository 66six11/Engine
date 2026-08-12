using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Threading;
using Editor.Shell.Commands;
using Editor.Shell.Docking.Panels;
using Editor.Shell.Services.Projects;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.ViewModels.Panels;
using Editor.UI.Icons;

namespace Editor.Shell.ViewModels.Windowing;

internal enum StudioShellStage
{
    Starting,
    Ready,
    Stopping,
}

[Flags]
internal enum RotationAxisMask
{
    None = 0,
    X = 1,
    Y = 2,
    Z = 4,
}

internal sealed record PendingRotationApply(
    ProjectEditId EditId,
    ProjectSessionId SessionId,
    Guid SceneId,
    Guid ObjectId,
    ulong BaseRevision,
    StudioEulerDegrees SubmittedEuler,
    string SubmittedTextX,
    string SubmittedTextY,
    string SubmittedTextZ,
    Quaternion SubmittedQuaternion,
    RotationAxisMask DirtyAxes,
    ulong EditVersion);

internal sealed class StudioShellViewModel : INotifyPropertyChanged, IDisposable
{
    private readonly IProjectSession projectSession_;
    private readonly IStudioProjectDialogService projectDialogs_;
    private readonly AsyncCommand createProjectCommand_;
    private readonly AsyncCommand openProjectCommand_;
    private readonly AsyncCommand closeProjectCommand_;
    private readonly AsyncCommand createEntityCommand_;
    private readonly AsyncCommand createMeshEntityCommand_;
    private readonly AsyncCommand saveSceneCommand_;
    private readonly AsyncCommand applyEntityNameCommand_;
    private readonly AsyncCommand applyEntityTransformCommand_;
    private readonly EditorDockWorkspaceViewModel dockWorkspace_;
    private readonly ViewportPresentationLifetime viewportPresentationLifetime_;
    private readonly CancellationTokenSource lifetimeCancellation_ = new();
    private StudioShellStage stage_ = StudioShellStage.Starting;
    private ProjectSessionSnapshot projectSnapshot_;
    private SceneEntitySnapshot? selectedEntity_;
    private string newProjectName_ = "MyProject";
    private string projectOperationMessage_ = string.Empty;
    private string inspectorName_ = string.Empty;
    private string positionX_ = "0";
    private string positionY_ = "0";
    private string positionZ_ = "0";
    private string rotationDegreesX_ = "0";
    private string rotationDegreesY_ = "0";
    private string rotationDegreesZ_ = "0";
    private StudioEulerDegrees rotationEulerHint_;
    private Quaternion authoritativeRotation_ = Quaternion.Identity;
    private RotationAxisMask rotationDirtyAxes_;
    private ulong rotationEditVersion_;
    private ulong rotationXEditVersion_;
    private ulong rotationYEditVersion_;
    private ulong rotationZEditVersion_;
    private ulong transformEditBaseRevision_;
    private PendingRotationApply? pendingRotationApply_;
    private string scaleX_ = "1";
    private string scaleY_ = "1";
    private string scaleZ_ = "1";
    private bool isProjectOperationRunning_;
    private bool isDisposed_;

    public StudioShellViewModel(
        IProjectSession projectSession,
        IStudioProjectDialogService projectDialogs)
    {
        ArgumentNullException.ThrowIfNull(projectSession);
        ArgumentNullException.ThrowIfNull(projectDialogs);
        projectSession_ = projectSession;
        projectDialogs_ = projectDialogs;
        viewportPresentationLifetime_ = new ViewportPresentationLifetime();
        projectSnapshot_ = projectSession.Current;
        createProjectCommand_ = new AsyncCommand(CreateProjectAsync, CanCreateProject);
        openProjectCommand_ = new AsyncCommand(OpenProjectAsync, CanRunProjectOperation);
        closeProjectCommand_ = new AsyncCommand(CloseProjectAsync, CanEditDocument);
        createEntityCommand_ = new AsyncCommand(CreateEntityAsync, CanEditDocument);
        createMeshEntityCommand_ = new AsyncCommand(
            CreateMeshEntityAsync,
            CanEditDocument);
        saveSceneCommand_ = new AsyncCommand(SaveSceneAsync, CanSaveDocument);
        applyEntityNameCommand_ = new AsyncCommand(ApplyEntityNameAsync, CanEditSelection);
        applyEntityTransformCommand_ = new AsyncCommand(
            ApplyEntityTransformAsync,
            CanEditSelection);
        dockWorkspace_ = CreateDockWorkspace();
        projectSession_.SnapshotChanged += OnProjectSnapshotChanged;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public StudioShellStage Stage => stage_;

    public string WindowTitle
    {
        get
        {
            if (stage_ == StudioShellStage.Starting)
            {
                return "Starting - Asharia Studio";
            }

            var project = projectSnapshot_.Project?.ProjectName ?? "No Project";
            var document = projectSnapshot_.Document;
            if (document is null)
            {
                return $"No Document - {project} - Asharia Studio";
            }
            var dirty = document.IsDirty ? "*" : string.Empty;
            return $"{dirty}{Path.GetFileName(document.Path)} - {project} - Asharia Studio";
        }
    }

    public bool IsStarting => stage_ == StudioShellStage.Starting;

    public bool IsWorkspaceVisible => stage_ == StudioShellStage.Ready;

    public bool HasProject => projectSnapshot_.IsReady;

    public bool HasNoProject => !projectSnapshot_.IsReady;

    public bool HasDocument => projectSnapshot_.Document is not null;

    public bool HasNoDocument => projectSnapshot_.Document is null;

    public bool IsDocumentDirty => projectSnapshot_.Document?.IsDirty == true;

    public bool HasSelection => SelectedEntity is not null;

    public string StartingStateText => "Starting";

    public string ProjectStateText =>
        projectSnapshot_.Project?.ProjectName ?? "No Project";

    public string ProjectPathText =>
        projectSnapshot_.Project?.RootPath ?? string.Empty;

    public string DocumentStateText => projectSnapshot_.Document is { } document
        ? $"{Path.GetFileName(document.Path)} · revision {document.Revision} · " +
          (document.IsDirty ? "Unsaved" : "Saved")
        : "No Document";

    public string DocumentPathText => projectSnapshot_.Document?.Path ?? string.Empty;

    public IReadOnlyList<SceneEntitySnapshot> SceneEntities =>
        projectSnapshot_.Document?.Entities ?? [];

    public SceneEntitySnapshot? SelectedEntity
    {
        get => selectedEntity_;
        set
        {
            if (ReferenceEquals(selectedEntity_, value))
            {
                return;
            }
            selectedEntity_ = value;
            LoadInspector(value);
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasSelection));
            RaiseProjectCommandStateChanged();
        }
    }

    public string NewProjectName
    {
        get => newProjectName_;
        set
        {
            if (string.Equals(newProjectName_, value, StringComparison.Ordinal))
            {
                return;
            }
            newProjectName_ = value;
            OnPropertyChanged();
            createProjectCommand_.RaiseCanExecuteChanged();
        }
    }

    public string InspectorName
    {
        get => inspectorName_;
        set => SetField(ref inspectorName_, value);
    }

    public string PositionX { get => positionX_; set => SetField(ref positionX_, value); }
    public string PositionY { get => positionY_; set => SetField(ref positionY_, value); }
    public string PositionZ { get => positionZ_; set => SetField(ref positionZ_, value); }
    public string RotationDegreesX
    {
        get => rotationDegreesX_;
        set => SetRotationField(ref rotationDegreesX_, value, RotationAxisMask.X);
    }

    public string RotationDegreesY
    {
        get => rotationDegreesY_;
        set => SetRotationField(ref rotationDegreesY_, value, RotationAxisMask.Y);
    }

    public string RotationDegreesZ
    {
        get => rotationDegreesZ_;
        set => SetRotationField(ref rotationDegreesZ_, value, RotationAxisMask.Z);
    }
    public string ScaleX { get => scaleX_; set => SetField(ref scaleX_, value); }
    public string ScaleY { get => scaleY_; set => SetField(ref scaleY_, value); }
    public string ScaleZ { get => scaleZ_; set => SetField(ref scaleZ_, value); }

    public bool IsProjectOperationRunning
    {
        get => isProjectOperationRunning_;
        private set
        {
            if (isProjectOperationRunning_ == value)
            {
                return;
            }
            isProjectOperationRunning_ = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(ProjectOperationStateText));
            RaiseProjectCommandStateChanged();
        }
    }

    public string ProjectOperationStateText =>
        IsProjectOperationRunning ? "Working..." : string.Empty;

    public string ProjectOperationMessage
    {
        get => projectOperationMessage_;
        private set
        {
            if (string.Equals(projectOperationMessage_, value, StringComparison.Ordinal))
            {
                return;
            }
            projectOperationMessage_ = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasProjectOperationMessage));
        }
    }

    public bool HasProjectOperationMessage =>
        !string.IsNullOrWhiteSpace(ProjectOperationMessage);

    public ICommand CreateProjectCommand => createProjectCommand_;
    public ICommand OpenProjectCommand => openProjectCommand_;
    public ICommand CloseProjectCommand => closeProjectCommand_;
    public ICommand CreateEntityCommand => createEntityCommand_;
    public ICommand CreateMeshEntityCommand => createMeshEntityCommand_;
    public ICommand SaveSceneCommand => saveSceneCommand_;
    public ICommand ApplyEntityNameCommand => applyEntityNameCommand_;
    public ICommand ApplyEntityTransformCommand => applyEntityTransformCommand_;
    public EditorDockWorkspaceViewModel DockWorkspace => dockWorkspace_;

    internal IProjectSession ProjectSession => projectSession_;

    internal ProjectSessionSnapshot AppliedProjectSnapshot => projectSnapshot_;

    internal ViewportPresentationLifetime ViewportPresentationLifetime =>
        viewportPresentationLifetime_;

    public void MarkReady()
    {
        ThrowIfDisposed();
        if (stage_ != StudioShellStage.Starting)
        {
            throw new InvalidOperationException(
                $"Studio shell cannot enter Ready from '{stage_}'.");
        }
        SetStage(StudioShellStage.Ready);
    }

    public void MarkStopping()
    {
        ThrowIfDisposed();
        if (stage_ == StudioShellStage.Stopping)
        {
            return;
        }
        _ = lifetimeCancellation_.CancelAsync();
        SetStage(StudioShellStage.Stopping);
    }

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }
        isDisposed_ = true;
        projectSession_.SnapshotChanged -= OnProjectSnapshotChanged;
        dockWorkspace_.Dispose();
        lifetimeCancellation_.Cancel();
        RaiseProjectCommandStateChanged();
    }

    private EditorDockWorkspaceViewModel CreateDockWorkspace()
    {
        var panels = new PanelRegistry();
        panels.Register(new PanelDescriptor(
            "hierarchy",
            "Hierarchy",
            PanelKind.Tool,
            EditorDockArea.Left,
            "Window/Panels/Hierarchy",
            DockContentCachePolicy.KeepAlive,
            () => new StudioHierarchyPanelViewModel(this),
            EditorIconKey.PanelHierarchy,
            "SCENE",
            "Scene entities",
            "tool"));
        panels.Register(new PanelDescriptor(
            "project",
            "Project",
            PanelKind.Tool,
            EditorDockArea.Left,
            "Window/Panels/Project",
            DockContentCachePolicy.KeepAlive,
            () => new StudioProjectPanelViewModel(this),
            EditorIconKey.PanelProject,
            "PROJECT",
            "Project content",
            "tool"));
        panels.Register(new PanelDescriptor(
            "scene-view",
            "Scene Document",
            PanelKind.Document,
            EditorDockArea.Center,
            "Window/Panels/Scene Document",
            DockContentCachePolicy.KeepAlive,
            () => new StudioScenePanelViewModel(this),
            EditorIconKey.PanelSceneView,
            "DOC",
            "Authoritative edit world",
            "document"));
        panels.Register(new PanelDescriptor(
            "inspector",
            "Inspector",
            PanelKind.Tool,
            EditorDockArea.Right,
            "Window/Panels/Inspector",
            DockContentCachePolicy.KeepAlive,
            () => new StudioInspectorPanelViewModel(this),
            EditorIconKey.PanelInspector,
            "ENTITY",
            "Selection context",
            "tool"));
        return new EditorDockWorkspaceViewModel(panels);
    }

    private bool CanCreateProject() =>
        CanRunProjectOperation() && !string.IsNullOrWhiteSpace(NewProjectName);

    private bool CanRunProjectOperation() =>
        !isDisposed_ && stage_ == StudioShellStage.Ready && !IsProjectOperationRunning;

    private bool CanEditDocument() => CanRunProjectOperation() && HasDocument;

    private bool CanSaveDocument() => CanEditDocument() && IsDocumentDirty;

    private bool CanEditSelection() => CanEditDocument() && HasSelection;

    private async Task CreateProjectAsync()
    {
        await RunProjectOperationAsync(async token =>
        {
            var parent = await projectDialogs_.SelectProjectParentDirectoryAsync(token);
            return parent is null
                ? null
                : await ExecuteWithPresentationDrainAsync(
                    () => projectSession_.CreateProjectAsync(parent, NewProjectName, token));
        });
    }

    private async Task OpenProjectAsync()
    {
        await RunProjectOperationAsync(async token =>
        {
            var descriptor = await projectDialogs_.SelectProjectDescriptorAsync(token);
            return descriptor is null
                ? null
                : await ExecuteWithPresentationDrainAsync(
                    () => projectSession_.OpenProjectAsync(descriptor, token));
        });
    }

    private Task CloseProjectAsync() =>
        RunProjectOperationAsync(async token =>
            await ExecuteWithPresentationDrainAsync(
                () => projectSession_.CloseProjectAsync(token)));

    private async Task CreateEntityAsync()
    {
        ProjectSessionOperationResult? result = null;
        await RunProjectOperationAsync(async token =>
            result = await projectSession_.CreateEntityAsync("Entity", token));
        SelectCreatedEntity(result);
    }

    private async Task CreateMeshEntityAsync()
    {
        ProjectSessionOperationResult? result = null;
        await RunProjectOperationAsync(async token =>
            result = await projectSession_.CreateMeshEntityAsync(
                "Directional Wedge",
                SceneMeshReference.DirectionalWedgeValidation,
                token));
        SelectCreatedEntity(result);
    }

    private void SelectCreatedEntity(ProjectSessionOperationResult? result)
    {
        if (result is not { Succeeded: true, CreatedObjectId: { } objectId })
        {
            return;
        }

        var created = SceneEntities.SingleOrDefault(entity => entity.ObjectId == objectId);
        if (created is null)
        {
            ProjectOperationMessage =
                "The created scene entity was not present in the applied project snapshot.";
            return;
        }

        SelectedEntity = created;
    }

    private Task SaveSceneAsync() =>
        RunProjectOperationAsync(async token =>
            await projectSession_.SaveSceneAsync(token));

    private Task ApplyEntityNameAsync()
    {
        var selected = SelectedEntity;
        return selected is null
            ? Task.CompletedTask
            : RunProjectOperationAsync(async token =>
                await projectSession_.SetEntityNameAsync(
                    selected.ObjectId,
                    InspectorName,
                    token));
    }

    private async Task ApplyEntityTransformAsync()
    {
        var selected = SelectedEntity;
        if (selected is null)
        {
            return;
        }
        if (!TryReadTransform(out var transform, out var submittedEuler))
        {
            ProjectOperationMessage =
                "Transform fields must be finite invariant-culture numbers; rotation is expressed in degrees.";
            return;
        }
        rotationEulerHint_ = submittedEuler;

        var project = projectSnapshot_.Project;
        var document = projectSnapshot_.Document;
        if (project is null || document is null)
        {
            return;
        }

        var editId = ProjectEditId.CreateNew();
        var baseRevision = rotationDirtyAxes_ == RotationAxisMask.None
            ? document.Revision
            : transformEditBaseRevision_;
        pendingRotationApply_ = new PendingRotationApply(
            editId,
            project.SessionId,
            document.SceneId,
            selected.ObjectId,
            baseRevision,
            submittedEuler,
            rotationDegreesX_,
            rotationDegreesY_,
            rotationDegreesZ_,
            transform.Rotation,
            rotationDirtyAxes_,
            rotationEditVersion_);

        ProjectOperationMessage = string.Empty;
        IsProjectOperationRunning = true;
        try
        {
            var result = await projectSession_.SetEntityTransformAsync(
                selected.ObjectId,
                transform,
                new ProjectSessionEditContext(editId, baseRevision),
                lifetimeCancellation_.Token);
            ApplyProjectSnapshot(
                result.Current,
                result.OriginatingEditId,
                result.OriginatingEditId is null ? null : result.Succeeded);
            if (!result.Succeeded && pendingRotationApply_?.EditId == editId)
            {
                pendingRotationApply_ = null;
            }
            ProjectOperationMessage = result.Message;
        }
        catch (OperationCanceledException) when (lifetimeCancellation_.IsCancellationRequested)
        {
            if (pendingRotationApply_?.EditId == editId)
            {
                pendingRotationApply_ = null;
            }
        }
        catch (Exception exception)
        {
            if (pendingRotationApply_?.EditId == editId)
            {
                pendingRotationApply_ = null;
            }
            ProjectOperationMessage = string.IsNullOrWhiteSpace(exception.Message)
                ? "The project operation failed without a diagnostic."
                : exception.Message;
        }
        finally
        {
            IsProjectOperationRunning = false;
        }
    }

    private async Task RunProjectOperationAsync(
        Func<CancellationToken, ValueTask<ProjectSessionOperationResult?>> operation)
    {
        ProjectOperationMessage = string.Empty;
        IsProjectOperationRunning = true;
        try
        {
            var result = await operation(lifetimeCancellation_.Token);
            if (result is null)
            {
                return;
            }
            ApplyProjectSnapshot(result.Current);
            ProjectOperationMessage = result.Message;
        }
        catch (OperationCanceledException) when (lifetimeCancellation_.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            ProjectOperationMessage = string.IsNullOrWhiteSpace(exception.Message)
                ? "The project operation failed without a diagnostic."
                : exception.Message;
        }
        finally
        {
            IsProjectOperationRunning = false;
        }
    }

    private async ValueTask<ProjectSessionOperationResult>
        ExecuteWithPresentationDrainAsync(
            Func<ValueTask<ProjectSessionOperationResult>> operation)
    {
        await using var pause =
            await viewportPresentationLifetime_.PauseAndDrainAsync();
        return await operation();
    }

    private void OnProjectSnapshotChanged(
        object? sender,
        ProjectSessionSnapshotChangedEventArgs e)
    {
        if (Dispatcher.UIThread.CheckAccess())
        {
            ApplyProjectSnapshot(
                e.Snapshot,
                e.OriginatingEditId,
                e.OriginatingEditSucceeded);
            return;
        }
        Dispatcher.UIThread.Post(() =>
        {
            if (!isDisposed_)
            {
                ApplyProjectSnapshot(
                    e.Snapshot,
                    e.OriginatingEditId,
                    e.OriginatingEditSucceeded);
            }
        });
    }

    private void ApplyProjectSnapshot(
        ProjectSessionSnapshot snapshot,
        ProjectEditId? originatingEditId = null,
        bool? originatingEditSucceeded = null)
    {
        var previousSessionId = projectSnapshot_.Project?.SessionId;
        var previousSceneId = projectSnapshot_.Document?.SceneId;
        var sameSelectionScope = previousSessionId == snapshot.Project?.SessionId
            && previousSceneId == snapshot.Document?.SceneId;
        var selectedObjectId = sameSelectionScope
            ? selectedEntity_?.ObjectId
            : null;
        projectSnapshot_ = snapshot;
        var nextSelection = selectedObjectId is { } id
            ? snapshot.Document?.Entities.FirstOrDefault(entity => entity.ObjectId == id)
            : null;
        var selectionChanged = !ReferenceEquals(selectedEntity_, nextSelection);
        selectedEntity_ = nextSelection;
        if (sameSelectionScope &&
            selectedObjectId is not null &&
            nextSelection is not null)
        {
            ReconcileInspector(
                nextSelection,
                originatingEditId,
                originatingEditSucceeded);
        }
        else if (selectionChanged)
        {
            LoadInspector(nextSelection);
        }
        if (selectionChanged)
        {
            OnPropertyChanged(nameof(SelectedEntity));
            OnPropertyChanged(nameof(HasSelection));
        }
        OnPropertyChanged(nameof(WindowTitle));
        OnPropertyChanged(nameof(HasProject));
        OnPropertyChanged(nameof(HasNoProject));
        OnPropertyChanged(nameof(HasDocument));
        OnPropertyChanged(nameof(HasNoDocument));
        OnPropertyChanged(nameof(IsDocumentDirty));
        OnPropertyChanged(nameof(ProjectStateText));
        OnPropertyChanged(nameof(ProjectPathText));
        OnPropertyChanged(nameof(DocumentStateText));
        OnPropertyChanged(nameof(DocumentPathText));
        OnPropertyChanged(nameof(SceneEntities));
        RaiseProjectCommandStateChanged();
    }

    private void LoadInspector(SceneEntitySnapshot? entity)
    {
        pendingRotationApply_ = null;
        rotationDirtyAxes_ = RotationAxisMask.None;
        rotationEditVersion_ = 0;
        rotationXEditVersion_ = 0;
        rotationYEditVersion_ = 0;
        rotationZEditVersion_ = 0;
        transformEditBaseRevision_ = projectSnapshot_.Document?.Revision ?? 0;
        var transform = entity?.Transform ?? TransformValue.Identity;
        authoritativeRotation_ = transform.Rotation;
        inspectorName_ = entity?.Name ?? string.Empty;
        positionX_ = Format(transform.Position.X);
        positionY_ = Format(transform.Position.Y);
        positionZ_ = Format(transform.Position.Z);
        if (!StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
                transform.Rotation,
                new StudioEulerDegrees(0.0, 0.0, 0.0),
                out rotationEulerHint_))
        {
            rotationEulerHint_ = default;
        }
        SetRotationTextFromHint();
        scaleX_ = Format(transform.Scale.X);
        scaleY_ = Format(transform.Scale.Y);
        scaleZ_ = Format(transform.Scale.Z);
        OnPropertyChanged(nameof(InspectorName));
        OnPropertyChanged(nameof(PositionX));
        OnPropertyChanged(nameof(PositionY));
        OnPropertyChanged(nameof(PositionZ));
        OnPropertyChanged(nameof(RotationDegreesX));
        OnPropertyChanged(nameof(RotationDegreesY));
        OnPropertyChanged(nameof(RotationDegreesZ));
        OnPropertyChanged(nameof(ScaleX));
        OnPropertyChanged(nameof(ScaleY));
        OnPropertyChanged(nameof(ScaleZ));
    }

    private void ReconcileInspector(
        SceneEntitySnapshot entity,
        ProjectEditId? originatingEditId,
        bool? originatingEditSucceeded)
    {
        var transform = entity.Transform;
        inspectorName_ = entity.Name;
        positionX_ = Format(transform.Position.X);
        positionY_ = Format(transform.Position.Y);
        positionZ_ = Format(transform.Position.Z);
        scaleX_ = Format(transform.Scale.X);
        scaleY_ = Format(transform.Scale.Y);
        scaleZ_ = Format(transform.Scale.Z);

        var document = projectSnapshot_.Document;
        var project = projectSnapshot_.Project;
        var pending = pendingRotationApply_;
        var acceptsPending = pending is not null &&
            originatingEditSucceeded == true &&
            originatingEditId == pending.EditId &&
            project?.SessionId == pending.SessionId &&
            document?.SceneId == pending.SceneId &&
            entity.ObjectId == pending.ObjectId &&
            (document.Revision == pending.BaseRevision ||
             document.Revision == pending.BaseRevision + 1) &&
            StudioEulerRotation.AreEquivalent(
                transform.Rotation,
                pending.SubmittedQuaternion);

        if (acceptsPending)
        {
            var editedAfterSubmit = RotationAxesEditedAfter(pending!.EditVersion);
            authoritativeRotation_ = transform.Rotation;
            if ((editedAfterSubmit & RotationAxisMask.X) == 0)
            {
                rotationDegreesX_ = pending.SubmittedTextX;
            }
            if ((editedAfterSubmit & RotationAxisMask.Y) == 0)
            {
                rotationDegreesY_ = pending.SubmittedTextY;
            }
            if ((editedAfterSubmit & RotationAxisMask.Z) == 0)
            {
                rotationDegreesZ_ = pending.SubmittedTextZ;
            }
            rotationEulerHint_ = HintFromCurrentText(
                pending.SubmittedEuler,
                editedAfterSubmit);
            rotationDirtyAxes_ = editedAfterSubmit;
            transformEditBaseRevision_ = document!.Revision;
            pendingRotationApply_ = null;
        }
        else
        {
            var rejectsPending = pending is not null &&
                originatingEditSucceeded == false &&
                originatingEditId == pending.EditId;
            if (rejectsPending)
            {
                pendingRotationApply_ = null;
            }

            var sameOrientation = StudioEulerRotation.AreEquivalent(
                authoritativeRotation_,
                transform.Rotation);
            if (rotationDirtyAxes_ != RotationAxisMask.None ||
                pendingRotationApply_ is not null)
            {
                if (!sameOrientation)
                {
                    MergeAuthoritativeRotationIntoDraft(transform.Rotation);
                }
                authoritativeRotation_ = transform.Rotation;
                transformEditBaseRevision_ = document?.Revision ?? 0;
            }
            else if (sameOrientation)
            {
                // Snapshot replacement, q/-q, name edits and saves must not
                // rewrite an already stable Editor Euler representation.
                authoritativeRotation_ = transform.Rotation;
                transformEditBaseRevision_ = document?.Revision ?? 0;
            }
            else if (StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
                         transform.Rotation,
                         rotationEulerHint_,
                         out var closest))
            {
                authoritativeRotation_ = transform.Rotation;
                rotationEulerHint_ = closest;
                SetRotationTextFromHint();
                transformEditBaseRevision_ = document?.Revision ?? 0;
            }
        }

        OnPropertyChanged(nameof(InspectorName));
        OnPropertyChanged(nameof(PositionX));
        OnPropertyChanged(nameof(PositionY));
        OnPropertyChanged(nameof(PositionZ));
        OnPropertyChanged(nameof(RotationDegreesX));
        OnPropertyChanged(nameof(RotationDegreesY));
        OnPropertyChanged(nameof(RotationDegreesZ));
        OnPropertyChanged(nameof(ScaleX));
        OnPropertyChanged(nameof(ScaleY));
        OnPropertyChanged(nameof(ScaleZ));
    }

    private void SetRotationTextFromHint()
    {
        rotationDegreesX_ = Format(rotationEulerHint_.X);
        rotationDegreesY_ = Format(rotationEulerHint_.Y);
        rotationDegreesZ_ = Format(rotationEulerHint_.Z);
    }

    private void MergeAuthoritativeRotationIntoDraft(Quaternion rotation)
    {
        if (!StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
                rotation,
                HintFromCurrentText(rotationEulerHint_, rotationDirtyAxes_),
                out var closest))
        {
            return;
        }

        if ((rotationDirtyAxes_ & RotationAxisMask.X) == 0)
        {
            rotationDegreesX_ = Format(closest.X);
        }
        if ((rotationDirtyAxes_ & RotationAxisMask.Y) == 0)
        {
            rotationDegreesY_ = Format(closest.Y);
        }
        if ((rotationDirtyAxes_ & RotationAxisMask.Z) == 0)
        {
            rotationDegreesZ_ = Format(closest.Z);
        }
        rotationEulerHint_ = HintFromCurrentText(closest, rotationDirtyAxes_);
    }

    private StudioEulerDegrees HintFromCurrentText(
        StudioEulerDegrees fallback,
        RotationAxisMask textAxes) =>
        new(
            (textAxes & RotationAxisMask.X) != 0 &&
                TryParseDouble(rotationDegreesX_, out var x)
                    ? x
                    : fallback.X,
            (textAxes & RotationAxisMask.Y) != 0 &&
                TryParseDouble(rotationDegreesY_, out var y)
                    ? y
                    : fallback.Y,
            (textAxes & RotationAxisMask.Z) != 0 &&
                TryParseDouble(rotationDegreesZ_, out var z)
                    ? z
                    : fallback.Z);

    private bool TryReadTransform(
        out TransformValue transform,
        out StudioEulerDegrees rotationEuler)
    {
        transform = default;
        rotationEuler = default;
        if (!TryParse(PositionX, out var px) || !TryParse(PositionY, out var py) ||
            !TryParse(PositionZ, out var pz) ||
            !TryParse(RotationDegreesX, out var rotationDegreesX) ||
            !TryParse(RotationDegreesY, out var rotationDegreesY) ||
            !TryParse(RotationDegreesZ, out var rotationDegreesZ) ||
            !TryParse(ScaleX, out var sx) ||
            !TryParse(ScaleY, out var sy) || !TryParse(ScaleZ, out var sz))
        {
            return false;
        }
        rotationEuler = new StudioEulerDegrees(
            rotationDegreesX,
            rotationDegreesY,
            rotationDegreesZ);
        transform = new TransformValue(
            new Float3(px, py, pz),
            StudioEulerRotation.QuaternionFromEulerDegreesYxz(rotationEuler),
            new Float3(sx, sy, sz));
        return true;
    }

    private static bool TryParse(string text, out float value) =>
        float.TryParse(
            text,
            NumberStyles.Float,
            CultureInfo.InvariantCulture,
            out value) && float.IsFinite(value);

    private static bool TryParseDouble(string text, out double value) =>
        double.TryParse(
            text,
            NumberStyles.Float,
            CultureInfo.InvariantCulture,
            out value) && double.IsFinite(value);

    private static string Format(float value) =>
        value.ToString("G9", CultureInfo.InvariantCulture);

    private static string Format(double value) =>
        Math.Abs(value) < 1.0e-10
            ? "0"
            : value.ToString("G15", CultureInfo.InvariantCulture);

    private void SetStage(StudioShellStage stage)
    {
        stage_ = stage;
        OnPropertyChanged(nameof(Stage));
        OnPropertyChanged(nameof(WindowTitle));
        OnPropertyChanged(nameof(IsStarting));
        OnPropertyChanged(nameof(IsWorkspaceVisible));
        RaiseProjectCommandStateChanged();
    }

    private void RaiseProjectCommandStateChanged()
    {
        createProjectCommand_.RaiseCanExecuteChanged();
        openProjectCommand_.RaiseCanExecuteChanged();
        closeProjectCommand_.RaiseCanExecuteChanged();
        createEntityCommand_.RaiseCanExecuteChanged();
        createMeshEntityCommand_.RaiseCanExecuteChanged();
        saveSceneCommand_.RaiseCanExecuteChanged();
        applyEntityNameCommand_.RaiseCanExecuteChanged();
        applyEntityTransformCommand_.RaiseCanExecuteChanged();
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(isDisposed_, this);

    private void SetField(
        ref string field,
        string value,
        [CallerMemberName] string? propertyName = null)
    {
        if (string.Equals(field, value, StringComparison.Ordinal))
        {
            return;
        }
        field = value;
        OnPropertyChanged(propertyName);
    }

    private void SetRotationField(
        ref string field,
        string value,
        RotationAxisMask axis,
        [CallerMemberName] string? propertyName = null)
    {
        if (string.Equals(field, value, StringComparison.Ordinal))
        {
            return;
        }
        if (rotationDirtyAxes_ == RotationAxisMask.None)
        {
            transformEditBaseRevision_ = projectSnapshot_.Document?.Revision ?? 0;
        }
        field = value;
        rotationDirtyAxes_ |= axis;
        rotationEditVersion_ = checked(rotationEditVersion_ + 1);
        switch (axis)
        {
            case RotationAxisMask.X:
                rotationXEditVersion_ = rotationEditVersion_;
                break;
            case RotationAxisMask.Y:
                rotationYEditVersion_ = rotationEditVersion_;
                break;
            case RotationAxisMask.Z:
                rotationZEditVersion_ = rotationEditVersion_;
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(axis));
        }
        OnPropertyChanged(propertyName);
    }

    private RotationAxisMask RotationAxesEditedAfter(ulong editVersion)
    {
        var result = RotationAxisMask.None;
        if (rotationXEditVersion_ > editVersion)
        {
            result |= RotationAxisMask.X;
        }
        if (rotationYEditVersion_ > editVersion)
        {
            result |= RotationAxisMask.Y;
        }
        if (rotationZEditVersion_ > editVersion)
        {
            result |= RotationAxisMask.Z;
        }
        return result;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
