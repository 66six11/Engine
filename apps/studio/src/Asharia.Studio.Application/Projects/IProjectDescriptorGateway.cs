using System;

namespace Asharia.Studio.Application.Projects;

public interface IProjectDescriptorGateway
{
    ProjectDescriptorGatewaySnapshot CreateMinimalProject(
        string projectRoot,
        string projectName,
        Guid projectId);

    ProjectDescriptorGatewaySnapshot OpenProject(string projectRoot);
}
