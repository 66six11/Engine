using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Scenes;

namespace Asharia.Studio.Application.Projects;

public readonly record struct ProjectSessionId(Guid Value)
{
    public bool IsValid => Value != Guid.Empty;

    public static ProjectSessionId CreateNew() => new(Guid.NewGuid());
}

public readonly record struct ProjectEditId(Guid Value)
{
    public bool IsValid => Value != Guid.Empty;

    public static ProjectEditId CreateNew() => new(Guid.NewGuid());
}

public readonly record struct ContentStateId(ulong Value)
{
    public bool IsValid => Value != 0;
}

public readonly record struct ProjectSessionEditContext(
    ProjectEditId EditId,
    ulong ExpectedRevision);

public sealed record ProjectDocumentTransitionExpectation
{
    private ProjectDocumentTransitionExpectation(
        ProjectSessionState state,
        ProjectSessionId sessionId,
        Guid projectId,
        Guid sceneId,
        ulong documentRevision,
        ContentStateId currentContentStateId,
        ContentStateId savedContentStateId,
        bool allowsUnsavedDiscard)
    {
        State = state;
        SessionId = sessionId;
        ProjectId = projectId;
        SceneId = sceneId;
        DocumentRevision = documentRevision;
        CurrentContentStateId = currentContentStateId;
        SavedContentStateId = savedContentStateId;
        AllowsUnsavedDiscard = allowsUnsavedDiscard;
    }

    public ProjectSessionState State { get; }

    public ProjectSessionId SessionId { get; }

    public Guid ProjectId { get; }

    public Guid SceneId { get; }

    public ulong DocumentRevision { get; }

    public ContentStateId CurrentContentStateId { get; }

    public ContentStateId SavedContentStateId { get; }

    internal bool AllowsUnsavedDiscard { get; }

    public static ProjectDocumentTransitionExpectation Capture(
        ProjectSessionSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (snapshot.IsDirty)
        {
            throw new ArgumentException(
                "A dirty document transition expectation requires an explicit " +
                "Save or Discard decision from the transition coordinator.",
                nameof(snapshot));
        }
        return CaptureCore(snapshot, allowsUnsavedDiscard: false);
    }

    internal static ProjectDocumentTransitionExpectation CaptureAfterDiscard(
        ProjectSessionSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (!snapshot.IsDirty)
        {
            throw new ArgumentException(
                "A Discard transition expectation requires a dirty document.",
                nameof(snapshot));
        }
        return CaptureCore(snapshot, allowsUnsavedDiscard: true);
    }

    private static ProjectDocumentTransitionExpectation CaptureCore(
        ProjectSessionSnapshot snapshot,
        bool allowsUnsavedDiscard)
    {
        if (!snapshot.IsReady)
        {
            return new ProjectDocumentTransitionExpectation(
                ProjectSessionState.NoProject,
                default,
                Guid.Empty,
                Guid.Empty,
                documentRevision: 0,
                default,
                default,
                allowsUnsavedDiscard: false);
        }

        return new ProjectDocumentTransitionExpectation(
            ProjectSessionState.Ready,
            snapshot.Project!.SessionId,
            snapshot.Project.ProjectId,
            snapshot.Document!.SceneId,
            snapshot.Document.Revision,
            snapshot.CurrentContentStateId,
            snapshot.SavedContentStateId,
            allowsUnsavedDiscard);
    }

    internal bool Matches(ProjectSessionSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        return State == snapshot.State &&
            (State == ProjectSessionState.NoProject ||
             snapshot.Project!.SessionId == SessionId &&
             snapshot.Project.ProjectId == ProjectId &&
             snapshot.Document!.SceneId == SceneId &&
             snapshot.Document.Revision == DocumentRevision &&
             snapshot.CurrentContentStateId == CurrentContentStateId &&
             snapshot.SavedContentStateId == SavedContentStateId);
    }
}

public enum ProjectSessionState
{
    NoProject,
    Ready,
}

