using System;
using Editor.Core.Models;
using Editor.Core.Models.Scene;

namespace Editor.Core.Abstractions;

public interface ISceneSnapshotProvider
{
    event EventHandler? SnapshotChanged;

    SceneSnapshot GetCurrentSnapshot();

    bool TryGetObject(string objectId, out SceneObjectSnapshot? sceneObject);
}
