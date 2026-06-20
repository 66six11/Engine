using System;

namespace Editor.Core.Models;

public readonly record struct EditorBackgroundTaskId(Guid Value)
{
    public static EditorBackgroundTaskId NewId() => new(Guid.NewGuid());
}
