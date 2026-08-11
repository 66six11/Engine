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
        SceneDocumentSnapshot? document)
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

        State = state;
        Project = project;
        Document = document;
    }

    public static ProjectSessionSnapshot NoProject { get; } =
        new(ProjectSessionState.NoProject, project: null, document: null);

    public ProjectSessionState State { get; }

    public ActiveProjectSnapshot? Project { get; }

    public SceneDocumentSnapshot? Document { get; }

    public bool IsReady => State == ProjectSessionState.Ready;

    public static ProjectSessionSnapshot Ready(
        ActiveProjectSnapshot project,
        SceneDocumentSnapshot document)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(document);
        return new ProjectSessionSnapshot(ProjectSessionState.Ready, project, document);
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
    InternalError,
}

public sealed record ProjectSessionOperationResult
{
    private ProjectSessionOperationResult(
        bool succeeded,
        ProjectSessionSnapshot current,
        string message,
        ProjectSessionFailureKind? failureKind,
        Guid? createdObjectId)
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

        Succeeded = succeeded;
        Current = current;
        Message = message;
        FailureKind = failureKind;
        CreatedObjectId = createdObjectId;
    }

    public bool Succeeded { get; }

    public ProjectSessionSnapshot Current { get; }

    public string Message { get; }

    public ProjectSessionFailureKind? FailureKind { get; }

    public Guid? CreatedObjectId { get; }

    public static ProjectSessionOperationResult Success(
        ProjectSessionSnapshot current,
        string message,
        Guid? createdObjectId = null) =>
        new(succeeded: true, current, message, failureKind: null, createdObjectId);

    public static ProjectSessionOperationResult Failed(
        ProjectSessionSnapshot current,
        ProjectSessionFailureKind failureKind,
        string message)
    {
        return new ProjectSessionOperationResult(
            succeeded: false,
            current,
            message,
            failureKind,
            createdObjectId: null);
    }
}

public interface IProjectSession : IAsyncDisposable
{
    event EventHandler? SnapshotChanged;

    ProjectSessionSnapshot Current { get; }

    ValueTask<ProjectSessionOperationResult> CreateProjectAsync(
        string parentDirectory,
        string projectName,
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> OpenProjectAsync(
        string projectPath,
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> CloseProjectAsync(
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
        CancellationToken cancellationToken = default);

    ValueTask<ProjectSessionOperationResult> SaveSceneAsync(
        CancellationToken cancellationToken = default);
}
