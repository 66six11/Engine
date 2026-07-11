using System;

namespace Asharia.Editor.Tasks;

public readonly record struct EditorBackgroundTaskId(Guid Value)
{
    public static EditorBackgroundTaskId NewId() => new(Guid.NewGuid());
}
