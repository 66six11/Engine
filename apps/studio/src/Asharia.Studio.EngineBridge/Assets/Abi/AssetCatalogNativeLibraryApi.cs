using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Asharia.Studio.EngineBridge.Assets.Abi;

internal interface IAssetCatalogNativeApi
{
    AssetCatalogNativeStatus Query(
        in AssetCatalogNativeQueryRequest request,
        nint responseUtf8,
        ulong responseCapacity,
        out AssetCatalogNativeResult result,
        ulong resultCapacity);
}

internal sealed class AssetCatalogNativeLibraryApi : IAssetCatalogNativeApi
{
    public static AssetCatalogNativeLibraryApi Instance { get; } = new();

    private AssetCatalogNativeLibraryApi()
    {
    }

    public AssetCatalogNativeStatus Query(
        in AssetCatalogNativeQueryRequest request,
        nint responseUtf8,
        ulong responseCapacity,
        out AssetCatalogNativeResult result,
        ulong resultCapacity) =>
        (AssetCatalogNativeStatus)AssetCatalogNativeEntryPoints.Query(
            in request,
            responseUtf8,
            responseCapacity,
            out result,
            resultCapacity);
}

internal static partial class AssetCatalogNativeEntryPoints
{
    private const string LibraryName = "asharia_editor_content_native";

    [LibraryImport(LibraryName, EntryPoint = "asharia_editor_content_query")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint Query(
        in AssetCatalogNativeQueryRequest request,
        nint responseUtf8,
        ulong responseCapacity,
        out AssetCatalogNativeResult result,
        ulong resultCapacity);
}
