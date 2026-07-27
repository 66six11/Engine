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

    SceneNativeStatus GetLocalTransform(
        nint world,
        in SceneNativeEntityRequest request,
        out TransformValue transform);

    SceneNativeStatus SetLocalTransform(
        nint world,
        in SceneNativeSetLocalTransformRequest request);

    SceneNativeStatus GetEntityName(
        nint world,
        in SceneNativeEntityRequest request,
        nint nameUtf8,
        ulong nameCapacity,
        out ulong nameByteLength);

    SceneNativeStatus SetEntityName(
        nint world,
        in SceneNativeSetEntityNameRequest request);
}
