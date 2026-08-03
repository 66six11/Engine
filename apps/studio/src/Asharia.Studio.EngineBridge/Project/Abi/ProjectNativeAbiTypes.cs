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
    [field: FieldOffset(8)] ProjectNativeStringView ProjectPathUtf8)
{
    public const uint StructSize = 24;

    public static ProjectNativeOpenRequest Current(
        nint projectPathUtf8,
        ulong projectPathByteLength) =>
        new(
            new ProjectNativeAbiHeader(ProjectNativeAbi.Version, StructSize),
            new ProjectNativeStringView(projectPathUtf8, projectPathByteLength));
}

[StructLayout(LayoutKind.Explicit, Size = 56)]
internal readonly record struct ProjectNativeCreateRequest(
    [field: FieldOffset(0)] ProjectNativeAbiHeader Header,
    [field: FieldOffset(8)] ProjectNativeStringView ParentDirectoryUtf8,
    [field: FieldOffset(24)] ProjectNativeStringView ProjectNameUtf8,
    [field: FieldOffset(40)] ProjectNativeStringView ProjectIdUtf8)
{
    public const uint StructSize = 56;

    public static ProjectNativeCreateRequest Current(
        nint parentDirectoryUtf8,
        ulong parentDirectoryByteLength,
        nint projectNameUtf8,
        ulong projectNameByteLength,
        nint projectIdUtf8,
        ulong projectIdByteLength) =>
        new(
            new ProjectNativeAbiHeader(ProjectNativeAbi.Version, StructSize),
            new ProjectNativeStringView(
                parentDirectoryUtf8,
                parentDirectoryByteLength),
            new ProjectNativeStringView(projectNameUtf8, projectNameByteLength),
            new ProjectNativeStringView(projectIdUtf8, projectIdByteLength));
}

[StructLayout(LayoutKind.Explicit, Size = 16)]
internal readonly record struct ProjectNativeTextSpan(
    [field: FieldOffset(0)] ulong ByteOffset,
    [field: FieldOffset(8)] ulong ByteLength);

[StructLayout(LayoutKind.Explicit, Size = 88)]
internal readonly record struct ProjectNativeResult(
    [field: FieldOffset(0)] ProjectNativeAbiHeader Header,
    [field: FieldOffset(8)] ProjectNativeStatus Status,
    [field: FieldOffset(12)] uint Reserved,
    [field: FieldOffset(16)] ulong RequiredByteLength,
    [field: FieldOffset(24)] ProjectNativeTextSpan ProjectRootUtf8,
    [field: FieldOffset(40)] ProjectNativeTextSpan ProjectNameUtf8,
    [field: FieldOffset(56)] ProjectNativeTextSpan ProjectIdUtf8,
    [field: FieldOffset(72)] ProjectNativeTextSpan MessageUtf8)
{
    public const uint StructSize = 88;
}
