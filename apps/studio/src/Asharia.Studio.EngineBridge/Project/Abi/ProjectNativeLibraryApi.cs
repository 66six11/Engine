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
        out ProjectNativeResult result)
    {
        return (ProjectNativeStatus)ProjectNativeEntryPoints.Open(
            in request,
            out result);
    }

    public ProjectNativeStatus CreateMinimal(
        in ProjectNativeCreateRequest request,
        out ProjectNativeResult result)
    {
        return (ProjectNativeStatus)ProjectNativeEntryPoints.CreateMinimal(
            in request,
            out result);
    }

    public void Release(ProjectNativeResult result)
    {
        ProjectNativeEntryPoints.Release(result);
    }
}

internal static partial class ProjectNativeEntryPoints
{
    private const string LibraryName = "editor_native";

    [LibraryImport(LibraryName, EntryPoint = "editor_project_open")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint Open(
        in ProjectNativeOpenRequest request,
        out ProjectNativeResult result);

    [LibraryImport(LibraryName, EntryPoint = "editor_project_create_minimal")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CreateMinimal(
        in ProjectNativeCreateRequest request,
        out ProjectNativeResult result);

    [LibraryImport(LibraryName, EntryPoint = "editor_project_release_result")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void Release(ProjectNativeResult result);
}