public sealed record ActiveProjectSnapshot
{
    public ActiveProjectSnapshot(
        ProjectSessionId sessionId,
        Guid projectId,
        string projectName,
        string rootPath)
    {
        if (!sessionId.IsValid)
        {
            throw new ArgumentException(
                "Project session id must be valid.",
                nameof(sessionId));
        }
        if (projectId == Guid.Empty)
        {
            throw new ArgumentException(
                "Project id must not be empty.",
                nameof(projectId));
        }
        if (string.IsNullOrWhiteSpace(projectName))
        {
            throw new ArgumentException(
                "Project name must not be empty.",
                nameof(projectName));
        }
        if (string.IsNullOrWhiteSpace(rootPath))
        {
            throw new ArgumentException(
                "Project root path must not be empty.",
                nameof(rootPath));
        }

        SessionId = sessionId;
        ProjectId = projectId;
        ProjectName = projectName;
        RootPath = rootPath;
    }

    public ProjectSessionId SessionId { get; }

    public Guid ProjectId { get; }

    public string ProjectName { get; }

    public string RootPath { get; }
}

public sealed record ProjectSessionSnapshot
{
    private ProjectSessionSnapshot(
        ProjectSessionState state,
        ActiveProjectSnapshot? project,
        SceneDocumentSnapshot? document,
        ContentStateId currentContentStateId,
        ContentStateId savedContentStateId,
        bool canUndo,
        bool canRedo,
        string? undoLabel,
        string? redoLabel)
    {
        if (state == ProjectSessionState.Ready &&
            (project is null || document is null))
        {
            throw new ArgumentException(
                "Only a ready project session may contain an active project and scene document.",
                nameof(project));
        }
        if (state == ProjectSessionState.NoProject &&
            (project is not null || document is not null))
        {
            throw new ArgumentException(
                "A project-free session must not contain a scene document.",
                nameof(document));
        }
        if (state == ProjectSessionState.Ready &&
            (!currentContentStateId.IsValid || !savedContentStateId.IsValid))
        {
            throw new ArgumentException(
                "A ready project session requires valid current and saved content state ids.",
                nameof(currentContentStateId));
        }
        if (state == ProjectSessionState.NoProject &&
            (currentContentStateId.IsValid || savedContentStateId.IsValid ||
             canUndo || canRedo || undoLabel is not null || redoLabel is not null))
        {
            throw new ArgumentException(
                "A project-free session cannot contain document history state.",
                nameof(currentContentStateId));
        }
        if (canUndo != !string.IsNullOrWhiteSpace(undoLabel))
        {
            throw new ArgumentException(
                "An Undo label is required exactly when Undo is available.",
                nameof(undoLabel));
        }
        if (canRedo != !string.IsNullOrWhiteSpace(redoLabel))
        {
            throw new ArgumentException(
                "A Redo label is required exactly when Redo is available.",
                nameof(redoLabel));
        }

        State = state;
        Project = project;
        Document = document;
        CurrentContentStateId = currentContentStateId;
        SavedContentStateId = savedContentStateId;
        CanUndo = canUndo;
        CanRedo = canRedo;
        UndoLabel = undoLabel;
        RedoLabel = redoLabel;
    }

    public static ProjectSessionSnapshot NoProject { get; } =
        new(
            ProjectSessionState.NoProject,
            project: null,
            document: null,
            currentContentStateId: default,
            savedContentStateId: default,
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);

    public ProjectSessionState State { get; }

    public ActiveProjectSnapshot? Project { get; }

    public SceneDocumentSnapshot? Document { get; }

    public bool IsReady => State == ProjectSessionState.Ready;

    public ContentStateId CurrentContentStateId { get; }

    public ContentStateId SavedContentStateId { get; }

    public bool IsDirty =>
        IsReady && CurrentContentStateId != SavedContentStateId;

    public bool CanUndo { get; }

    public bool CanRedo { get; }

    public string? UndoLabel { get; }

    public string? RedoLabel { get; }

    public static ProjectSessionSnapshot Ready(
        ActiveProjectSnapshot project,
        SceneDocumentSnapshot document,
        ContentStateId currentContentStateId,
        ContentStateId savedContentStateId,
        bool canUndo,
        bool canRedo,
        string? undoLabel,
        string? redoLabel)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(document);
        return new ProjectSessionSnapshot(
            ProjectSessionState.Ready,
            project,
            document,
            currentContentStateId,
            savedContentStateId,
            canUndo,
            canRedo,
            undoLabel,
            redoLabel);
    }
}

public enum ProjectSessionFailureKind
{
    InvalidInput,
    InvalidProject,
    InvalidScene,
    AlreadyExists,
    Busy,
    RevisionConflict,
    InvalidObject,
    InvalidTransform,
    InvalidAssetReference,
    IoFailure,
    NativeUnavailable,
    NoProject,
    StaleDocumentTransition,
    InternalError,
}

