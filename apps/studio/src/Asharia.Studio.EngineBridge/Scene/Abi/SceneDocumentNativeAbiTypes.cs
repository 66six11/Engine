using System.Runtime.InteropServices;
using Asharia.Runtime;

namespace Asharia.Studio.EngineBridge.Scene.Abi;

[StructLayout(LayoutKind.Explicit, Size = 8)]
internal readonly record struct SceneNativeDocumentHandle(
    [field: FieldOffset(0)] uint Index,
    [field: FieldOffset(4)] uint Generation)
{
    public bool IsValid => Index != 0 && Generation != 0;
}

[StructLayout(LayoutKind.Explicit, Size = 16)]
internal readonly record struct SceneNativeTextSpan(
    [field: FieldOffset(0)] ulong ByteOffset,
    [field: FieldOffset(8)] ulong ByteLength);

[StructLayout(LayoutKind.Explicit, Size = 40)]
internal readonly record struct SceneNativeDocumentOpenDefaultRequest(
    [field: FieldOffset(0)] SceneNativeAbiHeader Header,
    [field: FieldOffset(8)] SceneNativeStringView ProjectRootUtf8,
    [field: FieldOffset(24)] SceneNativeStringView NewSceneIdUtf8)
{
    public const uint StructSize = 40;

    public static SceneNativeDocumentOpenDefaultRequest Current(
        nint projectRootUtf8,
        ulong projectRootByteLength,
        nint newSceneIdUtf8,
        ulong newSceneIdByteLength) =>
        new(
            new SceneNativeAbiHeader(SceneNativeAbi.Version, StructSize),
            new SceneNativeStringView(projectRootUtf8, projectRootByteLength),
            new SceneNativeStringView(newSceneIdUtf8, newSceneIdByteLength));
}

[StructLayout(LayoutKind.Explicit, Size = 16)]
internal readonly record struct SceneNativeDocumentRequest(
    [field: FieldOffset(0)] SceneNativeAbiHeader Header,
    [field: FieldOffset(8)] SceneNativeDocumentHandle Document)
{
    public const uint StructSize = 16;

    public static SceneNativeDocumentRequest Current(SceneNativeDocumentHandle document) =>
        new(new SceneNativeAbiHeader(SceneNativeAbi.Version, StructSize), document);
}

[StructLayout(LayoutKind.Explicit, Size = 56)]
internal readonly record struct SceneNativeDocumentCreateEntityRequest(
    [field: FieldOffset(0)] SceneNativeAbiHeader Header,
    [field: FieldOffset(8)] SceneNativeDocumentHandle Document,
    [field: FieldOffset(16)] ulong ExpectedRevision,
    [field: FieldOffset(24)] SceneNativeStringView ObjectIdUtf8,
    [field: FieldOffset(40)] SceneNativeStringView NameUtf8)
{
    public const uint StructSize = 56;
}

[StructLayout(LayoutKind.Explicit, Size = 56)]
internal readonly record struct SceneNativeDocumentSetEntityNameRequest(
    [field: FieldOffset(0)] SceneNativeAbiHeader Header,
    [field: FieldOffset(8)] SceneNativeDocumentHandle Document,
    [field: FieldOffset(16)] ulong ExpectedRevision,
    [field: FieldOffset(24)] SceneNativeStringView ObjectIdUtf8,
    [field: FieldOffset(40)] SceneNativeStringView NameUtf8)
{
    public const uint StructSize = 56;
}

[StructLayout(LayoutKind.Explicit, Size = 80)]
internal readonly record struct SceneNativeDocumentSetEntityTransformRequest(
    [field: FieldOffset(0)] SceneNativeAbiHeader Header,
    [field: FieldOffset(8)] SceneNativeDocumentHandle Document,
    [field: FieldOffset(16)] ulong ExpectedRevision,
    [field: FieldOffset(24)] SceneNativeStringView ObjectIdUtf8,
    [field: FieldOffset(40)] TransformValue Transform)
{
    public const uint StructSize = 80;
}

[StructLayout(LayoutKind.Explicit, Size = 24)]
internal readonly record struct SceneNativeDocumentSaveRequest(
    [field: FieldOffset(0)] SceneNativeAbiHeader Header,
    [field: FieldOffset(8)] SceneNativeDocumentHandle Document,
    [field: FieldOffset(16)] ulong ExpectedRevision)
{
    public const uint StructSize = 24;
}

[StructLayout(LayoutKind.Explicit, Size = 48)]
internal readonly record struct SceneNativeDocumentOperationResult(
    [field: FieldOffset(0)] SceneNativeStatus OperationStatus,
    [field: FieldOffset(4)] uint Reserved,
    [field: FieldOffset(8)] ulong RequiredByteLength,
    [field: FieldOffset(16)] ulong Revision,
    [field: FieldOffset(24)] ulong SavedRevision,
    [field: FieldOffset(32)] SceneNativeTextSpan MessageUtf8);

[StructLayout(LayoutKind.Explicit, Size = 72)]
internal readonly record struct SceneNativeDocumentEntitySnapshot(
    [field: FieldOffset(0)] SceneNativeTextSpan ObjectIdUtf8,
    [field: FieldOffset(16)] SceneNativeTextSpan NameUtf8,
    [field: FieldOffset(32)] TransformValue Transform)
{
    public const int StructSize = 72;
}

[StructLayout(LayoutKind.Explicit, Size = 80)]
internal readonly record struct SceneNativeDocumentSnapshotResult(
    [field: FieldOffset(0)] SceneNativeStatus OperationStatus,
    [field: FieldOffset(4)] uint Reserved,
    [field: FieldOffset(8)] ulong RequiredByteLength,
    [field: FieldOffset(16)] ulong Revision,
    [field: FieldOffset(24)] ulong SavedRevision,
    [field: FieldOffset(32)] ulong EntityCount,
    [field: FieldOffset(40)] ulong EntitiesOffset,
    [field: FieldOffset(48)] SceneNativeTextSpan SceneIdUtf8,
    [field: FieldOffset(64)] SceneNativeTextSpan MessageUtf8);
