using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using Asharia.Runtime;
using Asharia.Studio.Application.Actions;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Threading;
using Editor.Shell.Actions;
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

internal sealed record ProjectActionExecution(
    ProjectSessionOperationResult? Result,
    StudioActionCompletion Completion);

internal readonly record struct StudioActionCommandProjectionKey(
    StudioActionId ActionId,
    StudioActionInvocationSource Source,
    StudioPresentationId TopLevelId,
    StudioPresentationId? FocusedPanelId);

internal sealed class StudioShellViewModel : INotifyPropertyChanged, IDisposable
{
    private const string UnclassifiedActionFailureMessage =
        "The Studio action failed unexpectedly.";
    private const TransformFieldMask RotationFields =
        TransformFieldMask.RotationX |
        TransformFieldMask.RotationY |
        TransformFieldMask.RotationZ;

    private readonly IProjectSession projectSession_;
    private readonly IStudioProjectDialogService projectDialogs_;
    private readonly ProjectDocumentTransitionCoordinator documentTransitions_;
    private readonly StudioOperationDiagnosticWriter diagnostics_;
    private readonly StudioActionRegistry actionRegistry_ = new();
    private readonly StudioActionExecutor actionExecutor_;
    private readonly Dictionary<StudioActionId, StudioActionCommand> actionCommands_ = [];
    private readonly Dictionary<StudioActionCommandProjectionKey, StudioActionCommand>
        projectedActionCommands_ = [];
    private readonly HashSet<StudioActionId> shortcutActionsInFlight_ = [];
    private readonly EditorDockWorkspaceViewModel dockWorkspace_;
    private readonly ViewportPresentationLifetime viewportPresentationLifetime_;
    private readonly CancellationTokenSource lifetimeCancellation_ = new();
    private StudioShellStage stage_ = StudioShellStage.Starting;
    private ProjectSessionSnapshot projectSnapshot_;
    private ProjectSessionSnapshot? lastOperationResultSnapshot_;
    private ProjectSessionSnapshot? sessionSnapshotAtLastOperationResult_;
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
        IStudioProjectDialogService projectDialogs,
        ProjectDocumentTransitionCoordinator documentTransitions,
        StudioOperationDiagnosticWriter diagnostics)
    {
        ArgumentNullException.ThrowIfNull(projectSession);
        ArgumentNullException.ThrowIfNull(projectDialogs);
        ArgumentNullException.ThrowIfNull(documentTransitions);
        ArgumentNullException.ThrowIfNull(diagnostics);
        projectSession_ = projectSession;
        projectDialogs_ = projectDialogs;
        documentTransitions_ = documentTransitions;
        diagnostics_ = diagnostics;
        actionExecutor_ = new StudioActionExecutor(actionRegistry_);
        viewportPresentationLifetime_ = new ViewportPresentationLifetime();
        projectSnapshot_ = projectSession.Current;
        dockWorkspace_ = CreateDockWorkspace();
        RegisterActions();
        CreateActionCommands();
        dockWorkspace_.DockContentChanged += OnDockContentChanged;
        projectSession_.SnapshotChanged += OnProjectSnapshotChanged;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public event EventHandler? ActionStateChanged;

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
            RaiseActionStateChanged(StudioShellActionIds.CreateProject);
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

    internal void PresentProjectOperationMessage(string message)
    {
        ThrowIfDisposed();
        ProjectOperationMessage = message ?? string.Empty;
    }

    public ICommand CreateProjectCommand => GetActionCommand(StudioShellActionIds.CreateProject);
    public ICommand OpenProjectCommand => GetActionCommand(StudioShellActionIds.OpenProject);
    public ICommand CloseProjectCommand => GetActionCommand(StudioShellActionIds.CloseProject);
    public ICommand CreateEntityCommand => GetActionCommand(StudioShellActionIds.CreateEntity);
    public ICommand CreateMeshEntityCommand =>
        GetActionCommand(StudioShellActionIds.CreateMeshEntity);
    public ICommand SaveSceneCommand => GetActionCommand(StudioShellActionIds.SaveScene);
    public ICommand UndoSceneCommand => GetActionCommand(StudioShellActionIds.UndoScene);
    public ICommand RedoSceneCommand => GetActionCommand(StudioShellActionIds.RedoScene);
    public ICommand ApplyEntityNameCommand =>
        GetActionCommand(StudioShellActionIds.ApplyEntityName);
    public ICommand ApplyEntityTransformCommand =>
        GetActionCommand(StudioShellActionIds.ApplyEntityTransform);
    public ICommand OpenHierarchyPanelCommand =>
        GetActionCommand(StudioShellActionIds.OpenHierarchyPanel);
    public ICommand OpenProjectPanelCommand =>
        GetActionCommand(StudioShellActionIds.OpenProjectPanel);
    public ICommand OpenSceneViewPanelCommand =>
        GetActionCommand(StudioShellActionIds.OpenSceneViewPanel);
    public ICommand OpenInspectorPanelCommand =>
        GetActionCommand(StudioShellActionIds.OpenInspectorPanel);
    public ImmutableArray<StudioActionCatalogEntry> ActionCatalog =>
        actionRegistry_.GetActions();
    public EditorDockWorkspaceViewModel DockWorkspace => dockWorkspace_;

    internal IProjectSession ProjectSession => projectSession_;

    internal ProjectSessionSnapshot AppliedProjectSnapshot => projectSnapshot_;

    internal ViewportPresentationLifetime ViewportPresentationLifetime =>
        viewportPresentationLifetime_;

    public ICommand GetActionCommand(StudioActionId actionId)
    {
        if (!actionId.IsValid)
        {
            throw new ArgumentException("Action id must be valid.", nameof(actionId));
        }

        return actionCommands_.TryGetValue(actionId, out var command)
            ? command
            : throw new KeyNotFoundException(
                $"Studio action '{actionId}' is not registered.");
    }

    public ICommand GetActionCommand(
        StudioActionId actionId,
        StudioActionInvocationSource source,
        StudioPresentationId topLevelId,
        StudioPresentationId? focusedPanelId = null)
    {
        if (!actionId.IsValid)
        {
            throw new ArgumentException("Action id must be valid.", nameof(actionId));
        }
        if (!topLevelId.IsValid)
        {
            throw new ArgumentException("Top-level id must be valid.", nameof(topLevelId));
        }

        if (focusedPanelId is StudioPresentationId panelId && !panelId.IsValid)
        {
            throw new ArgumentException("Focused panel id must be valid.", nameof(focusedPanelId));
        }

        var key = new StudioActionCommandProjectionKey(
            actionId,
            source,
            topLevelId,
            focusedPanelId);
        if (!projectedActionCommands_.TryGetValue(key, out var command))
        {
            command = new StudioActionCommand(
                actionId,
                source,
                topLevelId,
                (id, invocationSource, presentationId) => CaptureContextForAction(
                    id,
                    invocationSource,
                    presentationId,
                    focusedPanelId),
                EvaluateAction,
                (id, context) => ExecuteActionAsync(id, context),
                () => !isDisposed_);
            projectedActionCommands_.Add(key, command);
        }
        return command;
    }

    public StudioActionContextSnapshot CaptureActionContext(
        StudioActionInvocationSource source,
        StudioPresentationId? topLevelId,
        StudioActionTarget? target = null,
        StudioPresentationId? focusedPanelId = null)
    {
        ThrowIfDisposed();
        var project = projectSnapshot_.Project;
        var document = projectSnapshot_.Document;
        var selection = selectedEntity_ is { } selected
            ? new StudioActionSelectionSnapshot([selected.ObjectId], selected.ObjectId)
            : StudioActionSelectionSnapshot.Empty;
        var frozenTarget = target ?? (document is not null && project is not null
            ? StudioActionTarget.Scene(project.SessionId, document.SceneId)
            : project is not null
                ? StudioActionTarget.Project(project.SessionId)
                : StudioActionTarget.None);
        if (focusedPanelId is StudioPresentationId panelId && !panelId.IsValid)
        {
            throw new ArgumentException("Focused panel id must be valid.", nameof(focusedPanelId));
        }
        var capturedFocusedPanelId = focusedPanelId ?? ActivePanelId(dockWorkspace_);
        return new StudioActionContextSnapshot(
            source,
            topLevelId,
            capturedFocusedPanelId,
            project?.SessionId,
            document?.SceneId,
            document?.Revision,
            selection,
            frozenTarget,
            Guid.NewGuid(),
            Guid.NewGuid());
    }

    public StudioActionStateEvaluation EvaluateAction(
        StudioActionId actionId,
        StudioActionContextSnapshot context) =>
        actionExecutor_.EvaluateState(actionId, context);

    public async ValueTask<StudioActionResult> ExecuteActionAsync(
        StudioActionId actionId,
        StudioActionContextSnapshot context,
        CancellationToken cancellationToken = default)
    {
        ThrowIfDisposed();
        StudioActionResult result;
        if (!cancellationToken.CanBeCanceled)
        {
            result = await actionExecutor_.ExecuteAsync(
                actionId,
                context,
                lifetimeCancellation_.Token);
        }
        else
        {
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken,
                lifetimeCancellation_.Token);
            result = await actionExecutor_.ExecuteAsync(actionId, context, linked.Token);
        }

        if (result.Status == StudioActionResultStatus.Failed &&
            result.DiagnosticSequence is null)
        {
            ProjectOperationMessage = UnclassifiedActionFailureMessage;
            var diagnostic = diagnostics_.PublishUnclassifiedActionFailure(
                actionId,
                UnclassifiedActionFailureMessage,
                DiagnosticContext(actionId, context));
            return result.WithDiagnosticSequence(diagnostic.SequenceId);
        }
        return result;
    }

    public ValueTask<StudioActionResult> ExecuteActionAsync(
        StudioActionId actionId,
        StudioActionInvocationSource source,
        StudioPresentationId? topLevelId,
        StudioActionTarget? target = null,
        StudioPresentationId? focusedPanelId = null,
        CancellationToken cancellationToken = default) =>
        ExecuteActionAsync(
            actionId,
            CaptureActionContext(source, topLevelId, target, focusedPanelId),
            cancellationToken);

    public bool TryExecuteShortcut(
        StudioShortcutChord chord,
        StudioPresentationId topLevelId,
        StudioPresentationId? focusedPanelId = null)
    {
        ArgumentNullException.ThrowIfNull(chord);
        if (!topLevelId.IsValid ||
            !actionRegistry_.TryResolveShortcut(chord, out var actionId))
        {
            return false;
        }

        var context = CaptureContextForAction(
            actionId,
            StudioActionInvocationSource.Shortcut,
            topLevelId,
            focusedPanelId);
        var evaluation = EvaluateAction(actionId, context);
        if (evaluation.Status != StudioActionStateEvaluationStatus.Evaluated ||
            evaluation.State is not { IsVisible: true, IsEnabled: true, IsRunning: false })
        {
            return false;
        }

        if (!shortcutActionsInFlight_.Add(actionId))
        {
            return false;
        }
        RaiseActionStateChanged(actionId);
        var execution = ExecuteShortcutAsync(actionId, context);
        if (!execution.IsCompleted)
        {
            IsProjectOperationRunning = true;
        }
        return true;
    }

    private async Task ExecuteShortcutAsync(
        StudioActionId actionId,
        StudioActionContextSnapshot context)
    {
        try
        {
            await ExecuteActionAsync(actionId, context);
        }
        catch (ObjectDisposedException) when (isDisposed_)
        {
        }
        finally
        {
            shortcutActionsInFlight_.Remove(actionId);
            if (!isDisposed_)
            {
                IsProjectOperationRunning = false;
            }
            RaiseActionStateChanged(actionId);
        }
    }

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
        dockWorkspace_.DockContentChanged -= OnDockContentChanged;
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

    private void RegisterActions()
    {
        RegisterAction(
            StudioShellActionIds.CreateProject,
            "Create Project",
            "Create a project and replace the active document after dirty-state resolution.",
            "File",
            EvaluateCreateProject,
            HandleCreateProjectAsync,
            Menu(StudioShellActionIds.CreateProject, "File/Create Project", "create", 10010),
            Toolbar(StudioShellActionIds.CreateProject, "Main/Create Project", "project", 10),
            Shortcut(
                StudioShellActionIds.CreateProject,
                "N",
                StudioShortcutModifiers.Control,
                10));
        RegisterAction(
            StudioShellActionIds.OpenProject,
            "Open Project",
            "Open a project after dirty-state resolution.",
            "File",
            EvaluateRunProjectOperation,
            HandleOpenProjectAsync,
            Menu(StudioShellActionIds.OpenProject, "File/Open Project", "open", 10020),
            Toolbar(StudioShellActionIds.OpenProject, "Main/Open Project", "project", 20),
            Shortcut(
                StudioShellActionIds.OpenProject,
                "O",
                StudioShortcutModifiers.Control,
                20));
        RegisterAction(
            StudioShellActionIds.CloseProject,
            "Close Project",
            "Close the active project after dirty-state resolution.",
            "File",
            context => EvaluateDocumentAction(context, CanEditDocument),
            HandleCloseProjectAsync,
            Menu(StudioShellActionIds.CloseProject, "File/Close Project", "close", 10030),
            Toolbar(StudioShellActionIds.CloseProject, "Main/Close Project", "project", 30),
            Shortcut(
                StudioShellActionIds.CloseProject,
                "W",
                StudioShortcutModifiers.Control,
                30));
        RegisterAction(
            StudioShellActionIds.SaveScene,
            "Save Scene",
            "Save the active scene document.",
            "File",
            context => EvaluateDocumentAction(context, CanSaveDocument),
            (context, token) => ExecuteProjectActionAsync(
                StudioShellActionIds.SaveScene,
                context,
                token,
                projectSession_.SaveSceneAsync),
            Menu(StudioShellActionIds.SaveScene, "File/Save Scene", "save", 10040),
            Toolbar(StudioShellActionIds.SaveScene, "Main/Save Scene", "project", 40),
            Shortcut(
                StudioShellActionIds.SaveScene,
                "S",
                StudioShortcutModifiers.Control,
                40));
        RegisterAction(
            StudioShellActionIds.UndoScene,
            "Undo",
            "Undo the latest persistent scene edit.",
            "Edit",
            context => EvaluateHistoryAction(context, CanUndoDocument, UndoSceneLabel),
            (context, token) => ExecuteProjectActionAsync(
                StudioShellActionIds.UndoScene,
                context,
                token,
                projectSession_.UndoAsync),
            Menu(StudioShellActionIds.UndoScene, "Edit/Undo", "history", 20010),
            Toolbar(StudioShellActionIds.UndoScene, "Main/Undo", "history", 10),
            Shortcut(
                StudioShellActionIds.UndoScene,
                "Z",
                StudioShortcutModifiers.Control,
                10));
        RegisterAction(
            StudioShellActionIds.RedoScene,
            "Redo",
            "Redo the next persistent scene edit.",
            "Edit",
            context => EvaluateHistoryAction(context, CanRedoDocument, RedoSceneLabel),
            (context, token) => ExecuteProjectActionAsync(
                StudioShellActionIds.RedoScene,
                context,
                token,
                projectSession_.RedoAsync),
            Menu(StudioShellActionIds.RedoScene, "Edit/Redo", "history", 20020),
            Toolbar(StudioShellActionIds.RedoScene, "Main/Redo", "history", 20),
            Shortcut(
                StudioShellActionIds.RedoScene,
                "Y",
                StudioShortcutModifiers.Control,
                20),
            Shortcut(
                StudioShellActionIds.RedoScene,
                "Z",
                StudioShortcutModifiers.Control | StudioShortcutModifiers.Shift,
                21,
                "alternate"));
        RegisterAction(
            StudioShellActionIds.CreateEntity,
            "Create Entity",
            "Create an entity in the active scene.",
            "Scene",
            EvaluateSceneCreation,
            HandleCreateEntityAsync,
            Menu(StudioShellActionIds.CreateEntity, "Scene/Create Entity", "create", 30010),
            Toolbar(StudioShellActionIds.CreateEntity, "Main/Create Entity", "create", 10),
            ContextMenu(
                StudioShellActionIds.CreateEntity,
                "Hierarchy/Create Entity",
                "create",
                10));
        RegisterAction(
            StudioShellActionIds.CreateMeshEntity,
            "Create Mesh Entity",
            "Create the validation mesh entity in the active scene.",
            "Scene",
            EvaluateSceneCreation,
            HandleCreateMeshEntityAsync,
            Menu(
                StudioShellActionIds.CreateMeshEntity,
                "Scene/Create Mesh Entity",
                "create",
                30020),
            Toolbar(
                StudioShellActionIds.CreateMeshEntity,
                "Main/Create Mesh Entity",
                "create",
                20),
            ContextMenu(
                StudioShellActionIds.CreateMeshEntity,
                "Hierarchy/Create Mesh Entity",
                "create",
                20));
        RegisterAction(
            StudioShellActionIds.ApplyEntityName,
            "Apply Entity Name",
            "Apply the inspector name to the frozen selected entity.",
            "Scene",
            EvaluateSelectionAction,
            HandleApplyEntityNameAsync,
            Menu(
                StudioShellActionIds.ApplyEntityName,
                "Scene/Apply Entity Name",
                "apply",
                30030),
            Toolbar(
                StudioShellActionIds.ApplyEntityName,
                "Inspector/Apply Name",
                "apply",
                30));
        RegisterAction(
            StudioShellActionIds.ApplyEntityTransform,
            "Apply Entity Transform",
            "Apply the inspector transform to the frozen selected entity.",
            "Scene",
            EvaluateSelectionAction,
            HandleApplyEntityTransformAsync,
            Menu(
                StudioShellActionIds.ApplyEntityTransform,
                "Scene/Apply Entity Transform",
                "apply",
                30040),
            Toolbar(
                StudioShellActionIds.ApplyEntityTransform,
                "Inspector/Apply Transform",
                "apply",
                40));

        RegisterPanelAction(
            StudioShellActionIds.OpenHierarchyPanel,
            "Hierarchy",
            "hierarchy",
            order: 40010);
        RegisterPanelAction(
            StudioShellActionIds.OpenProjectPanel,
            "Project",
            "project",
            order: 40020);
        RegisterPanelAction(
            StudioShellActionIds.OpenSceneViewPanel,
            "Scene Document",
            "scene-view",
            order: 40030);
        RegisterPanelAction(
            StudioShellActionIds.OpenInspectorPanel,
            "Inspector",
            "inspector",
            order: 40040);
    }

    private void RegisterPanelAction(
        StudioActionId actionId,
        string label,
        string panelId,
        int order)
    {
        var stablePanelId = new StudioPresentationId(panelId);
        RegisterAction(
            actionId,
            $"Open {label}",
            $"Open or activate the {label} panel.",
            "Window",
            context => EvaluatePanelAction(context, stablePanelId),
            (context, _) => HandleOpenPanelAsync(actionId, stablePanelId, context),
            Menu(actionId, $"Window/Panels/{label}", "panels", order));
    }

    private void RegisterAction(
        StudioActionId actionId,
        string label,
        string description,
        string category,
        StudioActionStateEvaluator stateEvaluator,
        StudioActionHandler handler,
        params StudioActionPlacement[] placements) =>
        actionRegistry_.Register(
            new StudioActionDefinition(actionId, label, description, category),
            placements,
            stateEvaluator,
            handler);

    private void CreateActionCommands()
    {
        foreach (var entry in actionRegistry_.GetActions())
        {
            var actionId = entry.Definition.Id;
            actionCommands_.Add(
                actionId,
                new StudioActionCommand(
                    actionId,
                    id => CaptureContextForAction(
                        id,
                        StudioActionInvocationSource.Toolbar,
                        StudioShellPresentationIds.MainWindow),
                    EvaluateAction,
                    (id, context) => ExecuteActionAsync(id, context),
                    () => !isDisposed_));
        }
    }

    private StudioActionContextSnapshot CaptureContextForAction(
        StudioActionId actionId,
        StudioActionInvocationSource source,
        StudioPresentationId topLevelId,
        StudioPresentationId? focusedPanelId = null)
    {
        var target = actionId == StudioShellActionIds.ApplyEntityName ||
            actionId == StudioShellActionIds.ApplyEntityTransform
                ? CurrentSelectionTarget()
                : IsPanelAction(actionId)
                    ? PanelTarget(actionId)
                    : null;
        return CaptureActionContext(source, topLevelId, target, focusedPanelId);
    }

    internal static StudioPresentationId? ActivePanelId(
        EditorDockWorkspaceViewModel workspace)
    {
        ArgumentNullException.ThrowIfNull(workspace);
        var activePanelId = workspace.ActiveWindow?.ActiveTab?.Id;
        return activePanelId is null
            ? null
            : new StudioPresentationId(activePanelId);
    }

    private StudioActionTarget? CurrentSelectionTarget()
    {
        var project = projectSnapshot_.Project;
        var document = projectSnapshot_.Document;
        var selected = selectedEntity_;
        return project is null || document is null || selected is null
            ? null
            : StudioActionTarget.SceneObject(
                project.SessionId,
                document.SceneId,
                selected.ObjectId);
    }

    private static bool IsPanelAction(StudioActionId actionId) =>
        actionId == StudioShellActionIds.OpenHierarchyPanel ||
        actionId == StudioShellActionIds.OpenProjectPanel ||
        actionId == StudioShellActionIds.OpenSceneViewPanel ||
        actionId == StudioShellActionIds.OpenInspectorPanel;

    private static StudioActionTarget PanelTarget(StudioActionId actionId) =>
        StudioActionTarget.Panel(new StudioPresentationId(actionId switch
        {
            var id when id == StudioShellActionIds.OpenHierarchyPanel => "hierarchy",
            var id when id == StudioShellActionIds.OpenProjectPanel => "project",
            var id when id == StudioShellActionIds.OpenSceneViewPanel => "scene-view",
            var id when id == StudioShellActionIds.OpenInspectorPanel => "inspector",
            _ => throw new ArgumentOutOfRangeException(nameof(actionId)),
        }));

    private StudioActionState EvaluateCreateProject(StudioActionContextSnapshot context) =>
        EvaluateWorkspaceAction(
            context,
            CanCreateProject,
            "Enter a project name before creating a project.");

    private StudioActionState EvaluateRunProjectOperation(
        StudioActionContextSnapshot context) =>
        EvaluateWorkspaceAction(
            context,
            CanRunProjectOperation,
            "The Studio workspace is not ready for project operations.");

    private StudioActionState EvaluateDocumentAction(
        StudioActionContextSnapshot context,
        Func<bool> canExecute)
    {
        if (!ScopeMatchesCurrentDocument(context))
        {
            return StudioActionState.Blocked(
                StudioActionBlockKind.Stale,
                "The action was captured for an older project or scene revision.");
        }

        return EvaluateWorkspaceAction(
            context,
            canExecute,
            "The active scene is not available for this action.");
    }

    private StudioActionState EvaluateHistoryAction(
        StudioActionContextSnapshot context,
        Func<bool> canExecute,
        string presentationLabel)
    {
        var state = EvaluateDocumentAction(context, canExecute);
        return new StudioActionState(
            state.IsVisible,
            state.BlockKind,
            state.CheckState,
            state.IsRunning,
            state.DisabledReason,
            presentationLabel);
    }

    private StudioActionState EvaluateSceneCreation(StudioActionContextSnapshot context)
    {
        var documentState = EvaluateDocumentAction(context, CanEditDocument);
        if (!documentState.IsEnabled)
        {
            return documentState;
        }
        if (context.Target.Kind is not StudioActionTargetKind.Scene and
            not StudioActionTargetKind.SceneObject)
        {
            return StudioActionState.Blocked(
                StudioActionBlockKind.Disabled,
                "Scene creation requires an explicit scene target.");
        }
        if (context.Target.Kind == StudioActionTargetKind.SceneObject &&
            (context.Target.ObjectId is not Guid targetObjectId ||
             !projectSnapshot_.Document!.Entities.Any(
                 entity => entity.ObjectId == targetObjectId)))
        {
            return StudioActionState.Blocked(
                StudioActionBlockKind.Stale,
                "The target entity changed after the action context was captured.");
        }

        return StudioActionState.Available(isRunning: IsProjectOperationRunning);
    }

    private StudioActionState EvaluateSelectionAction(StudioActionContextSnapshot context)
    {
        var documentState = EvaluateDocumentAction(context, CanEditSelection);
        if (!documentState.IsEnabled)
        {
            return documentState;
        }
        var targetObjectId = context.Target.ObjectId;
        if (context.Target.Kind != StudioActionTargetKind.SceneObject ||
            targetObjectId is null || selectedEntity_?.ObjectId != targetObjectId)
        {
            return StudioActionState.Blocked(
                StudioActionBlockKind.Stale,
                "The selected entity changed after the action context was captured.");
        }

        return StudioActionState.Available(isRunning: IsProjectOperationRunning);
    }

    private StudioActionState EvaluatePanelAction(
        StudioActionContextSnapshot context,
        StudioPresentationId panelId)
    {
        if (!CanRunProjectOperation() ||
            context.Target.PanelId != panelId ||
            !dockWorkspace_.CanOpenPanel(panelId.Value))
        {
            return StudioActionState.Blocked(
                StudioActionBlockKind.Disabled,
                $"Panel '{panelId}' cannot be opened in the current workspace.");
        }

        return StudioActionState.Available();
    }

    private StudioActionState EvaluateWorkspaceAction(
        StudioActionContextSnapshot context,
        Func<bool> canExecute,
        string disabledReason)
    {
        if (isDisposed_ || stage_ != StudioShellStage.Ready)
        {
            return StudioActionState.Blocked(
                StudioActionBlockKind.Disabled,
                "The Studio workspace is not ready.");
        }
        var current = CurrentActionSnapshot();
        if (context.ProjectSessionId != current.Project?.SessionId)
        {
            return StudioActionState.Blocked(
                StudioActionBlockKind.Stale,
                "The active project changed after the action context was captured.");
        }
        if (!canExecute())
        {
            return StudioActionState.Blocked(
                StudioActionBlockKind.Disabled,
                disabledReason,
                isRunning: IsProjectOperationRunning);
        }

        return StudioActionState.Available(isRunning: IsProjectOperationRunning);
    }

    private bool ScopeMatchesCurrentDocument(StudioActionContextSnapshot context) =>
        context.ProjectSessionId == CurrentActionSnapshot().Project?.SessionId &&
        context.SceneId == CurrentActionSnapshot().Document?.SceneId &&
        context.DocumentRevision == CurrentActionSnapshot().Document?.Revision;

    private ProjectSessionSnapshot CurrentActionSnapshot()
    {
        var sessionSnapshot = projectSession_.Current;
        return lastOperationResultSnapshot_ == projectSnapshot_ &&
            sessionSnapshotAtLastOperationResult_ == sessionSnapshot
                ? lastOperationResultSnapshot_
                : sessionSnapshot;
    }

    private void RememberOperationResult(ProjectSessionSnapshot resultSnapshot)
    {
        lastOperationResultSnapshot_ = resultSnapshot;
        sessionSnapshotAtLastOperationResult_ = projectSession_.Current;
    }

    private async ValueTask<StudioActionCompletion> HandleCreateProjectAsync(
        StudioActionContextSnapshot context,
        CancellationToken cancellationToken)
    {
        var parent = await projectDialogs_.SelectProjectParentDirectoryAsync(
            cancellationToken);
        if (parent is null)
        {
            return StudioActionCompletion.Cancelled("Project creation was cancelled.");
        }

        return await ExecuteDocumentTransitionActionAsync(
            StudioShellActionIds.CreateProject,
            context,
            cancellationToken,
            ProjectDocumentTransitionKind.CreateProject,
            (expectation, token) => ExecuteWithPresentationDrainAsync(
                () => projectSession_.CreateProjectAsync(
                    parent,
                    NewProjectName,
                    expectation,
                    token)));
    }

    private async ValueTask<StudioActionCompletion> HandleOpenProjectAsync(
        StudioActionContextSnapshot context,
        CancellationToken cancellationToken)
    {
        var descriptor = await projectDialogs_.SelectProjectDescriptorAsync(
            cancellationToken);
        if (descriptor is null)
        {
            return StudioActionCompletion.Cancelled("Project opening was cancelled.");
        }

        return await ExecuteDocumentTransitionActionAsync(
            StudioShellActionIds.OpenProject,
            context,
            cancellationToken,
            ProjectDocumentTransitionKind.OpenProject,
            (expectation, token) => ExecuteWithPresentationDrainAsync(
                () => projectSession_.OpenProjectAsync(
                    descriptor,
                    expectation,
                    token)));
    }

    private ValueTask<StudioActionCompletion> HandleCloseProjectAsync(
        StudioActionContextSnapshot context,
        CancellationToken cancellationToken) =>
        ExecuteDocumentTransitionActionAsync(
            StudioShellActionIds.CloseProject,
            context,
            cancellationToken,
            ProjectDocumentTransitionKind.CloseProject,
            (expectation, token) => ExecuteWithPresentationDrainAsync(
                () => projectSession_.CloseProjectAsync(expectation, token)));

    private async ValueTask<StudioActionCompletion> ExecuteDocumentTransitionActionAsync(
        StudioActionId actionId,
        StudioActionContextSnapshot context,
        CancellationToken cancellationToken,
        ProjectDocumentTransitionKind transitionKind,
        Func<ProjectDocumentTransitionExpectation, CancellationToken,
            ValueTask<ProjectSessionOperationResult>> transition)
    {
        ProjectOperationMessage = string.Empty;
        IsProjectOperationRunning = true;
        try
        {
            var result = await documentTransitions_.ExecuteAsync(
                transitionKind,
                transition,
                cancellationToken);
            if (result.ProjectOperation is { } operation)
            {
                ApplyProjectSnapshot(operation.Current);
                RememberOperationResult(operation.Current);
            }
            ProjectOperationMessage = result.Status ==
                ProjectDocumentTransitionStatus.Cancelled
                    ? string.Empty
                    : result.Message;
            var diagnostic = diagnostics_.PublishDocumentTransitionFailure(
                result,
                DiagnosticContext(actionId, context));
            return result.Status switch
            {
                ProjectDocumentTransitionStatus.Completed =>
                    StudioActionCompletion.Succeeded(result.Message),
                ProjectDocumentTransitionStatus.Cancelled =>
                    StudioActionCompletion.Cancelled(result.Message),
                ProjectDocumentTransitionStatus.Busy =>
                    StudioActionCompletion.Conflict(result.Message),
                ProjectDocumentTransitionStatus.Stale =>
                    StudioActionCompletion.Stale(
                        result.Message,
                        diagnostic?.SequenceId),
                _ => StudioActionCompletion.Failed(
                    result.Message,
                    diagnostic?.SequenceId),
            };
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return StudioActionCompletion.Cancelled("The project transition was cancelled.");
        }
        catch (Exception exception)
        {
            const string message = "The document transition failed unexpectedly.";
            ProjectOperationMessage = message;
            var diagnostic = diagnostics_.PublishUnexpectedException(
                DiagnosticContext(actionId, context),
                message,
                exception);
            return StudioActionCompletion.Failed(message, diagnostic.SequenceId);
        }
        finally
        {
            IsProjectOperationRunning = false;
        }
    }

    private async ValueTask<StudioActionCompletion> HandleCreateEntityAsync(
        StudioActionContextSnapshot context,
        CancellationToken cancellationToken)
    {
        var execution = await ExecuteProjectActionCoreAsync(
            StudioShellActionIds.CreateEntity,
            context,
            cancellationToken,
            token => projectSession_.CreateEntityAsync("Entity", token));
        SelectCreatedEntity(execution.Result);
        return execution.Completion;
    }

    private async ValueTask<StudioActionCompletion> HandleCreateMeshEntityAsync(
        StudioActionContextSnapshot context,
        CancellationToken cancellationToken)
    {
        var execution = await ExecuteProjectActionCoreAsync(
            StudioShellActionIds.CreateMeshEntity,
            context,
            cancellationToken,
            token => projectSession_.CreateMeshEntityAsync(
                "Directional Wedge",
                SceneMeshReference.DirectionalWedgeValidation,
                token));
        SelectCreatedEntity(execution.Result);
        return execution.Completion;
    }

    private ValueTask<StudioActionCompletion> HandleApplyEntityNameAsync(
        StudioActionContextSnapshot context,
        CancellationToken cancellationToken)
    {
        var objectId = context.Target.ObjectId!.Value;
        return ExecuteProjectActionAsync(
            StudioShellActionIds.ApplyEntityName,
            context,
            cancellationToken,
            token => projectSession_.SetEntityNameAsync(
                objectId,
                InspectorName,
                token));
    }

    private async ValueTask<StudioActionCompletion> HandleApplyEntityTransformAsync(
        StudioActionContextSnapshot context,
        CancellationToken cancellationToken)
    {
        var selected = SelectedEntity!;
        if (!TryReadTransform(out var transform, out var submittedEuler))
        {
            ProjectOperationMessage =
                "Transform fields must be finite invariant-culture numbers; rotation is expressed in degrees.";
            return StudioActionCompletion.Disabled(ProjectOperationMessage);
        }
        rotationEulerHint_ = submittedEuler;
        var project = projectSnapshot_.Project!;
        var document = projectSnapshot_.Document!;
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

        var execution = await ExecuteProjectActionCoreAsync(
            StudioShellActionIds.ApplyEntityTransform,
            context,
            cancellationToken,
            token => projectSession_.SetEntityTransformAsync(
                selected.ObjectId,
                transform,
                new ProjectSessionEditContext(editId, baseRevision),
                token),
            (result) => ApplyProjectSnapshot(
                result.Current,
                result.OriginatingEditId,
                result.OriginatingEditId is null ? null : result.Succeeded));
        if (execution.Result is not { Succeeded: true } &&
            pendingTransformApply_?.EditId == editId)
        {
            pendingTransformApply_ = null;
        }
        return execution.Completion;
    }

    private async ValueTask<StudioActionCompletion> ExecuteProjectActionAsync(
        StudioActionId actionId,
        StudioActionContextSnapshot context,
        CancellationToken cancellationToken,
        Func<CancellationToken, ValueTask<ProjectSessionOperationResult>> operation) =>
        (await ExecuteProjectActionCoreAsync(
            actionId,
            context,
            cancellationToken,
            operation)).Completion;

    private async ValueTask<ProjectActionExecution> ExecuteProjectActionCoreAsync(
        StudioActionId actionId,
        StudioActionContextSnapshot context,
        CancellationToken cancellationToken,
        Func<CancellationToken, ValueTask<ProjectSessionOperationResult>> operation,
        Action<ProjectSessionOperationResult>? apply = null)
    {
        ProjectOperationMessage = string.Empty;
        IsProjectOperationRunning = true;
        try
        {
            var result = await operation(cancellationToken);
            (apply ?? (result => ApplyProjectSnapshot(result.Current)))(result);
            RememberOperationResult(result.Current);
            ProjectOperationMessage = result.Message;
            var diagnostic = diagnostics_.PublishProjectSessionFailure(
                result,
                DiagnosticContext(actionId, context));
            var completion = result.Succeeded
                ? StudioActionCompletion.Succeeded(
                    result.Message,
                    projectEditId: result.OriginatingEditId)
                : CompletionForFailure(result, diagnostic?.SequenceId);
            return new ProjectActionExecution(result, completion);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return new ProjectActionExecution(
                null,
                StudioActionCompletion.Cancelled("The project operation was cancelled."));
        }
        catch (Exception exception)
        {
            const string message = "The project operation failed unexpectedly.";
            ProjectOperationMessage = message;
            var diagnostic = diagnostics_.PublishUnexpectedException(
                DiagnosticContext(actionId, context),
                message,
                exception);
            return new ProjectActionExecution(
                null,
                StudioActionCompletion.Failed(message, diagnostic.SequenceId));
        }
        finally
        {
            IsProjectOperationRunning = false;
        }
    }


    private static StudioActionCompletion CompletionForFailure(
        ProjectSessionOperationResult result,
        long? diagnosticSequence) => result.FailureKind switch
        {
            ProjectSessionFailureKind.RevisionConflict =>
                StudioActionCompletion.Conflict(result.Message, diagnosticSequence),
            ProjectSessionFailureKind.Busy =>
                StudioActionCompletion.Conflict(result.Message, diagnosticSequence),
            ProjectSessionFailureKind.StaleDocumentTransition =>
                StudioActionCompletion.Stale(result.Message, diagnosticSequence),
            _ => StudioActionCompletion.Failed(result.Message, diagnosticSequence),
        };

    private ValueTask<StudioActionCompletion> HandleOpenPanelAsync(
        StudioActionId actionId,
        StudioPresentationId panelId,
        StudioActionContextSnapshot context)
    {
        try
        {
            return ValueTask.FromResult(dockWorkspace_.OpenPanel(panelId.Value)
                ? StudioActionCompletion.Succeeded($"Opened {panelId} panel.")
                : StudioActionCompletion.Disabled(
                    $"Panel '{panelId}' could not be opened."));
        }
        catch (Exception exception)
        {
            var message = $"Panel '{panelId}' failed to open unexpectedly.";
            var diagnostic = diagnostics_.PublishUnexpectedException(
                DiagnosticContext(actionId, context),
                message,
                exception);
            return ValueTask.FromResult(
                StudioActionCompletion.Failed(message, diagnostic.SequenceId));
        }
    }

    private static StudioActionPlacement Menu(
        StudioActionId actionId,
        string path,
        string section,
        int order) =>
        Placement(actionId, "menu", StudioActionPlacementKind.Menu, path, section, order);

    private static StudioActionPlacement Toolbar(
        StudioActionId actionId,
        string path,
        string section,
        int order) =>
        Placement(actionId, "toolbar", StudioActionPlacementKind.Toolbar, path, section, order);

    private static StudioActionPlacement ContextMenu(
        StudioActionId actionId,
        string path,
        string section,
        int order) =>
        Placement(
            actionId,
            "context",
            StudioActionPlacementKind.ContextMenu,
            path,
            section,
            order,
            StudioActionScope.FocusedPanel);

    private static StudioActionPlacement Shortcut(
        StudioActionId actionId,
        string key,
        StudioShortcutModifiers modifiers,
        int order,
        string suffix = "primary") =>
        new(
            new StudioActionPlacementId($"{actionId.Value}.shortcut.{suffix}"),
            StudioActionPlacementKind.Shortcut,
            path: null,
            "shortcut",
            order,
            StudioActionScope.Document,
            new StudioShortcutChord(key, modifiers));

    private static StudioActionPlacement Placement(
        StudioActionId actionId,
        string kindSuffix,
        StudioActionPlacementKind kind,
        string path,
        string section,
        int order,
        StudioActionScope scope = StudioActionScope.Workspace) =>
        new(
            new StudioActionPlacementId($"{actionId.Value}.{kindSuffix}"),
            kind,
            path,
            section,
            order,
            scope);

    private StudioUnexpectedOperationContext DiagnosticContext(
        StudioActionId actionId,
        StudioActionContextSnapshot context) =>
        new(
            $"{actionId.Value}.failed",
            "studio-action",
            "studio-shell",
            context.ProjectSessionId is ProjectSessionId projectSessionId
                ? new StudioDiagnosticScope(
                    "studio-action",
                    projectSessionId.Value.ToString("D"),
                    checked((long)(context.DocumentRevision ?? 0)))
                : StudioDiagnosticScope.Process(diagnostics_.ProcessIdentity),
            context.OperationId,
            context.CorrelationId,
            context.ParentCorrelationId,
            "Review the action message and retry after resolving the reported project state.");

    private void OnDockContentChanged(object? sender, EventArgs e) =>
        RaiseProjectCommandStateChanged();

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
        foreach (var command in actionCommands_.Values)
        {
            command.RaiseCanExecuteChanged();
        }
        foreach (var command in projectedActionCommands_.Values)
        {
            command.RaiseCanExecuteChanged();
        }
        if (!isDisposed_)
        {
            ActionStateChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    private void RaiseActionStateChanged(StudioActionId actionId)
    {
        if (actionCommands_.TryGetValue(actionId, out var command))
        {
            command.RaiseCanExecuteChanged();
        }
        foreach (var pair in projectedActionCommands_)
        {
            if (pair.Key.ActionId == actionId)
            {
                pair.Value.RaiseCanExecuteChanged();
            }
        }
        if (!isDisposed_)
        {
            ActionStateChanged?.Invoke(this, EventArgs.Empty);
        }
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
