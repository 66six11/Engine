using System;
using Asharia.Editor.Projects;
using Asharia.Studio.Application.Projects;
using Xunit;

namespace Asharia.Studio.Application.Tests.Projects;

public sealed class ProjectSessionServiceTests
{
    private static readonly Guid ProjectId =
        Guid.Parse("51e86383-8a06-4c41-9267-ab10b0b67eb9");

    [Fact]
    public void Service_starts_without_an_active_project()
    {
        var service = CreateService();

        Assert.Same(ProjectSessionSnapshot.NoProject, service.Current);
    }

    [Fact]
    public void Create_activates_canonical_gateway_result_and_updates_recent_project()
    {
        var gateway = new StubProjectDescriptorGateway
        {
            CreateResult = Descriptor(
                @"D:\Canonical\Example",
                "Canonical Example"),
        };
        var recent = new StubRecentProjectStore();
        var service = new ProjectSessionService(gateway, recent);
        var changed = 0;
        service.SnapshotChanged += (_, _) => changed++;

        var result = service.CreateMinimalProject(
            @"D:\Selected\Example",
            "Selected Example");

        Assert.True(result.Succeeded);
        Assert.Equal(1, gateway.CreateCalls);
        Assert.Equal(@"D:\Selected\Example", gateway.LastCreateRoot);
        Assert.Equal("Selected Example", gateway.LastCreateName);
        Assert.NotEqual(Guid.Empty, gateway.LastCreateId);
        Assert.Equal("Canonical Example", service.Current.Project?.ProjectName);
        Assert.Equal(@"D:\Canonical\Example", service.Current.Project?.RootPath);
        Assert.Equal(ProjectId, service.Current.Project?.ProjectId);
        Assert.Equal(@"D:\Canonical\Example", recent.WrittenRoot);
        Assert.Equal(1, changed);
    }

    [Fact]
    public void Open_activates_project_and_reports_success()
    {
        var gateway = new StubProjectDescriptorGateway
        {
            OpenResult = Descriptor(
                @"D:\Projects\Example",
                "Example"),
        };
        var service = CreateService(gateway);

        var result = service.OpenProject(@"D:\Projects\Example");

        Assert.True(result.Succeeded);
        Assert.Equal(
            "Opened project 'Example'.",
            result.Message);
        Assert.True(service.Current.IsReady);
        Assert.Equal(ProjectId, service.Current.Project?.ProjectId);
    }

    [Fact]
    public void Failed_open_preserves_current_successful_session()
    {
        var gateway = new StubProjectDescriptorGateway
        {
            OpenResult = Descriptor(
                @"D:\Projects\First",
                "First"),
        };
        var recent = new StubRecentProjectStore();
        var service = new ProjectSessionService(gateway, recent);
        _ = service.OpenProject(@"D:\Projects\First");
        var active = service.Current;
        gateway.OpenException = new InvalidOperationException(
            "The selected project is invalid.");
        var changed = 0;
        service.SnapshotChanged += (_, _) => changed++;

        var result = service.OpenProject(@"D:\Projects\Broken");

        Assert.False(result.Succeeded);
        Assert.Equal(
            "The selected project is invalid.",
            result.Message);
        Assert.Same(active, service.Current);
        Assert.Equal(@"D:\Projects\First", recent.WrittenRoot);
        Assert.Equal(0, changed);
    }

    [Fact]
    public void Restore_revalidates_recent_root_through_gateway()
    {
        var gateway = new StubProjectDescriptorGateway
        {
            OpenResult = Descriptor(
                @"D:\Canonical\Recent",
                "Recent"),
        };
        var recent = new StubRecentProjectStore
        {
            ReadRoot = @"D:\Stored\Recent",
        };
        var service = new ProjectSessionService(gateway, recent);

        var result = service.RestoreRecentProject();

        Assert.True(result.Succeeded);
        Assert.Equal(@"D:\Stored\Recent", gateway.LastOpenRoot);
        Assert.Equal(@"D:\Canonical\Recent", service.Current.Project?.RootPath);
    }

    [Fact]
    public void Restore_without_recent_project_remains_no_project()
    {
        var gateway = new StubProjectDescriptorGateway();
        var service = CreateService(gateway);

        var result = service.RestoreRecentProject();

        Assert.False(result.Succeeded);
        Assert.Equal(0, gateway.OpenCalls);
        Assert.Same(ProjectSessionSnapshot.NoProject, service.Current);
    }

    [Fact]
    public void Recent_project_write_failure_does_not_discard_active_session()
    {
        var recent = new StubRecentProjectStore
        {
            WriteException = new InvalidOperationException(
                "preference is read-only"),
        };
        var service = new ProjectSessionService(
            new StubProjectDescriptorGateway
            {
                OpenResult = Descriptor(
                    @"D:\Projects\Example",
                    "Example"),
            },
            recent);

        var result = service.OpenProject(@"D:\Projects\Example");

        Assert.True(result.Succeeded);
        Assert.True(service.Current.IsReady);
        Assert.Contains(
            "preference is read-only",
            result.Message,
            StringComparison.Ordinal);
    }

    private static ProjectSessionService CreateService(
        StubProjectDescriptorGateway? gateway = null)
    {
        return new ProjectSessionService(
            gateway ?? new StubProjectDescriptorGateway(),
            new StubRecentProjectStore());
    }

    private static ProjectDescriptorGatewaySnapshot Descriptor(
        string root,
        string name)
    {
        return new ProjectDescriptorGatewaySnapshot(
            root,
            name,
            ProjectId);
    }

    private sealed class StubProjectDescriptorGateway : IProjectDescriptorGateway
    {
        public int CreateCalls { get; private set; }

        public int OpenCalls { get; private set; }

        public string LastCreateRoot { get; private set; } = string.Empty;

        public string LastCreateName { get; private set; } = string.Empty;

        public Guid LastCreateId { get; private set; }

        public string LastOpenRoot { get; private set; } = string.Empty;

        public ProjectDescriptorGatewaySnapshot CreateResult { get; set; } =
            Descriptor(@"D:\Projects\Created", "Created");

        public ProjectDescriptorGatewaySnapshot OpenResult { get; set; } =
            Descriptor(@"D:\Projects\Opened", "Opened");

        public Exception? CreateException { get; set; }

        public Exception? OpenException { get; set; }

        public ProjectDescriptorGatewaySnapshot CreateMinimalProject(
            string projectRoot,
            string projectName,
            Guid projectId)
        {
            CreateCalls++;
            LastCreateRoot = projectRoot;
            LastCreateName = projectName;
            LastCreateId = projectId;
            if (CreateException is not null)
            {
                throw CreateException;
            }
            return CreateResult;
        }

        public ProjectDescriptorGatewaySnapshot OpenProject(string projectRoot)
        {
            OpenCalls++;
            LastOpenRoot = projectRoot;
            if (OpenException is not null)
            {
                throw OpenException;
            }
            return OpenResult;
        }
    }

    private sealed class StubRecentProjectStore : IRecentProjectStore
    {
        public string? ReadRoot { get; set; }

        public string? WrittenRoot { get; private set; }

        public Exception? ReadException { get; set; }

        public Exception? WriteException { get; set; }

        public string? Read()
        {
            if (ReadException is not null)
            {
                throw ReadException;
            }
            return ReadRoot;
        }

        public void Write(string projectRoot)
        {
            if (WriteException is not null)
            {
                throw WriteException;
            }
            WrittenRoot = projectRoot;
        }
    }
}
