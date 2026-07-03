using System;
using System.Collections.Generic;
using System.Linq;
using Editor.Core.Abstractions;
using Editor.Core.Models.Transactions;

namespace Editor.Shell.Services;

public sealed class EditorTransactionService : IEditorTransactionService
{
    private readonly List<TransactionEntry> undoStack_ = [];
    private readonly List<TransactionEntry> redoStack_ = [];
    private readonly List<string> diagnostics_ = [];
    private PendingTransaction? activeTransaction_;

    public event EventHandler? StateChanged;

    public EditorTransactionServiceSnapshot Current => CreateSnapshot();

    public EditorTransactionId Begin(string displayLabel)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(displayLabel);
        if (activeTransaction_ is not null)
        {
            throw new InvalidOperationException("An editor transaction is already active.");
        }

        var id = EditorTransactionId.NewId();
        activeTransaction_ = new PendingTransaction(id, displayLabel);
        OnStateChanged();
        return id;
    }

    public void AddCommand(EditorTransactionId transactionId, IEditorEditCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);

        var transaction = GetActiveTransaction(transactionId);
        var descriptor = command.Descriptor;
        if (!descriptor.Validation.IsValid)
        {
            var message = descriptor.Validation.Message ?? "Editor edit command failed validation.";
            diagnostics_.Add(message);
            OnStateChanged();
            throw new InvalidOperationException(message);
        }

        try
        {
            command.Apply();
        }
        catch (Exception exception)
        {
            FailActiveTransaction(transaction, exception);
            throw new InvalidOperationException(
                $"Editor transaction '{transaction.DisplayLabel}' failed while applying '{descriptor.DisplayLabel}': {exception.Message}",
                exception);
        }

        transaction.Commands.Add(command);
        OnStateChanged();
    }

    public void Commit(EditorTransactionId transactionId)
    {
        var transaction = GetActiveTransaction(transactionId);
        activeTransaction_ = null;

        if (transaction.Commands.Count > 0)
        {
            undoStack_.Add(new TransactionEntry(transaction.DisplayLabel, transaction.Commands.ToArray()));
            redoStack_.Clear();
        }

        OnStateChanged();
    }

    public void Rollback(EditorTransactionId transactionId)
    {
        var transaction = GetActiveTransaction(transactionId);
        activeTransaction_ = null;

        RevertCommands(transaction.Commands);
        OnStateChanged();
    }

    public void Undo()
    {
        if (activeTransaction_ is not null)
        {
            throw new InvalidOperationException("Cannot undo while an editor transaction is active.");
        }

        if (undoStack_.Count == 0)
        {
            throw new InvalidOperationException("No editor transaction is available to undo.");
        }

        var entry = undoStack_[^1];
        undoStack_.RemoveAt(undoStack_.Count - 1);
        RevertCommands(entry.Commands);
        redoStack_.Add(entry);
        OnStateChanged();
    }

    public void Redo()
    {
        if (activeTransaction_ is not null)
        {
            throw new InvalidOperationException("Cannot redo while an editor transaction is active.");
        }

        if (redoStack_.Count == 0)
        {
            throw new InvalidOperationException("No editor transaction is available to redo.");
        }

        var entry = redoStack_[^1];
        redoStack_.RemoveAt(redoStack_.Count - 1);
        ApplyCommands(entry.Commands);
        undoStack_.Add(entry);
        OnStateChanged();
    }

    private EditorTransactionServiceSnapshot CreateSnapshot()
    {
        return new EditorTransactionServiceSnapshot(
            activeTransaction_?.Id,
            activeTransaction_?.DisplayLabel ?? string.Empty,
            undoStack_.Count > 0,
            undoStack_.Count > 0,
            redoStack_.Count > 0,
            diagnostics_.ToArray());
    }

    private PendingTransaction GetActiveTransaction(EditorTransactionId transactionId)
    {
        if (activeTransaction_ is null)
        {
            throw new InvalidOperationException("No editor transaction is active.");
        }

        if (activeTransaction_.Id != transactionId)
        {
            throw new InvalidOperationException(
                $"Editor transaction '{transactionId.Value}' is not active.");
        }

        return activeTransaction_;
    }

    private static void ApplyCommands(IReadOnlyList<IEditorEditCommand> commands)
    {
        foreach (var command in commands)
        {
            command.Apply();
        }
    }

    private static void RevertCommands(IReadOnlyList<IEditorEditCommand> commands)
    {
        foreach (var command in commands.Reverse())
        {
            command.Revert();
        }
    }

    private void FailActiveTransaction(PendingTransaction transaction, Exception exception)
    {
        diagnostics_.Add(exception.Message);
        activeTransaction_ = null;
        RevertCommands(transaction.Commands);
        OnStateChanged();
    }

    private void OnStateChanged()
    {
        StateChanged?.Invoke(this, EventArgs.Empty);
    }

    private sealed class PendingTransaction(EditorTransactionId id, string displayLabel)
    {
        public EditorTransactionId Id { get; } = id;

        public string DisplayLabel { get; } = displayLabel;

        public List<IEditorEditCommand> Commands { get; } = [];
    }

    private sealed record TransactionEntry(
        string DisplayLabel,
        IReadOnlyList<IEditorEditCommand> Commands);
}
