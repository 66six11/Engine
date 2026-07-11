using System;

namespace Asharia.Editor.Transactions;

public readonly record struct EditorTransactionId(Guid Value)
{
    public static EditorTransactionId NewId() => new(Guid.NewGuid());
}
