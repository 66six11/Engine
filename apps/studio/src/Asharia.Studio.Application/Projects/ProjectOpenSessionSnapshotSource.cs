using System;
using Asharia.Editor.Projects;

namespace Asharia.Studio.Application.Projects;

public sealed class ProjectOpenSessionSnapshotSource : IProjectOpenSessionSnapshotSource
{
    private readonly object gate_ = new();
    private ProjectOpenSessionSnapshot current_;

    public ProjectOpenSessionSnapshotSource(
        ProjectOpenSessionSnapshot? initialSnapshot = null)
    {
        current_ = initialSnapshot ?? ProjectOpenSessionSnapshot.NoProject;
    }

    public event EventHandler? SnapshotChanged;

    public ProjectOpenSessionSnapshot Current
    {
        get
        {
            lock (gate_)
            {
                return current_;
            }
        }
    }

    public void Publish(ProjectOpenSessionSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);

        lock (gate_)
        {
            if (ReferenceEquals(current_, snapshot))
            {
                return;
            }

            current_ = snapshot;
        }

        SnapshotChanged?.Invoke(this, EventArgs.Empty);
    }
}
