namespace Asharia.Editor.Tasks;

public sealed record EditorBackgroundTaskSnapshot(
    EditorBackgroundTaskId Id,
    string OperationId,
    string Title,
    EditorBackgroundTaskState State,
    double? Progress,
    string? Message,
    bool CanCancel);
