using System;
using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface ISceneSnapshotProvider
{
    event EventHandler? SnapshotChanged;

    SceneSnapshot Current { get; }

    bool TryGetObject(string objectId, out SceneObjectSnapshot? sceneObject);
}
