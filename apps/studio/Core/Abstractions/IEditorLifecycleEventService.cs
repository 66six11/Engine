using System;
using System.Collections.Generic;
using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface IEditorLifecycleEventService
{
    event EventHandler? EventsChanged;

    EditorLifecycleEventSnapshot Publish(
        EditorLifecycleEventKind kind,
        string source,
        string? message = null);

    IReadOnlyList<EditorLifecycleEventSnapshot> GetRecentEvents();
}
