using System.Runtime.InteropServices;

namespace Asharia.Studio.EngineBridge.Project.Abi;

internal static class ProjectNativeAbi
{
    public const uint Version = 1;
}

[StructLayout(LayoutKind.Explicit, Size = 8)]
internal readonly record struct ProjectNativeAbiHeader(
    [field: FieldOffset(0)] uint AbiVersion,
    [field: FieldOffset(4)] uint StructSize);

[StructLayout(LayoutKind.Explicit, Size = 16)]
internal readonly record struct ProjectNativeStringView(
    [field: FieldOffset(0)] nint Data,
    [field: FieldOffset(8)] ulong ByteLength);

[StructLayout(LayoutKind.Explicit, Size = 24)]
internal readonly record struct ProjectNativeOpenRequest(
    [field: FieldOffset(0)] ProjectNativeAbiHeader Header,
    [field: FieldOffset(8)] ProjectNativeStringView ProjectRootUtf8)
{
    public const uint StructSize = 24;

    public static ProjectNativeOpenRequest Current(
        nint projectRootUtf8,
        ulong projectRootByteLength)
    {
        return new ProjectNativeOpenRequest(
            new ProjectNativeAbiHeader(ProjectNativeAbi.Version, StructSize),
            new ProjectNativeStringView(
                projectRootUtf8,
                projectRootByteLength));
    }
}

[StructLayout(LayoutKind.Explicit, Size = 56)]
internal readonly record struct ProjectNativeCreateRequest(
    [field: FieldOffset(0)] ProjectNativeAbiHeader Header,
    [field: FieldOffset(8)] ProjectNativeStringView ProjectRootUtf8,
    [field: FieldOffset(24)] ProjectNativeStringView ProjectNameUtf8,
    [field: FieldOffset(40)] ProjectNativeStringView ProjectIdUtf8)
{
    public const uint StructSize = 56;

    public static ProjectNativeCreateRequest Current(
        nint projectRootUtf8,
        ulong projectRootByteLength,
        nint projectNameUtf8,
        ulong projectNameByteLength,
        nint projectIdUtf8,
        ulong projectIdByteLength)
    {
        return new ProjectNativeCreateRequest(
            new ProjectNativeAbiHeader(ProjectNativeAbi.Version, StructSize),
            new ProjectNativeStringView(
                projectRootUtf8,
                projectRootByteLength),
            new ProjectNativeStringView(
                projectNameUtf8,
                projectNameByteLength),
            new ProjectNativeStringView(
                projectIdUtf8,
                projectIdByteLength));
    }
}

[StructLayout(LayoutKind.Explicit, Size = 80)]
internal readonly record struct ProjectNativeResult(
    [field: FieldOffset(0)] ProjectNativeAbiHeader Header,
    [field: FieldOffset(8)] ProjectNativeStatus Status,
    [field: FieldOffset(16)] nint ProjectRootUtf8,
    [field: FieldOffset(24)] ulong ProjectRootByteLength,
    [field: FieldOffset(32)] nint ProjectNameUtf8,
    [field: FieldOffset(40)] ulong ProjectNameByteLength,
    [field: FieldOffset(48)] nint ProjectIdUtf8,
    [field: FieldOffset(56)] ulong ProjectIdByteLength,
    [field: FieldOffset(64)] nint MessageUtf8,
    [field: FieldOffset(72)] ulong MessageByteLength)
{
    public const uint StructSize = 80;
}
