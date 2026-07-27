using Asharia.Runtime;

namespace Asharia.Studio.EngineBridge.Scene.Abi;

internal interface ISceneNativeApi
{
    SceneNativeStatus CreateWorld(
        in SceneNativeWorldCreateRequest request,
        out nint world);

    SceneNativeStatus DestroyWorld(nint world);

    SceneNativeStatus CreateEntity(
        nint world,
        in SceneNativeCreateEntityRequest request,
        out EntityId entity);

    SceneNativeStatus DestroyEntity(
        nint world,
        in SceneNativeEntityRequest request);

    SceneNativeStatus IsAlive(
        nint world,
        in SceneNativeEntityRequest request,
        out uint isAlive);
}
