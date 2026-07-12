using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Selection;

namespace Asharia.Studio.Application.Selection;

public sealed class EditorSelectionService : IEditorSelectionService
{
    private EditorSelectionSnapshot current_ = EditorSelectionSnapshot.Empty;

    public event EventHandler<EditorSelectionChangedEventArgs>? SelectionChanged;

    public EditorSelectionSnapshot Current => current_;

    public void SetActiveContext(string activeContextId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(activeContextId);

        SetCurrent(new EditorSelectionSnapshot(activeContextId, []));
    }

    public void ReplaceSelection(string activeContextId, IReadOnlyList<EditorSelectionItem> items)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(activeContextId);
        ArgumentNullException.ThrowIfNull(items);

        SetCurrent(new EditorSelectionSnapshot(activeContextId, items.ToArray()));
    }

    public void ClearSelection(string activeContextId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(activeContextId);

        SetCurrent(new EditorSelectionSnapshot(activeContextId, []));
    }

    private void SetCurrent(EditorSelectionSnapshot next)
    {
        if (AreEquivalent(current_, next))
        {
            return;
        }

        var previous = current_;
        current_ = next;
        SelectionChanged?.Invoke(this, new EditorSelectionChangedEventArgs(previous, current_));
    }

    private static bool AreEquivalent(
        EditorSelectionSnapshot left,
        EditorSelectionSnapshot right)
    {
        return string.Equals(left.ActiveContextId, right.ActiveContextId, StringComparison.Ordinal)
            && left.Items.SequenceEqual(right.Items);
    }
}
