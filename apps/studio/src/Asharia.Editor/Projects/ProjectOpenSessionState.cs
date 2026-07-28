namespace Asharia.Editor.Projects;

public enum ProjectOpenSessionState
{
    NoProject = 0,
    Opening = 1,
    Ready = 2,
    PendingBuild = 3,
    PendingRestart = 4,
    RepairRequired = 5,
    UpgradeRequired = 6,
    SafeMode = 7,
    FatalDistributionError = 8,
}
