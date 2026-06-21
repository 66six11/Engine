using System;
using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Core.Services;

public sealed class InMemorySceneSnapshotProvider : ISceneSnapshotProvider
{
    private Dictionary<string, SceneObjectSnapshot> objectsById_;
    private SceneSnapshot current_;

    public InMemorySceneSnapshotProvider(SceneSnapshot current)
    {
        ArgumentNullException.ThrowIfNull(current);

        current_ = current;
        objectsById_ = BuildObjectIndex(current);
    }

    public event EventHandler? SnapshotChanged;

    public SceneSnapshot GetCurrentSnapshot()
    {
        return current_;
    }

    public void ReplaceSnapshot(SceneSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);

        var objectsById = BuildObjectIndex(snapshot);
        current_ = snapshot;
        objectsById_ = objectsById;
        SnapshotChanged?.Invoke(this, EventArgs.Empty);
    }

    public bool TryGetObject(string objectId, out SceneObjectSnapshot? sceneObject)
    {
        if (string.IsNullOrWhiteSpace(objectId))
        {
            sceneObject = null;
            return false;
        }

        return objectsById_.TryGetValue(objectId, out sceneObject);
    }

    private static Dictionary<string, SceneObjectSnapshot> BuildObjectIndex(SceneSnapshot snapshot)
    {
        var objectsById = new Dictionary<string, SceneObjectSnapshot>(StringComparer.Ordinal);
        foreach (var sceneObject in snapshot.Objects)
        {
            if (!objectsById.TryAdd(sceneObject.Id, sceneObject))
            {
                throw new InvalidOperationException(
                    $"Scene snapshot contains duplicate object id '{sceneObject.Id}'.");
            }
        }

        return objectsById;
    }
}
