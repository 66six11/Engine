namespace Asharia.Studio.EngineBridge.Project.Abi;

internal interface IProjectNativeApi
{
    ProjectNativeStatus Open(
        in ProjectNativeOpenRequest request,
        nint responseUtf8,
        ulong responseCapacity,
        out ProjectNativeResult result,
        ulong resultCapacity);

    ProjectNativeStatus CreateMinimal(
        in ProjectNativeCreateRequest request,
        nint responseUtf8,
        ulong responseCapacity,
        out ProjectNativeResult result,
        ulong resultCapacity);
}
