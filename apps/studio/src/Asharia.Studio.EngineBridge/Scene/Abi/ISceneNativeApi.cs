namespace Asharia.Studio.EngineBridge.Scene.Abi;

internal interface ISceneNativeApi
{
    SceneNativeStatus CreateWorld(
        in SceneNativeWorldCreateRequest request,
        out nint world);

    SceneNativeStatus DestroyWorld(nint world);
}
