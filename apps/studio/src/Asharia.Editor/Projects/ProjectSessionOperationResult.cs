using System;

namespace Asharia.Editor.Projects;

public sealed record ProjectSessionOperationResult
{
    private ProjectSessionOperationResult(
        bool succeeded,
        ProjectSessionSnapshot? session,
        string message)
    {
        if (succeeded != (session is not null))
        {
            throw new ArgumentException(
                "Only a successful project operation may contain a session.",
                nameof(session));
        }
        if (succeeded && session?.State != ProjectSessionState.Ready)
        {
            throw new ArgumentException(
                "A successful project operation requires a ready session.",
                nameof(session));
        }
        if (string.IsNullOrWhiteSpace(message))
        {
            throw new ArgumentException(
                "Project operation message must not be null or whitespace.",
                nameof(message));
        }

        Succeeded = succeeded;
        Session = session;
        Message = message;
    }

    public bool Succeeded { get; }

    public ProjectSessionSnapshot? Session { get; }

    public string Message { get; }

    public static ProjectSessionOperationResult Success(
        ProjectSessionSnapshot session,
        string message)
    {
        ArgumentNullException.ThrowIfNull(session);
        return new ProjectSessionOperationResult(
            succeeded: true,
            session,
            message);
    }

    public static ProjectSessionOperationResult Failure(string message)
    {
        return new ProjectSessionOperationResult(
            succeeded: false,
            session: null,
            message);
    }
}
