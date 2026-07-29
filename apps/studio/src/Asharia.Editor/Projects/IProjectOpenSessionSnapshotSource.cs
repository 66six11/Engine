using System;

namespace Asharia.Editor.Projects;

public interface IProjectOpenSessionSnapshotSource
{
    event EventHandler? SnapshotChanged;

    ProjectOpenSessionSnapshot Current { get; }
}
