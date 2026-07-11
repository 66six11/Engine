using System;

namespace Asharia.Editor.Selection;

public sealed class EditorSelectionChangedEventArgs(
    EditorSelectionSnapshot previous,
    EditorSelectionSnapshot current)
    : EventArgs
{
    public EditorSelectionSnapshot Previous { get; } = previous;

    public EditorSelectionSnapshot Current { get; } = current;
}
