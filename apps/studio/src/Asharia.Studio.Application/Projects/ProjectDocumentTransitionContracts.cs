using System;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.Projects;

public enum ProjectDocumentTransitionKind
{
    CreateProject,
    OpenProject,
    CloseProject,
    ExitStudio,
}

public enum ProjectDocumentTransitionDecision
{
    Cancel,
    Save,
    Discard,
}

public sealed record ProjectDocumentTransitionPrompt
{
    private ProjectDocumentTransitionPrompt(
        ProjectDocumentTransitionKind kind,
        ProjectSessionId sessionId,
        Guid projectId,
        string projectName,
        Guid sceneId,
        string documentPath,
        ulong documentRevision,
        ContentStateId currentContentStateId,
        ContentStateId savedContentStateId)
    {
        Kind = kind;
        SessionId = sessionId;
        ProjectId = projectId;
        ProjectName = projectName;
        SceneId = sceneId;
        DocumentPath = documentPath;
        DocumentRevision = documentRevision;
        CurrentContentStateId = currentContentStateId;
        SavedContentStateId = savedContentStateId;
    }

    public ProjectDocumentTransitionKind Kind { get; }

    public ProjectSessionId SessionId { get; }

    public Guid ProjectId { get; }

    public string ProjectName { get; }

    public Guid SceneId { get; }

    public string DocumentPath { get; }

    public ulong DocumentRevision { get; }

    public ContentStateId CurrentContentStateId { get; }

    public ContentStateId SavedContentStateId { get; }

    internal static ProjectDocumentTransitionPrompt FromDirtySnapshot(
        ProjectDocumentTransitionKind kind,
        ProjectSessionSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, null);
        }
        if (!snapshot.IsDirty || snapshot.Project is null || snapshot.Document is null)
        {
            throw new ArgumentException(
                "A document transition prompt requires a dirty active document.",
                nameof(snapshot));
        }

        return new ProjectDocumentTransitionPrompt(
            kind,
            snapshot.Project.SessionId,
            snapshot.Project.ProjectId,
            snapshot.Project.ProjectName,
            snapshot.Document.SceneId,
            snapshot.Document.Path,
            snapshot.Document.Revision,
            snapshot.CurrentContentStateId,
            snapshot.SavedContentStateId);
    }
}

public interface IProjectDocumentTransitionPrompt
{
    ValueTask<ProjectDocumentTransitionDecision> ChooseAsync(
        ProjectDocumentTransitionPrompt prompt,
        CancellationToken cancellationToken = default);
}

public enum ProjectDocumentTransitionStatus
{
    Completed,
    Cancelled,
    Stale,
    SaveFailed,
    TransitionFailed,
    Busy,
}

public sealed record ProjectDocumentTransitionResult
{
    private ProjectDocumentTransitionResult(
        ProjectDocumentTransitionStatus status,
        string message,
        ProjectSessionOperationResult? projectOperation)
    {
        if (string.IsNullOrWhiteSpace(message))
        {
            throw new ArgumentException(
                "A document transition result message must not be empty.",
                nameof(message));
        }
        var operationIsValid = status switch
        {
            ProjectDocumentTransitionStatus.Completed =>
                projectOperation is null or { Succeeded: true },
            ProjectDocumentTransitionStatus.SaveFailed or
                ProjectDocumentTransitionStatus.TransitionFailed =>
                projectOperation is { Succeeded: false },
            ProjectDocumentTransitionStatus.Cancelled or
                ProjectDocumentTransitionStatus.Busy =>
                projectOperation is null,
            ProjectDocumentTransitionStatus.Stale =>
                projectOperation is null or
                {
                    Succeeded: false,
                    FailureKind: ProjectSessionFailureKind.StaleDocumentTransition,
                },
            _ => false,
        };
        if (!operationIsValid)
        {
            throw new ArgumentException(
                "The project operation does not match the document transition status.",
                nameof(projectOperation));
        }

        Status = status;
        Message = message;
        ProjectOperation = projectOperation;
    }

    public ProjectDocumentTransitionStatus Status { get; }

    public string Message { get; }

    public ProjectSessionOperationResult? ProjectOperation { get; }

    public bool MayProceed => Status == ProjectDocumentTransitionStatus.Completed;

    internal static ProjectDocumentTransitionResult Completed(
        ProjectSessionOperationResult? operation = null) =>
        new(
            ProjectDocumentTransitionStatus.Completed,
            operation?.Message ?? "The document transition may continue.",
            operation);

    internal static ProjectDocumentTransitionResult Cancelled() =>
        new(
            ProjectDocumentTransitionStatus.Cancelled,
            "The document transition was cancelled.",
            projectOperation: null);

    internal static ProjectDocumentTransitionResult Stale(
        ProjectSessionOperationResult? operation = null) =>
        new(
            ProjectDocumentTransitionStatus.Stale,
            operation?.Message ??
                "The active document changed while the transition was awaiting confirmation.",
            operation);

    internal static ProjectDocumentTransitionResult SaveFailed(
        ProjectSessionOperationResult operation) =>
        new(
            ProjectDocumentTransitionStatus.SaveFailed,
            operation.Message,
            operation);

    internal static ProjectDocumentTransitionResult TransitionFailed(
        ProjectSessionOperationResult operation) =>
        new(
            ProjectDocumentTransitionStatus.TransitionFailed,
            operation.Message,
            operation);

    internal static ProjectDocumentTransitionResult Busy() =>
        new(
            ProjectDocumentTransitionStatus.Busy,
            "Another document transition is already in progress.",
            projectOperation: null);
}
