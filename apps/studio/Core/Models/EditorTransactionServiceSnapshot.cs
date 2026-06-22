using System.Collections.Generic;

namespace Editor.Core.Models;

public sealed record EditorTransactionServiceSnapshot(
    EditorTransactionId? ActiveTransactionId,
    string ActiveTransactionLabel,
    bool IsDirty,
    bool CanUndo,
    bool CanRedo,
    IReadOnlyList<string> Diagnostics)
{
    public static EditorTransactionServiceSnapshot Clean { get; } = new(
        ActiveTransactionId: null,
        ActiveTransactionLabel: string.Empty,
        IsDirty: false,
        CanUndo: false,
        CanRedo: false,
        Diagnostics: []);
}
