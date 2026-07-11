using System;

namespace Asharia.Editor.Worlds.Snapshots;

public interface ISceneSnapshotProvider
{
    event EventHandler? SnapshotChanged;

    SceneSnapshot GetCurrentSnapshot();

    bool TryGetObject(string objectId, out SceneObjectSnapshot? sceneObject);
}
