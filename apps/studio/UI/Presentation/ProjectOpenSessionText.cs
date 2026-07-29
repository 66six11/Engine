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
            ProjectOpenSessionState.Ready => "Ready to open",
            ProjectOpenSessionState.PendingBuild => "Build required",
            ProjectOpenSessionState.PendingRestart => "Restart required",
            ProjectOpenSessionState.RepairRequired => "Repair required",
            ProjectOpenSessionState.UpgradeRequired => "Upgrade required",
            ProjectOpenSessionState.SafeMode => "Safe mode",
            ProjectOpenSessionState.FatalDistributionError => "Studio installation error",
            _ => throw new ArgumentOutOfRangeException(nameof(state), state, null),
        };

    public static string GetStateTitle(ProjectOpenSessionState state) =>
        state switch
        {
            ProjectOpenSessionState.NoProject => "No project is open",
            ProjectOpenSessionState.Opening => "Checking project",
            ProjectOpenSessionState.Ready => "Project check completed",
            ProjectOpenSessionState.PendingBuild => "Project code build required",
            ProjectOpenSessionState.PendingRestart => "Studio restart required",
            ProjectOpenSessionState.RepairRequired => "Engine installation repair required",
            ProjectOpenSessionState.UpgradeRequired => "Compatible engine version required",
            ProjectOpenSessionState.SafeMode => "Safe mode required",
            ProjectOpenSessionState.FatalDistributionError => "Studio installation repair required",
            _ => throw new ArgumentOutOfRangeException(nameof(state), state, null),
        };

    public static string GetStateMessage(ProjectOpenSessionState state) =>
        state switch
        {
            ProjectOpenSessionState.NoProject =>
                "Select a project after project opening is connected.",
            ProjectOpenSessionState.Opening =>
                "The project and current Studio installation are being checked.",
            ProjectOpenSessionState.Ready =>
                "The project can be activated after project opening is connected.",
            ProjectOpenSessionState.PendingBuild =>
                "Matching project code must be built before the project can open.",
            ProjectOpenSessionState.PendingRestart =>
                "Restart Studio to use the prepared project code.",
            ProjectOpenSessionState.RepairRequired =>
                "Repair the engine installation before opening this project.",
            ProjectOpenSessionState.UpgradeRequired =>
                "Open this project with a compatible engine version.",
            ProjectOpenSessionState.SafeMode =>
                "Open without project code after safe mode is connected.",
            ProjectOpenSessionState.FatalDistributionError =>
                "Repair the Studio installation before opening a project.",
            _ => throw new ArgumentOutOfRangeException(nameof(state), state, null),
        };

    public static string GetNextActionLabel(ProjectOpenNextAction action) =>
        action switch
        {
            ProjectOpenNextAction.SelectProject => "Select Project",
            ProjectOpenNextAction.InspectProject => "Check Project",
            ProjectOpenNextAction.ActivateProjectProfile => "Open Project",
            ProjectOpenNextAction.BuildProjectHost => "Build Project Code",
            ProjectOpenNextAction.RestartEditor => "Restart Studio",
            ProjectOpenNextAction.RepairDistribution => "Repair Engine Installation",
            ProjectOpenNextAction.UpgradeEngine => "Use Compatible Engine Version",
            ProjectOpenNextAction.OpenSafeMode => "Open in Safe Mode",
            ProjectOpenNextAction.RepairEditorImage => "Repair Studio Installation",
            _ => throw new ArgumentOutOfRangeException(nameof(action), action, null),
        };
}
