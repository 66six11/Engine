using System;

namespace Editor.Core.Models.Lifecycle;

public sealed record EditorLifecycleEventSnapshot(
    long Sequence,
    EditorLifecycleEventKind Kind,
    string Source,
    string? Message,
    DateTimeOffset OccurredAtUtc);
