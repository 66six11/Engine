using System;

using Asharia.Editor.Commands;

namespace Editor.Core.Models.Workbench;

public sealed record EditorStatusMessageSnapshot(
    EditorStatusMessageSeverity Severity,
    EditorStatusMessageSource Source,
    string Message,
    string? TargetPanelId = null)
{
    public static EditorStatusMessageSnapshot FromCommandResult(EditorCommandExecutionResult result)
    {
        ArgumentNullException.ThrowIfNull(result);

        return new EditorStatusMessageSnapshot(
            MapSeverity(result.Status),
            EditorStatusMessageSource.Command,
            CreateMessage(result));
    }

    private static EditorStatusMessageSeverity MapSeverity(EditorCommandExecutionStatus status)
    {
        return status switch
        {
            EditorCommandExecutionStatus.Succeeded => EditorStatusMessageSeverity.Success,
            EditorCommandExecutionStatus.Disabled => EditorStatusMessageSeverity.Warning,
            EditorCommandExecutionStatus.NotFound => EditorStatusMessageSeverity.Error,
            EditorCommandExecutionStatus.Failed => EditorStatusMessageSeverity.Error,
            _ => EditorStatusMessageSeverity.Info,
        };
    }

    private static string CreateMessage(EditorCommandExecutionResult result)
    {
        if (!string.IsNullOrWhiteSpace(result.Message))
        {
            return result.Message;
        }

        return result.Status switch
        {
            EditorCommandExecutionStatus.Succeeded => $"Command '{result.CommandId}' completed.",
            EditorCommandExecutionStatus.Disabled => $"Command '{result.CommandId}' is disabled.",
            EditorCommandExecutionStatus.NotFound => $"Command '{result.CommandId}' is not registered.",
            EditorCommandExecutionStatus.Failed => $"Command '{result.CommandId}' did not complete.",
            _ => $"Command '{result.CommandId}' finished with status {result.Status}.",
        };
    }
}
