using System;

namespace Editor.Core.Models;

public readonly record struct EditorTransactionId(Guid Value)
{
    public static EditorTransactionId NewId() => new(Guid.NewGuid());
}
