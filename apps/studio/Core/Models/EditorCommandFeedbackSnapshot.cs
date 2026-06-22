using System;

namespace Editor.Core.Models;

public sealed record EditorCommandFeedbackSnapshot(
    EditorCommandFeedbackSeverity Severity,
    WorkbenchCommandExecutionStatus Status,
    string CommandId,
    string Message)
{
    public static EditorCommandFeedbackSnapshot FromResult(WorkbenchCommandExecutionResult result)
    {
        ArgumentNullException.ThrowIfNull(result);

        return new EditorCommandFeedbackSnapshot(
            MapSeverity(result.Status),
            result.Status,
            result.CommandId,
            CreateMessage(result));
    }

    private static EditorCommandFeedbackSeverity MapSeverity(WorkbenchCommandExecutionStatus status)
    {
        return status switch
        {
            WorkbenchCommandExecutionStatus.Succeeded => EditorCommandFeedbackSeverity.Success,
            WorkbenchCommandExecutionStatus.Disabled => EditorCommandFeedbackSeverity.Warning,
            WorkbenchCommandExecutionStatus.NotFound => EditorCommandFeedbackSeverity.Error,
            WorkbenchCommandExecutionStatus.Failed => EditorCommandFeedbackSeverity.Error,
            _ => EditorCommandFeedbackSeverity.Info,
        };
    }

    private static string CreateMessage(WorkbenchCommandExecutionResult result)
    {
        if (!string.IsNullOrWhiteSpace(result.Message))
        {
            return result.Message;
        }

        return result.Status switch
        {
            WorkbenchCommandExecutionStatus.Succeeded => $"Command '{result.CommandId}' completed.",
            WorkbenchCommandExecutionStatus.Disabled => $"Command '{result.CommandId}' is disabled.",
            WorkbenchCommandExecutionStatus.NotFound => $"Command '{result.CommandId}' is not registered.",
            WorkbenchCommandExecutionStatus.Failed => $"Command '{result.CommandId}' did not complete.",
            _ => $"Command '{result.CommandId}' finished with status {result.Status}.",
        };
    }
}
