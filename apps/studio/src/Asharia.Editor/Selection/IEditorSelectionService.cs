using System;
using System.Collections.Generic;

namespace Asharia.Editor.Selection;

public interface IEditorSelectionService
{
    event EventHandler<EditorSelectionChangedEventArgs>? SelectionChanged;

    EditorSelectionSnapshot Current { get; }

    void SetActiveContext(string activeContextId);

    void ReplaceSelection(string activeContextId, IReadOnlyList<EditorSelectionItem> items);

    void ClearSelection(string activeContextId);
}
