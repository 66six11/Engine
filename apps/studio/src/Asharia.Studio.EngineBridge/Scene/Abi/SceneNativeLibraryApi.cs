using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Asharia.Studio.EngineBridge.Scene.Abi;

internal sealed class SceneNativeLibraryApi : ISceneNativeApi
{
    public static SceneNativeLibraryApi Instance { get; } = new();

    private SceneNativeLibraryApi()
    {
    }

    public SceneNativeStatus CreateWorld(
        in SceneNativeWorldCreateRequest request,
        out nint world)
    {
        return (SceneNativeStatus)SceneNativeEntryPoints.CreateWorld(
            in request,
            out world);
    }

    public SceneNativeStatus DestroyWorld(nint world)
    {
        return (SceneNativeStatus)SceneNativeEntryPoints.DestroyWorld(world);
    }
}

internal static partial class SceneNativeEntryPoints
{
    private const string LibraryName = "asharia_scene_native";

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_world_create")]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial uint CreateWorld(
        in SceneNativeWorldCreateRequest request,
        out nint world);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_world_destroy")]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial uint DestroyWorld(nint world);
}
