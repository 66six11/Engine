using System;
using Asharia.Editor.Worlds.Snapshots;

namespace Asharia.Studio.Application.Providers;

public sealed class EditorSceneProviderRegistration
{
    public EditorSceneProviderRegistration(
        string id,
        string role,
        Func<ISceneSnapshotProvider> createProvider)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);
        ArgumentException.ThrowIfNullOrWhiteSpace(role);
        ArgumentNullException.ThrowIfNull(createProvider);

        Id = id;
        Role = role;
        CreateProvider = createProvider;
    }

    public string Id { get; }

    public string Role { get; }

    public Func<ISceneSnapshotProvider> CreateProvider { get; }
}
