using System;

namespace Editor.Core.Models;

public sealed record EditorStatusMessageSnapshot(
    EditorStatusMessageSeverity Severity,
    EditorStatusMessageSource Source,
    string Message,
    string? TargetPanelId = null)
{
    public static EditorStatusMessageSnapshot FromCommandResult(WorkbenchCommandExecutionResult result)
    {
        ArgumentNullException.ThrowIfNull(result);

        return new EditorStatusMessageSnapshot(
            MapSeverity(result.Status),
            EditorStatusMessageSource.Command,
            CreateMessage(result));
    }

    private static EditorStatusMessageSeverity MapSeverity(WorkbenchCommandExecutionStatus status)
    {
        return status switch
        {
            WorkbenchCommandExecutionStatus.Succeeded => EditorStatusMessageSeverity.Success,
            WorkbenchCommandExecutionStatus.Disabled => EditorStatusMessageSeverity.Warning,
            WorkbenchCommandExecutionStatus.NotFound => EditorStatusMessageSeverity.Error,
            WorkbenchCommandExecutionStatus.Failed => EditorStatusMessageSeverity.Error,
            _ => EditorStatusMessageSeverity.Info,
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
