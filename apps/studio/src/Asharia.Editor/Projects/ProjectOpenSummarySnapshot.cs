using System;

namespace Asharia.Editor.Projects;

public sealed record ProjectOpenSummarySnapshot
{
    public ProjectOpenSummarySnapshot(
        string projectName,
        Guid projectId,
        ulong assetSourceRootCount)
    {
        ArgumentException.ThrowIfNullOrEmpty(projectName);
        if (projectId == Guid.Empty)
        {
            throw new ArgumentException(
                "Project id must not be empty.",
                nameof(projectId));
        }

        ProjectName = projectName;
        ProjectId = projectId;
        AssetSourceRootCount = assetSourceRootCount;
    }

    public string ProjectName { get; }

    public Guid ProjectId { get; }

    public ulong AssetSourceRootCount { get; }
}
