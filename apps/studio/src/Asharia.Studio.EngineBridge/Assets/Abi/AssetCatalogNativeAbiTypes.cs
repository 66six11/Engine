using System.Runtime.InteropServices;

namespace Asharia.Studio.EngineBridge.Assets.Abi;

internal static class AssetCatalogNativeAbi
{
    public const uint Version = 1;
}

internal enum AssetCatalogNativeStatus : uint
{
    Success = 0,
    InvalidArgument = 1,
    UnsupportedAbi = 2,
    InvalidUtf8 = 3,
    InvalidProject = 4,
    IoFailure = 5,
    LimitExceeded = 6,
    Cancelled = 7,
    BufferTooSmall = 8,
    InternalError = 9,
}

[StructLayout(LayoutKind.Explicit, Size = 8)]
internal readonly record struct AssetCatalogNativeAbiHeader(
    [field: FieldOffset(0)] uint AbiVersion,
    [field: FieldOffset(4)] uint StructSize);

[StructLayout(LayoutKind.Explicit, Size = 16)]
internal readonly record struct AssetCatalogNativeStringView(
    [field: FieldOffset(0)] nint Data,
    [field: FieldOffset(8)] ulong ByteLength);

[StructLayout(LayoutKind.Explicit, Size = 32)]
internal readonly record struct AssetCatalogNativeLimits(
    [field: FieldOffset(0)] ulong MaxSourceFiles,
    [field: FieldOffset(8)] ulong MaxTotalSourceBytes,
    [field: FieldOffset(16)] ulong MaxDiagnostics,
    [field: FieldOffset(24)] ulong MaxResponseBytes)
{
    public static AssetCatalogNativeLimits StudioDefault { get; } = new(
        MaxSourceFiles: 10_000,
        MaxTotalSourceBytes: 8UL * 1024 * 1024 * 1024,
        MaxDiagnostics: 10_000,
        MaxResponseBytes: 16UL * 1024 * 1024);
}

[StructLayout(LayoutKind.Explicit, Size = 88)]
internal readonly record struct AssetCatalogNativeQueryRequest(
    [field: FieldOffset(0)] AssetCatalogNativeAbiHeader Header,
    [field: FieldOffset(8)] AssetCatalogNativeStringView ProjectPathUtf8,
    [field: FieldOffset(24)] AssetCatalogNativeStringView TargetProfileUtf8,
    [field: FieldOffset(40)] AssetCatalogNativeStringView ProductManifestPathUtf8,
    [field: FieldOffset(56)] AssetCatalogNativeLimits Limits)
{
    public const uint StructSize = 88;

    public static AssetCatalogNativeQueryRequest Current(
        nint projectPathUtf8,
        ulong projectPathByteLength,
        nint targetProfileUtf8,
        ulong targetProfileByteLength,
        nint productManifestPathUtf8,
        ulong productManifestPathByteLength,
        AssetCatalogNativeLimits limits) =>
        new(
            new AssetCatalogNativeAbiHeader(AssetCatalogNativeAbi.Version, StructSize),
            new AssetCatalogNativeStringView(projectPathUtf8, projectPathByteLength),
            new AssetCatalogNativeStringView(targetProfileUtf8, targetProfileByteLength),
            new AssetCatalogNativeStringView(
                productManifestPathUtf8,
                productManifestPathByteLength),
            limits);
}

[StructLayout(LayoutKind.Explicit, Size = 16)]
internal readonly record struct AssetCatalogNativeTextSpan(
    [field: FieldOffset(0)] ulong ByteOffset,
    [field: FieldOffset(8)] ulong ByteLength);

[StructLayout(LayoutKind.Explicit, Size = 56)]
internal readonly record struct AssetCatalogNativeResult(
    [field: FieldOffset(0)] AssetCatalogNativeAbiHeader Header,
    [field: FieldOffset(8)] AssetCatalogNativeStatus OperationStatus,
    [field: FieldOffset(12)] uint Reserved,
    [field: FieldOffset(16)] ulong RequiredByteLength,
    [field: FieldOffset(24)] AssetCatalogNativeTextSpan PayloadJsonUtf8,
    [field: FieldOffset(40)] AssetCatalogNativeTextSpan MessageUtf8)
{
    public const uint StructSize = 56;
}
