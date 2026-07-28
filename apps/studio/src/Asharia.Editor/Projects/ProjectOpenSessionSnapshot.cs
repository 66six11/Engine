using System;
using System.Collections.Generic;
using System.Linq;

namespace Asharia.Editor.Projects;

public sealed record ProjectOpenSessionSnapshot
{
    public ProjectOpenSessionSnapshot(
        ProjectOpenSessionState state,
        ProjectOpenNextAction nextAction,
        ProjectOpenSummarySnapshot? project,
        IEnumerable<ProjectOpenSessionDiagnosticSnapshot>? diagnostics = null)
    {
        if (!Enum.IsDefined(state))
        {
            throw new ArgumentOutOfRangeException(nameof(state), state, null);
        }

        if (!Enum.IsDefined(nextAction))
        {
            throw new ArgumentOutOfRangeException(
                nameof(nextAction),
                nextAction,
                null);
        }

        if (nextAction != ExpectedNextAction(state))
        {
            throw new ArgumentException(
                "Project-open state and next action do not match.",
                nameof(nextAction));
        }

        if ((state == ProjectOpenSessionState.Ready) != (project is not null))
        {
            throw new ArgumentException(
                "Only a bootstrap-ready session may contain a project summary.",
                nameof(project));
        }

        State = state;
        NextAction = nextAction;
        Project = project;
        Diagnostics = Array.AsReadOnly(
            diagnostics?.ToArray() ?? []);
    }

    public static ProjectOpenSessionSnapshot NoProject { get; } = new(
        ProjectOpenSessionState.NoProject,
        ProjectOpenNextAction.SelectProject,
        project: null);

    public ProjectOpenSessionState State { get; }

    public ProjectOpenNextAction NextAction { get; }

    public ProjectOpenSummarySnapshot? Project { get; }

    public IReadOnlyList<ProjectOpenSessionDiagnosticSnapshot> Diagnostics { get; }

    public bool IsBootstrapReady => State == ProjectOpenSessionState.Ready;

    private static ProjectOpenNextAction ExpectedNextAction(
        ProjectOpenSessionState state) =>
        state switch
        {
            ProjectOpenSessionState.NoProject =>
                ProjectOpenNextAction.SelectProject,
            ProjectOpenSessionState.Opening =>
                ProjectOpenNextAction.InspectProject,
            ProjectOpenSessionState.Ready =>
                ProjectOpenNextAction.ActivateProjectProfile,
            ProjectOpenSessionState.PendingBuild =>
                ProjectOpenNextAction.BuildProjectHost,
            ProjectOpenSessionState.PendingRestart =>
                ProjectOpenNextAction.RestartEditor,
            ProjectOpenSessionState.RepairRequired =>
                ProjectOpenNextAction.RepairDistribution,
            ProjectOpenSessionState.UpgradeRequired =>
                ProjectOpenNextAction.UpgradeEngine,
            ProjectOpenSessionState.SafeMode =>
                ProjectOpenNextAction.OpenSafeMode,
            ProjectOpenSessionState.FatalDistributionError =>
                ProjectOpenNextAction.RepairEditorImage,
            _ => throw new ArgumentOutOfRangeException(
                nameof(state),
                state,
                null),
        };
}
