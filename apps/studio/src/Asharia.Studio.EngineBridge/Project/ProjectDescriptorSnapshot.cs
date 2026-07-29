using System;

namespace Asharia.Studio.EngineBridge.Project;

public sealed record ProjectDescriptorSnapshot
{
    public ProjectDescriptorSnapshot(
        string rootPath,
        string projectName,
        Guid projectId)
    {
        if (string.IsNullOrWhiteSpace(rootPath))
        {
            throw new ArgumentException(
                "Project root path must not be null or whitespace.",
                nameof(rootPath));
        }
        if (string.IsNullOrWhiteSpace(projectName))
        {
            throw new ArgumentException(
                "Project name must not be null or whitespace.",
                nameof(projectName));
        }
        if (projectId == Guid.Empty)
        {
            throw new ArgumentException(
                "Project id must not be empty.",
                nameof(projectId));
        }

        RootPath = rootPath;
        ProjectName = projectName;
        ProjectId = projectId;
    }

    public string RootPath { get; }

    public string ProjectName { get; }

    public Guid ProjectId { get; }
}
