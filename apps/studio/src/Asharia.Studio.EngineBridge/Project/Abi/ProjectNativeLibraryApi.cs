using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Asharia.Studio.EngineBridge.Project.Abi;

internal sealed class ProjectNativeLibraryApi : IProjectNativeApi
{
    public static ProjectNativeLibraryApi Instance { get; } = new();

    private ProjectNativeLibraryApi()
    {
    }

    public ProjectNativeStatus Open(
        in ProjectNativeOpenRequest request,
        nint responseUtf8,
        ulong responseCapacity,
        out ProjectNativeResult result,
        ulong resultCapacity) =>
        (ProjectNativeStatus)ProjectNativeEntryPoints.Open(
            in request,
            responseUtf8,
            responseCapacity,
            out result,
            resultCapacity);

    public ProjectNativeStatus CreateMinimal(
        in ProjectNativeCreateRequest request,
        nint responseUtf8,
        ulong responseCapacity,
        out ProjectNativeResult result,
        ulong resultCapacity) =>
        (ProjectNativeStatus)ProjectNativeEntryPoints.CreateMinimal(
            in request,
            responseUtf8,
            responseCapacity,
            out result,
            resultCapacity);
}

internal static partial class ProjectNativeEntryPoints
{
    private const string LibraryName = "asharia_project_native";

    [LibraryImport(LibraryName, EntryPoint = "asharia_project_open")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint Open(
        in ProjectNativeOpenRequest request,
        nint responseUtf8,
        ulong responseCapacity,
        out ProjectNativeResult result,
        ulong resultCapacity);

    [LibraryImport(LibraryName, EntryPoint = "asharia_project_create_minimal")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CreateMinimal(
        in ProjectNativeCreateRequest request,
        nint responseUtf8,
        ulong responseCapacity,
        out ProjectNativeResult result,
        ulong resultCapacity);
}
