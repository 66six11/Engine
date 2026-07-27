using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Asharia.Runtime;

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

    public SceneNativeStatus CreateEntity(
        nint world,
        in SceneNativeCreateEntityRequest request,
        out EntityId entity)
    {
        return (SceneNativeStatus)SceneNativeEntryPoints.CreateEntity(
            world,
            in request,
            out entity);
    }

    public SceneNativeStatus DestroyEntity(
        nint world,
        in SceneNativeEntityRequest request)
    {
        return (SceneNativeStatus)SceneNativeEntryPoints.DestroyEntity(
            world,
            in request);
    }

    public SceneNativeStatus IsAlive(
        nint world,
        in SceneNativeEntityRequest request,
        out uint isAlive)
    {
        return (SceneNativeStatus)SceneNativeEntryPoints.IsAlive(
            world,
            in request,
            out isAlive);
    }

    public SceneNativeStatus GetLocalTransform(
        nint world,
        in SceneNativeEntityRequest request,
        out TransformValue transform)
    {
        return (SceneNativeStatus)SceneNativeEntryPoints.GetLocalTransform(
            world,
            in request,
            out transform);
    }

    public SceneNativeStatus SetLocalTransform(
        nint world,
        in SceneNativeSetLocalTransformRequest request)
    {
        return (SceneNativeStatus)SceneNativeEntryPoints.SetLocalTransform(
            world,
            in request);
    }

    public SceneNativeStatus GetEntityName(
        nint world,
        in SceneNativeEntityRequest request,
        nint nameUtf8,
        ulong nameCapacity,
        out ulong nameByteLength)
    {
        return (SceneNativeStatus)SceneNativeEntryPoints.GetEntityName(
            world,
            in request,
            nameUtf8,
            nameCapacity,
            out nameByteLength);
    }

    public SceneNativeStatus SetEntityName(
        nint world,
        in SceneNativeSetEntityNameRequest request)
    {
        return (SceneNativeStatus)SceneNativeEntryPoints.SetEntityName(
            world,
            in request);
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

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_world_create_entity")]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial uint CreateEntity(
        nint world,
        in SceneNativeCreateEntityRequest request,
        out EntityId entity);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_world_destroy_entity")]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial uint DestroyEntity(
        nint world,
        in SceneNativeEntityRequest request);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_world_is_alive")]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial uint IsAlive(
        nint world,
        in SceneNativeEntityRequest request,
        out uint isAlive);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_world_get_local_transform")]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial uint GetLocalTransform(
        nint world,
        in SceneNativeEntityRequest request,
        out TransformValue transform);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_world_set_local_transform")]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial uint SetLocalTransform(
        nint world,
        in SceneNativeSetLocalTransformRequest request);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_world_get_entity_name")]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial uint GetEntityName(
        nint world,
        in SceneNativeEntityRequest request,
        nint nameUtf8,
        ulong nameCapacity,
        out ulong nameByteLength);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_world_set_entity_name")]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial uint SetEntityName(
        nint world,
        in SceneNativeSetEntityNameRequest request);
}
