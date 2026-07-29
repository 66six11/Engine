namespace Asharia.Studio.EngineBridge.Project.Abi;

internal interface IProjectNativeApi
{
    ProjectNativeStatus Open(
        in ProjectNativeOpenRequest request,
        out ProjectNativeResult result);

    ProjectNativeStatus CreateMinimal(
        in ProjectNativeCreateRequest request,
        out ProjectNativeResult result);

    void Release(ProjectNativeResult result);
}
