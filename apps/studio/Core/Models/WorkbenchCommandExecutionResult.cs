namespace Editor.Core.Models;

public sealed record WorkbenchCommandExecutionResult(
    WorkbenchCommandExecutionStatus Status,
    string CommandId,
    string? Message = null)
{
    public bool Succeeded => Status == WorkbenchCommandExecutionStatus.Succeeded;

    public static WorkbenchCommandExecutionResult Success(string commandId)
    {
        return new WorkbenchCommandExecutionResult(
            WorkbenchCommandExecutionStatus.Succeeded,
            commandId);
    }

    public static WorkbenchCommandExecutionResult NotFound(string commandId)
    {
        return new WorkbenchCommandExecutionResult(
            WorkbenchCommandExecutionStatus.NotFound,
            commandId,
            $"Command '{commandId}' is not registered.");
    }

    public static WorkbenchCommandExecutionResult Disabled(string commandId, string disabledReason)
    {
        return new WorkbenchCommandExecutionResult(
            WorkbenchCommandExecutionStatus.Disabled,
            commandId,
            disabledReason);
    }

    public static WorkbenchCommandExecutionResult Failed(string commandId, string message)
    {
        return new WorkbenchCommandExecutionResult(
            WorkbenchCommandExecutionStatus.Failed,
            commandId,
            message);
    }
}
