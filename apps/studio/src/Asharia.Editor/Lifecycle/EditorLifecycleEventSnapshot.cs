using System;

namespace Asharia.Editor.Lifecycle;

public sealed record EditorLifecycleEventSnapshot(
    long Sequence,
    EditorLifecycleEventKind Kind,
    string Source,
    string? Message,
    DateTimeOffset OccurredAtUtc);
