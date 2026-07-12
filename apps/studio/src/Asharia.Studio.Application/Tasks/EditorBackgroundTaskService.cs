using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Tasks;

namespace Asharia.Studio.Application.Tasks;

public sealed class EditorBackgroundTaskService : IEditorBackgroundTaskService
{
    private readonly object gate_ = new();
    private readonly Dictionary<EditorBackgroundTaskId, EditorBackgroundTaskSnapshot> tasks_ = [];

    public event EventHandler? TasksChanged;

    public EditorBackgroundTaskId Start(string operationId, string title, bool canCancel)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(operationId);
        ArgumentException.ThrowIfNullOrWhiteSpace(title);

        var id = EditorBackgroundTaskId.NewId();
        var snapshot = new EditorBackgroundTaskSnapshot(
            id,
            operationId,
            title,
            EditorBackgroundTaskState.Running,
            Progress: null,
            Message: null,
            canCancel);

        lock (gate_)
        {
            tasks_.Add(id, snapshot);
        }

        OnTasksChanged();
        return id;
    }

    public void Report(EditorBackgroundTaskId id, double? progress, string? message)
    {
        lock (gate_)
        {
            var snapshot = GetSnapshotCore(id);
            ThrowIfTerminal(snapshot);
            tasks_[id] = snapshot with { Progress = progress, Message = message };
        }

        OnTasksChanged();
    }

    public void Complete(EditorBackgroundTaskId id, string? message)
    {
        SetTerminalState(id, EditorBackgroundTaskState.Completed, message);
    }

    public void Fail(EditorBackgroundTaskId id, string message)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(message);

        SetTerminalState(id, EditorBackgroundTaskState.Failed, message);
    }

    public void Cancel(EditorBackgroundTaskId id, string? message)
    {
        SetTerminalState(id, EditorBackgroundTaskState.Canceled, message);
    }

    public EditorBackgroundTaskSnapshot GetSnapshot(EditorBackgroundTaskId id)
    {
        lock (gate_)
        {
            return GetSnapshotCore(id);
        }
    }

    public IReadOnlyList<EditorBackgroundTaskSnapshot> GetActiveSnapshots()
    {
        lock (gate_)
        {
            return tasks_.Values
                .Where(static snapshot => snapshot.State == EditorBackgroundTaskState.Running)
                .ToArray();
        }
    }

    private void SetTerminalState(
        EditorBackgroundTaskId id,
        EditorBackgroundTaskState state,
        string? message)
    {
        lock (gate_)
        {
            var snapshot = GetSnapshotCore(id);
            ThrowIfTerminal(snapshot);
            tasks_[id] = snapshot with { State = state, Message = message };
        }

        OnTasksChanged();
    }

    private void OnTasksChanged()
    {
        TasksChanged?.Invoke(this, EventArgs.Empty);
    }

    private static void ThrowIfTerminal(EditorBackgroundTaskSnapshot snapshot)
    {
        if (snapshot.State != EditorBackgroundTaskState.Running)
        {
            throw new InvalidOperationException(
                $"Editor background task '{snapshot.Id.Value}' is already {snapshot.State}.");
        }
    }

    private EditorBackgroundTaskSnapshot GetSnapshotCore(EditorBackgroundTaskId id)
    {
        if (tasks_.TryGetValue(id, out var snapshot))
        {
            return snapshot;
        }

        throw new KeyNotFoundException(
            $"No editor background task exists for id '{id.Value}'.");
    }
}
