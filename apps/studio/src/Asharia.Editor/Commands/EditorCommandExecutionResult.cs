namespace Asharia.Editor.Commands;

public sealed record EditorCommandExecutionResult(
    EditorCommandExecutionStatus Status,
    string CommandId,
    string? Message = null)
{
    public bool Succeeded => Status == EditorCommandExecutionStatus.Succeeded;

    public static EditorCommandExecutionResult Success(string commandId) =>
        new(EditorCommandExecutionStatus.Succeeded, commandId);

    public static EditorCommandExecutionResult NotFound(string commandId) =>
        new(
            EditorCommandExecutionStatus.NotFound,
            commandId,
            $"Command '{commandId}' is not registered.");

    public static EditorCommandExecutionResult Disabled(string commandId, string disabledReason) =>
        new(EditorCommandExecutionStatus.Disabled, commandId, disabledReason);

    public static EditorCommandExecutionResult Failed(string commandId, string message) =>
        new(EditorCommandExecutionStatus.Failed, commandId, message);
}
