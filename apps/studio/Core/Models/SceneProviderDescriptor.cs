using System;
using Editor.Core.Abstractions;

namespace Editor.Core.Models;

public sealed record SceneProviderDescriptor
{
    public SceneProviderDescriptor(
        string id,
        string role,
        Func<ISceneSnapshotProvider> createProvider)
    {
        if (string.IsNullOrWhiteSpace(id))
        {
            throw new ArgumentException("Scene provider id must not be empty.", nameof(id));
        }

        if (string.IsNullOrWhiteSpace(role))
        {
            throw new ArgumentException("Scene provider role must not be empty.", nameof(role));
        }

        ArgumentNullException.ThrowIfNull(createProvider);

        Id = id;
        Role = role;
        CreateProvider = createProvider;
    }

    public string Id { get; }

    public string Role { get; }

    public Func<ISceneSnapshotProvider> CreateProvider { get; }
}
