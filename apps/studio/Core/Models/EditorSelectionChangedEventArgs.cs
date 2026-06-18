using System;

namespace Editor.Core.Models;

public sealed class EditorSelectionChangedEventArgs(
    EditorSelectionSnapshot previous,
    EditorSelectionSnapshot current)
    : EventArgs
{
    public EditorSelectionSnapshot Previous { get; } = previous;

    public EditorSelectionSnapshot Current { get; } = current;
}
