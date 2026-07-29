using System;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.EngineBridge.Project;

namespace Editor.Core.Interop.Projects.Adapters;

internal sealed class ProjectDescriptorGateway : IProjectDescriptorGateway
{
    private readonly ProjectDescriptorBridge bridge_;

    public ProjectDescriptorGateway()
        : this(new ProjectDescriptorBridge())
    {
    }

    internal ProjectDescriptorGateway(ProjectDescriptorBridge bridge)
    {
        ArgumentNullException.ThrowIfNull(bridge);
        bridge_ = bridge;
    }

    public ProjectDescriptorGatewaySnapshot CreateMinimalProject(
        string projectRoot,
        string projectName,
        Guid projectId)
    {
        return Map(bridge_.CreateMinimalProject(
            projectRoot,
            projectName,
            projectId));
    }

    public ProjectDescriptorGatewaySnapshot OpenProject(string projectRoot)
    {
        return Map(bridge_.OpenProject(projectRoot));
    }

    private static ProjectDescriptorGatewaySnapshot Map(
        ProjectDescriptorSnapshot descriptor)
    {
        return new ProjectDescriptorGatewaySnapshot(
            descriptor.RootPath,
            descriptor.ProjectName,
            descriptor.ProjectId);
    }
}