public sealed record ProjectSessionOperationResult
{
    private ProjectSessionOperationResult(
        bool succeeded,
        ProjectSessionSnapshot current,
        string message,
        ProjectSessionFailureKind? failureKind,
        Guid? createdObjectId,
        ProjectEditId? originatingEditId)
    {
        if (string.IsNullOrWhiteSpace(message))
        {
            throw new ArgumentException(
                "Project session operation message must not be empty.",
                nameof(message));
        }
        if (succeeded != (failureKind is null))
        {
            throw new ArgumentException(
                "Only a failed project session operation may contain a failure kind.",
                nameof(failureKind));
        }
        if (!succeeded && createdObjectId is not null)
        {
            throw new ArgumentException(
                "A failed project session operation cannot contain a created object id.",
                nameof(createdObjectId));
        }
        if (createdObjectId == Guid.Empty)
        {
            throw new ArgumentException(
                "A created object id must not be empty.",
                nameof(createdObjectId));
        }
        if (originatingEditId is ProjectEditId editId && !editId.IsValid)
        {
            throw new ArgumentException(
                "An originating project edit id must be valid.",
                nameof(originatingEditId));
        }

        Succeeded = succeeded;
        Current = current;
        Message = message;
        FailureKind = failureKind;
        CreatedObjectId = createdObjectId;
        OriginatingEditId = originatingEditId;
    }

    public bool Succeeded { get; }

    public ProjectSessionSnapshot Current { get; }

    public string Message { get; }

    public ProjectSessionFailureKind? FailureKind { get; }

    public Guid? CreatedObjectId { get; }

    public ProjectEditId? OriginatingEditId { get; }

    public static ProjectSessionOperationResult Success(
        ProjectSessionSnapshot current,
        string message,
        Guid? createdObjectId = null,
        ProjectEditId? originatingEditId = null) =>
        new(
            succeeded: true,
            current,
            message,
            failureKind: null,
            createdObjectId,
            originatingEditId);

    public static ProjectSessionOperationResult Failed(
        ProjectSessionSnapshot current,
        ProjectSessionFailureKind failureKind,
        string message,
        ProjectEditId? originatingEditId = null)
    {
        return new ProjectSessionOperationResult(
            succeeded: false,
            current,
            message,
            failureKind,
            createdObjectId: null,
            originatingEditId);
    }
}

public sealed class ProjectSessionSnapshotChangedEventArgs : EventArgs
{
    public ProjectSessionSnapshotChangedEventArgs(
        ProjectSessionSnapshot snapshot,
        ProjectEditId? originatingEditId,
        bool? originatingEditSucceeded = null)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (originatingEditId is ProjectEditId editId && !editId.IsValid)
        {
            throw new ArgumentException(
                "An originating project edit id must be valid.",
                nameof(originatingEditId));
        }
        if ((originatingEditId is null) != (originatingEditSucceeded is null))
        {
            throw new ArgumentException(
                "An originating edit outcome requires an originating edit id.",
                nameof(originatingEditSucceeded));
        }

        Snapshot = snapshot;
        OriginatingEditId = originatingEditId;
        OriginatingEditSucceeded = originatingEditSucceeded;
    }

    public ProjectSessionSnapshot Snapshot { get; }

    public ProjectEditId? OriginatingEditId { get; }

    public bool? OriginatingEditSucceeded { get; }
}

public interface IProjectSession : IAsyncDisposable
{
    event EventHandler<ProjectSessionSnapshotChangedEventArgs>? SnapshotChanged;

    ProjectSessionSnapshot Current { get; }

    ValueTask<ProjectSessionOperationResult> CreateProjectAsync(
        string parentDirectory,
        string projectName,
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> OpenProjectAsync(
        string projectPath,
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> CloseProjectAsync(
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> PrepareExitAsync(
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> CreateEntityAsync(
        string name,
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> CreateMeshEntityAsync(
        string name,
        SceneMeshReference mesh,
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> SetEntityNameAsync(
        Guid objectId,
        string name,
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> SetEntityTransformAsync(
        Guid objectId,
        TransformValue transform,
        ProjectSessionEditContext context,
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> UndoAsync(
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> RedoAsync(
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> SaveSceneAsync(
        CancellationToken cancellationToken = default);
}
