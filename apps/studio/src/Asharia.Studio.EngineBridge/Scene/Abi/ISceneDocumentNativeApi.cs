namespace Asharia.Studio.EngineBridge.Scene.Abi;

internal interface ISceneDocumentNativeApi
{
    SceneNativeStatus OpenDefault(
        in SceneNativeDocumentOpenDefaultRequest request,
        out SceneNativeDocumentHandle document,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result);

    SceneNativeStatus Close(ref SceneNativeDocumentHandle document);

    SceneNativeStatus Snapshot(
        in SceneNativeDocumentRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentSnapshotResult result);

    SceneNativeStatus CreateEntity(
        in SceneNativeDocumentCreateEntityRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result);

    SceneNativeStatus CreateMeshEntity(
        in SceneNativeDocumentCreateMeshEntityRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result);

    SceneNativeStatus SetEntityName(
        in SceneNativeDocumentSetEntityNameRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result);

    SceneNativeStatus SetEntityTransform(
        in SceneNativeDocumentSetEntityTransformRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentTransformOperationResult result);

    SceneNativeStatus SetEntityMesh(
        in SceneNativeDocumentSetEntityMeshRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentMeshOperationResult result);

    SceneNativeStatus Save(
        in SceneNativeDocumentSaveRequest request,
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result);
}
