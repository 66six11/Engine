namespace Asharia.Studio.EngineBridge.Scene;

public enum SceneNativeStatus : uint
{
    Success = 0,
    InvalidArgument = 1,
    UnsupportedAbi = 2,
    WrongThread = 3,
    InternalError = 4,
    InvalidEntity = 5,
    EntityCapacityExceeded = 6,
    InvalidTransform = 7,
    InvalidUtf8 = 8,
    BufferTooSmall = 9,
    InvalidScene = 10,
    RevisionConflict = 11,
    IoFailure = 12,
    StaleHandle = 13,
    InvalidObject = 14,
    DuplicateObject = 15,
    InvalidAssetReference = 16,
    RevisionExhausted = 17,
}
