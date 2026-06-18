using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface ISceneSnapshotProvider
{
    SceneSnapshot Current { get; }

    bool TryGetObject(string objectId, out SceneObjectSnapshot? sceneObject);
}
