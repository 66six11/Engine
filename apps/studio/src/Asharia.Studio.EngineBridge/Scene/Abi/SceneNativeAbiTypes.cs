using System.Runtime.InteropServices;
using Asharia.Runtime;

namespace Asharia.Studio.EngineBridge.Scene.Abi;

internal static class SceneNativeAbi
{
    public const uint Version = 1;
}

[StructLayout(LayoutKind.Explicit, Size = 8)]
internal readonly record struct SceneNativeAbiHeader(
    [field: FieldOffset(0)] uint AbiVersion,
    [field: FieldOffset(4)] uint StructSize);

[StructLayout(LayoutKind.Explicit, Size = 8)]
internal readonly record struct SceneNativeWorldCreateRequest(
    [field: FieldOffset(0)] SceneNativeAbiHeader Header)
{
    public const uint StructSize = 8;

    public static SceneNativeWorldCreateRequest Current { get; } =
        new(new SceneNativeAbiHeader(SceneNativeAbi.Version, StructSize));
}

[StructLayout(LayoutKind.Explicit, Size = 8)]
internal readonly record struct SceneNativeCreateEntityRequest(
    [field: FieldOffset(0)] SceneNativeAbiHeader Header)
{
    public const uint StructSize = 8;

    public static SceneNativeCreateEntityRequest Current { get; } =
        new(new SceneNativeAbiHeader(SceneNativeAbi.Version, StructSize));
}

[StructLayout(LayoutKind.Explicit, Size = 16)]
internal readonly record struct SceneNativeEntityRequest(
    [field: FieldOffset(0)] SceneNativeAbiHeader Header,
    [field: FieldOffset(8)] EntityId Entity)
{
    public const uint StructSize = 16;

    public static SceneNativeEntityRequest Current(EntityId entity)
    {
        return new SceneNativeEntityRequest(
            new SceneNativeAbiHeader(SceneNativeAbi.Version, StructSize),
            entity);
    }
}

[StructLayout(LayoutKind.Explicit, Size = 56)]
internal readonly record struct SceneNativeSetLocalTransformRequest(
    [field: FieldOffset(0)] SceneNativeAbiHeader Header,
    [field: FieldOffset(8)] EntityId Entity,
    [field: FieldOffset(16)] TransformValue Transform)
{
    public const uint StructSize = 56;

    public static SceneNativeSetLocalTransformRequest Current(
        EntityId entity,
        TransformValue transform)
    {
        return new SceneNativeSetLocalTransformRequest(
            new SceneNativeAbiHeader(SceneNativeAbi.Version, StructSize),
            entity,
            transform);
    }
}

[StructLayout(LayoutKind.Explicit, Size = 16)]
internal readonly record struct SceneNativeStringView(
    [field: FieldOffset(0)] nint Data,
    [field: FieldOffset(8)] ulong ByteLength);

[StructLayout(LayoutKind.Explicit, Size = 32)]
internal readonly record struct SceneNativeSetEntityNameRequest(
    [field: FieldOffset(0)] SceneNativeAbiHeader Header,
    [field: FieldOffset(8)] EntityId Entity,
    [field: FieldOffset(16)] SceneNativeStringView NameUtf8)
{
    public const uint StructSize = 32;

    public static SceneNativeSetEntityNameRequest Current(
        EntityId entity,
        nint data,
        ulong byteLength)
    {
        return new SceneNativeSetEntityNameRequest(
            new SceneNativeAbiHeader(SceneNativeAbi.Version, StructSize),
            entity,
            new SceneNativeStringView(data, byteLength));
    }
}
