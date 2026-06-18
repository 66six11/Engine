using System;
using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Core.Services;

public sealed class InMemorySceneSnapshotProvider : ISceneSnapshotProvider
{
    private readonly Dictionary<string, SceneObjectSnapshot> objectsById_;

    public InMemorySceneSnapshotProvider(SceneSnapshot current)
    {
        ArgumentNullException.ThrowIfNull(current);

        Current = current;
        objectsById_ = new Dictionary<string, SceneObjectSnapshot>(StringComparer.Ordinal);

        foreach (var sceneObject in current.Objects)
        {
            if (!objectsById_.TryAdd(sceneObject.Id, sceneObject))
            {
                throw new InvalidOperationException(
                    $"Scene snapshot contains duplicate object id '{sceneObject.Id}'.");
            }
        }
    }

    public SceneSnapshot Current { get; }

    public bool TryGetObject(string objectId, out SceneObjectSnapshot? sceneObject)
    {
        if (string.IsNullOrWhiteSpace(objectId))
        {
            sceneObject = null;
            return false;
        }

        return objectsById_.TryGetValue(objectId, out sceneObject);
    }
}
