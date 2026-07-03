using System;
using System.Collections.Generic;
using Editor.Core.Models.Selection;

namespace Editor.Core.Abstractions;

public interface IEditorSelectionService
{
    event EventHandler<EditorSelectionChangedEventArgs>? SelectionChanged;

    EditorSelectionSnapshot Current { get; }

    void SetActiveContext(string activeContextId);

    void ReplaceSelection(string activeContextId, IReadOnlyList<EditorSelectionItem> items);

    void ClearSelection(string activeContextId);
}
