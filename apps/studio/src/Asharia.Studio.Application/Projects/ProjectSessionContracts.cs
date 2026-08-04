using System;
using System.Threading;
using System.Threading.Tasks;

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
        ActiveProjectSnapshot? project)
    {
        if ((state == ProjectSessionState.Ready) != (project is not null))
        {
            throw new ArgumentException(
                "Only a ready project session may contain an active project.",
                nameof(project));
        }

        State = state;
        Project = project;
    }

    public static ProjectSessionSnapshot NoProject { get; } =
        new(ProjectSessionState.NoProject, project: null);

    public ProjectSessionState State { get; }

    public ActiveProjectSnapshot? Project { get; }

    public bool IsReady => State == ProjectSessionState.Ready;

    public static ProjectSessionSnapshot Ready(ActiveProjectSnapshot project)
    {
        ArgumentNullException.ThrowIfNull(project);
        return new ProjectSessionSnapshot(ProjectSessionState.Ready, project);
    }
}

public sealed record ProjectSessionOperationResult
{
    private ProjectSessionOperationResult(
        bool succeeded,
        ProjectSessionSnapshot current,
        string message,
        ProjectDescriptorFailureKind? failureKind)
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

        Succeeded = succeeded;
        Current = current;
        Message = message;
        FailureKind = failureKind;
    }

    public bool Succeeded { get; }

    public ProjectSessionSnapshot Current { get; }

    public string Message { get; }

    public ProjectDescriptorFailureKind? FailureKind { get; }

    public static ProjectSessionOperationResult Success(
        ProjectSessionSnapshot current,
        string message) =>
        new(succeeded: true, current, message, failureKind: null);

    public static ProjectSessionOperationResult Failed(
        ProjectSessionSnapshot current,
        ProjectDescriptorFailure failure)
    {
        ArgumentNullException.ThrowIfNull(failure);
        return new ProjectSessionOperationResult(
            succeeded: false,
            current,
            failure.Message,
            failure.Kind);
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
}
