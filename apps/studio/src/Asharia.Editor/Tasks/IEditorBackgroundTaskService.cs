using System;
using System.Collections.Generic;

namespace Asharia.Editor.Tasks;

public interface IEditorBackgroundTaskService
{
    event EventHandler? TasksChanged;

    EditorBackgroundTaskId Start(string operationId, string title, bool canCancel);

    void Report(EditorBackgroundTaskId id, double? progress, string? message);

    void Complete(EditorBackgroundTaskId id, string? message);

    void Fail(EditorBackgroundTaskId id, string message);

    void Cancel(EditorBackgroundTaskId id, string? message);

    EditorBackgroundTaskSnapshot GetSnapshot(EditorBackgroundTaskId id);

    IReadOnlyList<EditorBackgroundTaskSnapshot> GetActiveSnapshots();
}
