using System;
using System.Collections.Generic;

namespace Asharia.Editor.Lifecycle;

public interface IEditorLifecycleEventService
{
    event EventHandler? EventsChanged;

    EditorLifecycleEventSnapshot Publish(
        EditorLifecycleEventKind kind,
        string source,
        string? message = null);

    IReadOnlyList<EditorLifecycleEventSnapshot> GetRecentEvents();
}
