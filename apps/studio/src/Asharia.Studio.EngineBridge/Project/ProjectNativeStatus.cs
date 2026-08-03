namespace Asharia.Studio.EngineBridge.Project;

internal enum ProjectNativeStatus : uint
{
    Success = 0,
    InvalidArgument = 1,
    UnsupportedAbi = 2,
    InvalidUtf8 = 3,
    AlreadyExists = 4,
    Busy = 5,
    InvalidProject = 6,
    IoFailure = 7,
    BufferTooSmall = 8,
    InternalError = 9,
}
