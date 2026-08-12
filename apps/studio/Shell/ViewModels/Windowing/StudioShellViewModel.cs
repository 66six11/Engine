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
internal enum TransformFieldMask
{
    None = 0,
    PositionX = 1 << 0,
    PositionY = 1 << 1,
    PositionZ = 1 << 2,
    RotationX = 1 << 3,
    RotationY = 1 << 4,
    RotationZ = 1 << 5,
    ScaleX = 1 << 6,
    ScaleY = 1 << 7,
    ScaleZ = 1 << 8,
}

internal sealed record TransformInspectorText(
    string PositionX,
    string PositionY,
    string PositionZ,
    string RotationX,
    string RotationY,
    string RotationZ,
    string ScaleX,
    string ScaleY,
    string ScaleZ);

internal sealed record PendingTransformApply(
    ProjectEditId EditId,
    ProjectSessionId SessionId,
    Guid SceneId,
    Guid ObjectId,
    ulong BaseRevision,
    TransformValue SubmittedTransform,
    StudioEulerDegrees SubmittedEuler,
    TransformInspectorText SubmittedText,
    TransformFieldMask DirtyFields,
    ulong EditVersion);

internal sealed class StudioShellViewModel : INotifyPropertyChanged, IDisposable
{
    private const TransformFieldMask RotationFields =
        TransformFieldMask.RotationX |
        TransformFieldMask.RotationY |
        TransformFieldMask.RotationZ;

