using System;

namespace Asharia.Editor.Projects;

public interface IProjectSessionService
{
    event EventHandler? SnapshotChanged;

    ProjectSessionSnapshot Current { get; }

    ProjectSessionOperationResult CreateMinimalProject(
        string projectRoot,
        string projectName);

    ProjectSessionOperationResult OpenProject(string projectRoot);
}
