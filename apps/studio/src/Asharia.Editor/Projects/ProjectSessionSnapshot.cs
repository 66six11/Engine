using System;

namespace Asharia.Editor.Projects;

public sealed record ProjectSessionSnapshot
{
    public ProjectSessionSnapshot(
        ProjectSessionState state,
        ActiveProjectSnapshot? project)
    {
        if (!Enum.IsDefined(state))
        {
            throw new ArgumentOutOfRangeException(
                nameof(state),
                state,
                null);
        }
        if ((state == ProjectSessionState.Ready) != (project is not null))
        {
            throw new ArgumentException(
                "Only a ready project session may contain an active project.",
                nameof(project));
        }

        State = state;
        Project = project;
    }

    public static ProjectSessionSnapshot NoProject { get; } = new(
        ProjectSessionState.NoProject,
        project: null);

    public ProjectSessionState State { get; }

    public ActiveProjectSnapshot? Project { get; }

    public bool IsReady => State == ProjectSessionState.Ready;

    public static ProjectSessionSnapshot Ready(ActiveProjectSnapshot project)
    {
        ArgumentNullException.ThrowIfNull(project);
        return new ProjectSessionSnapshot(
            ProjectSessionState.Ready,
            project);
    }
}
