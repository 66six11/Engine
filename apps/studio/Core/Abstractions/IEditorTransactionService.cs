using System;
using Editor.Core.Models.Transactions;

namespace Editor.Core.Abstractions;

public interface IEditorTransactionService
{
    event EventHandler? StateChanged;

    EditorTransactionServiceSnapshot Current { get; }

    EditorTransactionId Begin(string displayLabel);

    void AddCommand(EditorTransactionId transactionId, IEditorEditCommand command);

    void Commit(EditorTransactionId transactionId);

    void Rollback(EditorTransactionId transactionId);

    void Undo();

    void Redo();
}
