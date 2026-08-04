using System;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.Projects;

public enum ProjectDescriptorFailureKind
{
    InvalidInput,
    InvalidProject,
    AlreadyExists,
    Busy,
    IoFailure,
    NativeUnavailable,
    InternalError,
}

public sealed record ProjectDescriptorSnapshot
{
    public ProjectDescriptorSnapshot(
        string rootPath,
        string projectName,
        Guid projectId)
    {
        if (string.IsNullOrWhiteSpace(rootPath))
        {
            throw new ArgumentException(
                "Project root path must not be null or whitespace.",
                nameof(rootPath));
        }
        if (string.IsNullOrWhiteSpace(projectName))
        {
            throw new ArgumentException(
                "Project name must not be null or whitespace.",
                nameof(projectName));
        }
        if (projectId == Guid.Empty)
        {
            throw new ArgumentException(
                "Project id must not be empty.",
                nameof(projectId));
        }

        RootPath = rootPath;
        ProjectName = projectName;
        ProjectId = projectId;
    }

    public string RootPath { get; }

    public string ProjectName { get; }

    public Guid ProjectId { get; }
}

public sealed record ProjectDescriptorFailure
{
    public ProjectDescriptorFailure(
        ProjectDescriptorFailureKind kind,
        string message)
    {
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, null);
        }
        if (string.IsNullOrWhiteSpace(message))
        {
            throw new ArgumentException(
                "Project descriptor failure message must not be empty.",
                nameof(message));
        }

        Kind = kind;
        Message = message;
    }

    public ProjectDescriptorFailureKind Kind { get; }

    public string Message { get; }
}

public sealed record ProjectDescriptorOperationResult
{
    private ProjectDescriptorOperationResult(
        ProjectDescriptorSnapshot? project,
        ProjectDescriptorFailure? failure)
    {
        if ((project is null) == (failure is null))
        {
            throw new ArgumentException(
                "A project descriptor result must contain exactly one project or failure.");
        }

        Project = project;
        Failure = failure;
    }

    public ProjectDescriptorSnapshot? Project { get; }

    public ProjectDescriptorFailure? Failure { get; }

    public bool Succeeded => Project is not null;

    public static ProjectDescriptorOperationResult Success(
        ProjectDescriptorSnapshot project)
    {
        ArgumentNullException.ThrowIfNull(project);
        return new ProjectDescriptorOperationResult(project, failure: null);
    }

    public static ProjectDescriptorOperationResult Failed(
        ProjectDescriptorFailure failure)
    {
        ArgumentNullException.ThrowIfNull(failure);
        return new ProjectDescriptorOperationResult(project: null, failure);
    }
}

public interface IProjectDescriptorGateway
{
    ValueTask<ProjectDescriptorOperationResult> CreateMinimalProjectAsync(
        string parentDirectory,
        string projectName,
        Guid projectId,
        CancellationToken cancellationToken = default);

    ValueTask<ProjectDescriptorOperationResult> OpenProjectAsync(
        string projectPath,
        CancellationToken cancellationToken = default);
}
