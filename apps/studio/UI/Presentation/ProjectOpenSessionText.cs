using System;
using Asharia.Editor.Projects;

namespace Editor.UI.Presentation;

internal static class ProjectOpenSessionText
{
    public static string GetProjectDisplayName(ProjectOpenSessionSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);

        return snapshot.Project?.ProjectName ?? "No project";
    }

    public static string GetStateLabel(ProjectOpenSessionState state) =>
        state switch
        {
            ProjectOpenSessionState.NoProject => "No project",
            ProjectOpenSessionState.Opening => "Opening",
            ProjectOpenSessionState.Ready => "Bootstrap ready",
            ProjectOpenSessionState.PendingBuild => "Build required",
            ProjectOpenSessionState.PendingRestart => "Restart required",
            ProjectOpenSessionState.RepairRequired => "Repair required",
            ProjectOpenSessionState.UpgradeRequired => "Upgrade required",
            ProjectOpenSessionState.SafeMode => "Safe mode",
            ProjectOpenSessionState.FatalDistributionError => "Editor image error",
            _ => throw new ArgumentOutOfRangeException(nameof(state), state, null),
        };

    public static string GetStateTitle(ProjectOpenSessionState state) =>
        state switch
        {
            ProjectOpenSessionState.NoProject => "No project is open",
            ProjectOpenSessionState.Opening => "Inspecting project",
            ProjectOpenSessionState.Ready => "Project bootstrap is ready",
            ProjectOpenSessionState.PendingBuild => "Project host build required",
            ProjectOpenSessionState.PendingRestart => "Editor restart required",
            ProjectOpenSessionState.RepairRequired => "Engine distribution repair required",
            ProjectOpenSessionState.UpgradeRequired => "Engine upgrade required",
            ProjectOpenSessionState.SafeMode => "Safe mode required",
            ProjectOpenSessionState.FatalDistributionError => "Editor image repair required",
            _ => throw new ArgumentOutOfRangeException(nameof(state), state, null),
        };

    public static string GetStateMessage(ProjectOpenSessionState state) =>
        state switch
        {
            ProjectOpenSessionState.NoProject =>
                "Select a project when project selection is connected.",
            ProjectOpenSessionState.Opening =>
                "Project manifests and the current editor image are being inspected.",
            ProjectOpenSessionState.Ready =>
                "Project profile activation and asset browsing are not connected yet.",
            ProjectOpenSessionState.PendingBuild =>
                "A matching project host must be built before activation.",
            ProjectOpenSessionState.PendingRestart =>
                "Restart the editor to use the prepared project host.",
            ProjectOpenSessionState.RepairRequired =>
                "Repair the engine distribution before opening this project.",
            ProjectOpenSessionState.UpgradeRequired =>
                "Use a compatible engine version before opening this project.",
            ProjectOpenSessionState.SafeMode =>
                "Open without project code after safe mode is connected.",
            ProjectOpenSessionState.FatalDistributionError =>
                "Repair the editor image before opening a project.",
            _ => throw new ArgumentOutOfRangeException(nameof(state), state, null),
        };

    public static string GetNextActionLabel(ProjectOpenNextAction action) =>
        action switch
        {
            ProjectOpenNextAction.SelectProject => "Select Project",
            ProjectOpenNextAction.InspectProject => "Inspect Project",
            ProjectOpenNextAction.ActivateProjectProfile => "Activate Project Profile",
            ProjectOpenNextAction.BuildProjectHost => "Build Project Host",
            ProjectOpenNextAction.RestartEditor => "Restart Editor",
            ProjectOpenNextAction.RepairDistribution => "Repair Distribution",
            ProjectOpenNextAction.UpgradeEngine => "Upgrade Engine",
            ProjectOpenNextAction.OpenSafeMode => "Open Safe Mode",
            ProjectOpenNextAction.RepairEditorImage => "Repair Editor Image",
            _ => throw new ArgumentOutOfRangeException(nameof(action), action, null),
        };

    public static string GetUnavailableReason(ProjectOpenNextAction action) =>
        $"The {GetNextActionLabel(action)} action is not connected to a project-open service.";
}
