using System;
using System.Collections.Generic;

namespace Asharia.Editor.Worlds.Snapshots;

public sealed record SceneSnapshot
{
    public static SceneSnapshot Empty { get; } = new("scene:empty", "Empty Scene", 0, []);

    public SceneSnapshot(
        string id,
        string displayName,
        long revision,
        IReadOnlyList<SceneObjectSnapshot>? objects = null)
    {
        if (string.IsNullOrWhiteSpace(id))
        {
            throw new ArgumentException("Scene id cannot be null or whitespace.", nameof(id));
        }

        Id = id;
        DisplayName = string.IsNullOrWhiteSpace(displayName) ? id : displayName;
        Revision = Math.Max(0, revision);
        Objects = Array.AsReadOnly(objects is null ? Array.Empty<SceneObjectSnapshot>() : [.. objects]);
    }

    public string Id { get; }

    public string DisplayName { get; }

    public long Revision { get; }

    public IReadOnlyList<SceneObjectSnapshot> Objects { get; }
}