    private readonly IProjectSession projectSession_;
    private readonly IStudioProjectDialogService projectDialogs_;
    private readonly AsyncCommand createProjectCommand_;
    private readonly AsyncCommand openProjectCommand_;
    private readonly AsyncCommand closeProjectCommand_;
    private readonly AsyncCommand createEntityCommand_;
    private readonly AsyncCommand createMeshEntityCommand_;
    private readonly AsyncCommand saveSceneCommand_;
    private readonly AsyncCommand undoSceneCommand_;
    private readonly AsyncCommand redoSceneCommand_;
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
    private TransformValue authoritativeTransform_ = TransformValue.Identity;
    private TransformFieldMask transformDirtyFields_;
    private ulong transformEditVersion_;
    private ulong positionXEditVersion_;
    private ulong positionYEditVersion_;
    private ulong positionZEditVersion_;
    private ulong rotationXEditVersion_;
    private ulong rotationYEditVersion_;
    private ulong rotationZEditVersion_;
    private ulong scaleXEditVersion_;
    private ulong scaleYEditVersion_;
    private ulong scaleZEditVersion_;
    private ulong transformEditBaseRevision_;
    private PendingTransformApply? pendingTransformApply_;
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
        undoSceneCommand_ = new AsyncCommand(UndoSceneAsync, CanUndoDocument);
        redoSceneCommand_ = new AsyncCommand(RedoSceneAsync, CanRedoDocument);
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
            var dirty = projectSnapshot_.IsDirty ? "*" : string.Empty;
            return $"{dirty}{Path.GetFileName(document.Path)} - {project} - Asharia Studio";
        }
    }

    public bool IsStarting => stage_ == StudioShellStage.Starting;

    public bool IsWorkspaceVisible => stage_ == StudioShellStage.Ready;

    public bool HasProject => projectSnapshot_.IsReady;

    public bool HasNoProject => !projectSnapshot_.IsReady;

    public bool HasDocument => projectSnapshot_.Document is not null;

    public bool HasNoDocument => projectSnapshot_.Document is null;

    public bool IsDocumentDirty => projectSnapshot_.IsDirty;

    public string UndoSceneLabel => projectSnapshot_.UndoLabel is { } label
        ? $"Undo {label}"
        : "Undo";

    public string RedoSceneLabel => projectSnapshot_.RedoLabel is { } label
        ? $"Redo {label}"
        : "Redo";

    public bool HasSelection => SelectedEntity is not null;

    public string StartingStateText => "Starting";

    public string ProjectStateText =>
        projectSnapshot_.Project?.ProjectName ?? "No Project";

    public string ProjectPathText =>
        projectSnapshot_.Project?.RootPath ?? string.Empty;

    public string DocumentStateText => projectSnapshot_.Document is { } document
        ? $"{Path.GetFileName(document.Path)} · revision {document.Revision} · " +
          (projectSnapshot_.IsDirty ? "Unsaved" : "Saved")
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

    public string PositionX
    {
        get => positionX_;
        set => SetTransformField(
            ref positionX_, value, TransformFieldMask.PositionX, ref positionXEditVersion_);
    }

    public string PositionY
    {
        get => positionY_;
        set => SetTransformField(
            ref positionY_, value, TransformFieldMask.PositionY, ref positionYEditVersion_);
    }

    public string PositionZ
    {
        get => positionZ_;
        set => SetTransformField(
            ref positionZ_, value, TransformFieldMask.PositionZ, ref positionZEditVersion_);
    }

    public string RotationDegreesX
    {
        get => rotationDegreesX_;
        set => SetTransformField(
            ref rotationDegreesX_,
            value,
            TransformFieldMask.RotationX,
            ref rotationXEditVersion_);
    }

    public string RotationDegreesY
    {
        get => rotationDegreesY_;
        set => SetTransformField(
            ref rotationDegreesY_,
            value,
            TransformFieldMask.RotationY,
            ref rotationYEditVersion_);
    }

    public string RotationDegreesZ
    {
        get => rotationDegreesZ_;
        set => SetTransformField(
            ref rotationDegreesZ_,
            value,
            TransformFieldMask.RotationZ,
            ref rotationZEditVersion_);
    }

    public string ScaleX
    {
        get => scaleX_;
        set => SetTransformField(
            ref scaleX_, value, TransformFieldMask.ScaleX, ref scaleXEditVersion_);
    }

    public string ScaleY
    {
        get => scaleY_;
        set => SetTransformField(
            ref scaleY_, value, TransformFieldMask.ScaleY, ref scaleYEditVersion_);
    }

    public string ScaleZ
    {
        get => scaleZ_;
        set => SetTransformField(
            ref scaleZ_, value, TransformFieldMask.ScaleZ, ref scaleZEditVersion_);
    }

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
    public ICommand UndoSceneCommand => undoSceneCommand_;
    public ICommand RedoSceneCommand => redoSceneCommand_;
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

    private bool CanUndoDocument() =>
        CanEditDocument() && projectSnapshot_.CanUndo;

    private bool CanRedoDocument() =>
        CanEditDocument() && projectSnapshot_.CanRedo;

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

    private Task UndoSceneAsync() =>
        RunProjectOperationAsync(async token =>
            await projectSession_.UndoAsync(token));

    private Task RedoSceneAsync() =>
        RunProjectOperationAsync(async token =>
            await projectSession_.RedoAsync(token));

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
        var baseRevision = transformDirtyFields_ == TransformFieldMask.None
            ? document.Revision
            : transformEditBaseRevision_;
        pendingTransformApply_ = new PendingTransformApply(
            editId,
            project.SessionId,
            document.SceneId,
            selected.ObjectId,
            baseRevision,
            transform,
            submittedEuler,
            CaptureTransformInspectorText(),
            transformDirtyFields_,
            transformEditVersion_);

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
            if (!result.Succeeded && pendingTransformApply_?.EditId == editId)
            {
                pendingTransformApply_ = null;
            }
            ProjectOperationMessage = result.Message;
        }
        catch (OperationCanceledException) when (lifetimeCancellation_.IsCancellationRequested)
        {
            if (pendingTransformApply_?.EditId == editId)
            {
                pendingTransformApply_ = null;
            }
        }
        catch (Exception exception)
        {
            if (pendingTransformApply_?.EditId == editId)
            {
                pendingTransformApply_ = null;
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
        OnPropertyChanged(nameof(UndoSceneLabel));
        OnPropertyChanged(nameof(RedoSceneLabel));
        OnPropertyChanged(nameof(ProjectStateText));
        OnPropertyChanged(nameof(ProjectPathText));
        OnPropertyChanged(nameof(DocumentStateText));
        OnPropertyChanged(nameof(DocumentPathText));
        OnPropertyChanged(nameof(SceneEntities));
        RaiseProjectCommandStateChanged();
    }

    private void LoadInspector(SceneEntitySnapshot? entity)
    {
        pendingTransformApply_ = null;
        transformDirtyFields_ = TransformFieldMask.None;
        transformEditVersion_ = 0;
        positionXEditVersion_ = 0;
        positionYEditVersion_ = 0;
        positionZEditVersion_ = 0;
        rotationXEditVersion_ = 0;
        rotationYEditVersion_ = 0;
        rotationZEditVersion_ = 0;
        scaleXEditVersion_ = 0;
        scaleYEditVersion_ = 0;
        scaleZEditVersion_ = 0;
        transformEditBaseRevision_ = projectSnapshot_.Document?.Revision ?? 0;
        var transform = entity?.Transform ?? TransformValue.Identity;
        authoritativeTransform_ = transform;
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
        var previousTransform = authoritativeTransform_;
        inspectorName_ = entity.Name;

        var document = projectSnapshot_.Document;
        var project = projectSnapshot_.Project;
        var pending = pendingTransformApply_;
        var acceptsPending = pending is not null &&
            originatingEditSucceeded == true &&
            originatingEditId == pending.EditId &&
            project?.SessionId == pending.SessionId &&
            document?.SceneId == pending.SceneId &&
            entity.ObjectId == pending.ObjectId &&
            (document.Revision == pending.BaseRevision ||
             document.Revision == pending.BaseRevision + 1) &&
            TransformMatchesSubmission(transform, pending.SubmittedTransform);

        if (acceptsPending)
        {
            var editedAfterSubmit = TransformFieldsEditedAfter(pending!.EditVersion);
            RestoreSubmittedTransformText(pending.SubmittedText, editedAfterSubmit);
            rotationEulerHint_ = HintFromCurrentText(
                pending.SubmittedEuler,
                editedAfterSubmit);
            transformDirtyFields_ = editedAfterSubmit;
            ResetAppliedFieldEditVersions(editedAfterSubmit);
            transformEditBaseRevision_ = document!.Revision;
            pendingTransformApply_ = null;
        }
        else
        {
            var completesPending = pending is not null &&
                originatingEditId == pending.EditId;
            if (completesPending)
            {
                transformDirtyFields_ |= pending!.DirtyFields |
                    TransformFieldsEditedAfter(pending.EditVersion);
                pendingTransformApply_ = null;
            }

            ProjectAuthoritativeFloat(
                ref positionX_,
                previousTransform.Position.X,
                transform.Position.X,
                TransformFieldMask.PositionX);
            ProjectAuthoritativeFloat(
                ref positionY_,
                previousTransform.Position.Y,
                transform.Position.Y,
                TransformFieldMask.PositionY);
            ProjectAuthoritativeFloat(
                ref positionZ_,
                previousTransform.Position.Z,
                transform.Position.Z,
                TransformFieldMask.PositionZ);
            ProjectAuthoritativeFloat(
                ref scaleX_,
                previousTransform.Scale.X,
                transform.Scale.X,
                TransformFieldMask.ScaleX);
            ProjectAuthoritativeFloat(
                ref scaleY_,
                previousTransform.Scale.Y,
                transform.Scale.Y,
                TransformFieldMask.ScaleY);
            ProjectAuthoritativeFloat(
                ref scaleZ_,
                previousTransform.Scale.Z,
                transform.Scale.Z,
                TransformFieldMask.ScaleZ);

            var sameOrientation = StudioEulerRotation.AreEquivalent(
                previousTransform.Rotation,
                transform.Rotation);
            var rotationDirtyFields = transformDirtyFields_ & RotationFields;
            if (!sameOrientation && rotationDirtyFields != TransformFieldMask.None)
            {
                MergeAuthoritativeRotationIntoDraft(transform.Rotation);
            }
            else if (!sameOrientation &&
                     StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
                         transform.Rotation,
                         rotationEulerHint_,
                         out var closest))
            {
                rotationEulerHint_ = closest;
                SetRotationTextFromHint();
            }

            transformEditBaseRevision_ = document?.Revision ?? 0;
        }

        authoritativeTransform_ = transform;

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
                HintFromCurrentText(rotationEulerHint_, transformDirtyFields_),
                out var closest))
        {
            return;
        }

        if ((transformDirtyFields_ & TransformFieldMask.RotationX) == 0)
        {
            rotationDegreesX_ = Format(closest.X);
        }
        if ((transformDirtyFields_ & TransformFieldMask.RotationY) == 0)
        {
            rotationDegreesY_ = Format(closest.Y);
        }
        if ((transformDirtyFields_ & TransformFieldMask.RotationZ) == 0)
        {
            rotationDegreesZ_ = Format(closest.Z);
        }
        rotationEulerHint_ = HintFromCurrentText(closest, transformDirtyFields_);
    }

    private StudioEulerDegrees HintFromCurrentText(
        StudioEulerDegrees fallback,
        TransformFieldMask textFields) =>
        new(
            (textFields & TransformFieldMask.RotationX) != 0 &&
                TryParseDouble(rotationDegreesX_, out var x)
                    ? x
                    : fallback.X,
            (textFields & TransformFieldMask.RotationY) != 0 &&
                TryParseDouble(rotationDegreesY_, out var y)
                    ? y
                    : fallback.Y,
            (textFields & TransformFieldMask.RotationZ) != 0 &&
                TryParseDouble(rotationDegreesZ_, out var z)
                    ? z
                    : fallback.Z);

    private TransformInspectorText CaptureTransformInspectorText() =>
        new(
            positionX_,
            positionY_,
            positionZ_,
            rotationDegreesX_,
            rotationDegreesY_,
            rotationDegreesZ_,
            scaleX_,
            scaleY_,
            scaleZ_);

    private void RestoreSubmittedTransformText(
        TransformInspectorText submitted,
        TransformFieldMask editedAfterSubmit)
    {
        RestoreSubmittedText(
            ref positionX_, submitted.PositionX, editedAfterSubmit, TransformFieldMask.PositionX);
        RestoreSubmittedText(
            ref positionY_, submitted.PositionY, editedAfterSubmit, TransformFieldMask.PositionY);
        RestoreSubmittedText(
            ref positionZ_, submitted.PositionZ, editedAfterSubmit, TransformFieldMask.PositionZ);
        RestoreSubmittedText(
            ref rotationDegreesX_,
            submitted.RotationX,
            editedAfterSubmit,
            TransformFieldMask.RotationX);
        RestoreSubmittedText(
            ref rotationDegreesY_,
            submitted.RotationY,
            editedAfterSubmit,
            TransformFieldMask.RotationY);
        RestoreSubmittedText(
            ref rotationDegreesZ_,
            submitted.RotationZ,
            editedAfterSubmit,
            TransformFieldMask.RotationZ);
        RestoreSubmittedText(
            ref scaleX_, submitted.ScaleX, editedAfterSubmit, TransformFieldMask.ScaleX);
        RestoreSubmittedText(
            ref scaleY_, submitted.ScaleY, editedAfterSubmit, TransformFieldMask.ScaleY);
        RestoreSubmittedText(
            ref scaleZ_, submitted.ScaleZ, editedAfterSubmit, TransformFieldMask.ScaleZ);
    }

    private static void RestoreSubmittedText(
        ref string current,
        string submitted,
        TransformFieldMask editedAfterSubmit,
        TransformFieldMask field)
    {
        if ((editedAfterSubmit & field) == 0)
        {
            current = submitted;
        }
    }

    private void ProjectAuthoritativeFloat(
        ref string currentText,
        float previous,
        float next,
        TransformFieldMask field)
    {
        if ((transformDirtyFields_ & field) == 0 && previous != next)
        {
            currentText = Format(next);
        }
    }

    private static bool TransformMatchesSubmission(
        TransformValue authoritative,
        TransformValue submitted) =>
        authoritative.Position == submitted.Position &&
        authoritative.Scale == submitted.Scale &&
        StudioEulerRotation.AreEquivalent(
            authoritative.Rotation,
            submitted.Rotation);

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
        undoSceneCommand_.RaiseCanExecuteChanged();
        redoSceneCommand_.RaiseCanExecuteChanged();
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

    private void SetTransformField(
        ref string field,
        string value,
        TransformFieldMask transformField,
        ref ulong fieldEditVersion,
        [CallerMemberName] string? propertyName = null)
    {
        if (string.Equals(field, value, StringComparison.Ordinal))
        {
            return;
        }
        if (transformDirtyFields_ == TransformFieldMask.None)
        {
            transformEditBaseRevision_ = projectSnapshot_.Document?.Revision ?? 0;
        }
        field = value;
        transformDirtyFields_ |= transformField;
        transformEditVersion_ = checked(transformEditVersion_ + 1);
        fieldEditVersion = transformEditVersion_;
        OnPropertyChanged(propertyName);
    }

    private TransformFieldMask TransformFieldsEditedAfter(ulong editVersion)
    {
        var result = TransformFieldMask.None;
        if (positionXEditVersion_ > editVersion)
        {
            result |= TransformFieldMask.PositionX;
        }
        if (positionYEditVersion_ > editVersion)
        {
            result |= TransformFieldMask.PositionY;
        }
        if (positionZEditVersion_ > editVersion)
        {
            result |= TransformFieldMask.PositionZ;
        }
        if (rotationXEditVersion_ > editVersion)
        {
            result |= TransformFieldMask.RotationX;
        }
        if (rotationYEditVersion_ > editVersion)
        {
            result |= TransformFieldMask.RotationY;
        }
        if (rotationZEditVersion_ > editVersion)
        {
            result |= TransformFieldMask.RotationZ;
        }
        if (scaleXEditVersion_ > editVersion)
        {
            result |= TransformFieldMask.ScaleX;
        }
        if (scaleYEditVersion_ > editVersion)
        {
            result |= TransformFieldMask.ScaleY;
        }
        if (scaleZEditVersion_ > editVersion)
        {
            result |= TransformFieldMask.ScaleZ;
        }
        return result;
    }

    private void ResetAppliedFieldEditVersions(TransformFieldMask editedAfterSubmit)
    {
        ResetAppliedFieldEditVersion(
            ref positionXEditVersion_, editedAfterSubmit, TransformFieldMask.PositionX);
        ResetAppliedFieldEditVersion(
            ref positionYEditVersion_, editedAfterSubmit, TransformFieldMask.PositionY);
        ResetAppliedFieldEditVersion(
            ref positionZEditVersion_, editedAfterSubmit, TransformFieldMask.PositionZ);
        ResetAppliedFieldEditVersion(
            ref rotationXEditVersion_, editedAfterSubmit, TransformFieldMask.RotationX);
        ResetAppliedFieldEditVersion(
            ref rotationYEditVersion_, editedAfterSubmit, TransformFieldMask.RotationY);
        ResetAppliedFieldEditVersion(
            ref rotationZEditVersion_, editedAfterSubmit, TransformFieldMask.RotationZ);
        ResetAppliedFieldEditVersion(
            ref scaleXEditVersion_, editedAfterSubmit, TransformFieldMask.ScaleX);
        ResetAppliedFieldEditVersion(
            ref scaleYEditVersion_, editedAfterSubmit, TransformFieldMask.ScaleY);
        ResetAppliedFieldEditVersion(
            ref scaleZEditVersion_, editedAfterSubmit, TransformFieldMask.ScaleZ);
    }

    private static void ResetAppliedFieldEditVersion(
        ref ulong fieldEditVersion,
        TransformFieldMask editedAfterSubmit,
        TransformFieldMask field)
    {
        if ((editedAfterSubmit & field) == 0)
        {
            fieldEditVersion = 0;
        }
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
