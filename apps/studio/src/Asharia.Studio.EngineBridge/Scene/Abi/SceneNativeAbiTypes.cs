using System.Runtime.InteropServices;

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
