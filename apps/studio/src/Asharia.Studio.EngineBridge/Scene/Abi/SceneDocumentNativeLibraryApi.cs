using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Asharia.Studio.EngineBridge.Scene.Abi;

internal sealed class SceneDocumentNativeLibraryApi : ISceneDocumentNativeApi
{
    public static SceneDocumentNativeLibraryApi Instance { get; } = new();

    private SceneDocumentNativeLibraryApi()
    {
    }

    public SceneNativeStatus OpenDefault(
        in SceneNativeDocumentOpenDefaultRequest request,
        out SceneNativeDocumentHandle document,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result) =>
        (SceneNativeStatus)SceneDocumentNativeEntryPoints.OpenDefault(
            in request,
            out document,
            responseBuffer,
            responseCapacity,
            out result);

    public SceneNativeStatus Close(ref SceneNativeDocumentHandle document) =>
        (SceneNativeStatus)SceneDocumentNativeEntryPoints.Close(ref document);

    public SceneNativeStatus Snapshot(
        in SceneNativeDocumentRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentSnapshotResult result) =>
        (SceneNativeStatus)SceneDocumentNativeEntryPoints.Snapshot(
            in request,
            responseBuffer,
            responseCapacity,
            out result);

    public SceneNativeStatus CreateEntity(
        in SceneNativeDocumentCreateEntityRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result) =>
        (SceneNativeStatus)SceneDocumentNativeEntryPoints.CreateEntity(
            in request,
            responseBuffer,
            responseCapacity,
            out result);

    public SceneNativeStatus CreateMeshEntity(
        in SceneNativeDocumentCreateMeshEntityRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result) =>
        (SceneNativeStatus)SceneDocumentNativeEntryPoints.CreateMeshEntity(
            in request,
            responseBuffer,
            responseCapacity,
            out result);

    public SceneNativeStatus SetEntityName(
        in SceneNativeDocumentSetEntityNameRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result) =>
        (SceneNativeStatus)SceneDocumentNativeEntryPoints.SetEntityName(
            in request,
            responseBuffer,
            responseCapacity,
            out result);

    public SceneNativeStatus SetEntityTransform(
        in SceneNativeDocumentSetEntityTransformRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentTransformOperationResult result) =>
        (SceneNativeStatus)SceneDocumentNativeEntryPoints.SetEntityTransform(
            in request,
            responseBuffer,
            responseCapacity,
            out result);

    public SceneNativeStatus Save(
        in SceneNativeDocumentSaveRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result) =>
        (SceneNativeStatus)SceneDocumentNativeEntryPoints.Save(
            in request,
            responseBuffer,
            responseCapacity,
            out result);
}

internal static partial class SceneDocumentNativeEntryPoints
{
    private const string LibraryName = "asharia_scene_native";

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_document_open_default")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint OpenDefault(
        in SceneNativeDocumentOpenDefaultRequest request,
        out SceneNativeDocumentHandle document,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_document_close")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint Close(ref SceneNativeDocumentHandle document);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_document_snapshot")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint Snapshot(
        in SceneNativeDocumentRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentSnapshotResult result);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_document_create_entity")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CreateEntity(
        in SceneNativeDocumentCreateEntityRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_document_create_mesh_entity")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CreateMeshEntity(
        in SceneNativeDocumentCreateMeshEntityRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_document_set_entity_name")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint SetEntityName(
        in SceneNativeDocumentSetEntityNameRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_document_set_entity_transform")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint SetEntityTransform(
        in SceneNativeDocumentSetEntityTransformRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentTransformOperationResult result);

    [LibraryImport(LibraryName, EntryPoint = "asharia_scene_document_save")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint Save(
        in SceneNativeDocumentSaveRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result);
}
