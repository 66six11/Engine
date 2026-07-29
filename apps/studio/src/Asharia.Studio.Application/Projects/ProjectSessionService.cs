using System;
using Asharia.Editor.Projects;

namespace Asharia.Studio.Application.Projects;

public sealed class ProjectSessionService : IProjectSessionService
{
    private readonly object gate_ = new();
    private readonly IProjectDescriptorGateway gateway_;
    private readonly IRecentProjectStore recentProjects_;
    private ProjectSessionSnapshot current_ = ProjectSessionSnapshot.NoProject;

    public ProjectSessionService(IProjectDescriptorGateway gateway)
        : this(gateway, RecentProjectStore.CreateDefault())
    {
    }

    internal ProjectSessionService(
        IProjectDescriptorGateway gateway,
        IRecentProjectStore recentProjects)
    {
        ArgumentNullException.ThrowIfNull(gateway);
        ArgumentNullException.ThrowIfNull(recentProjects);
        gateway_ = gateway;
        recentProjects_ = recentProjects;
    }

    public event EventHandler? SnapshotChanged;

    public ProjectSessionSnapshot Current
    {
        get
        {
            lock (gate_)
            {
                return current_;
            }
        }
    }

    public ProjectSessionOperationResult CreateMinimalProject(
        string projectRoot,
        string projectName)
    {
        return Execute(
            () => gateway_.CreateMinimalProject(
                projectRoot,
                projectName,
                Guid.NewGuid()),
            snapshot => $"Created project '{snapshot.ProjectName}'.");
    }

    public ProjectSessionOperationResult OpenProject(string projectRoot)
    {
        return Execute(
            () => gateway_.OpenProject(projectRoot),
            snapshot => $"Opened project '{snapshot.ProjectName}'.");
    }

    public ProjectSessionOperationResult RestoreRecentProject()
    {
        string? projectRoot;
        try
        {
            projectRoot = recentProjects_.Read();
        }
        catch (Exception exception)
        {
            return ProjectSessionOperationResult.Failure(
                "Could not read the recent-project preference: "
                    + ExceptionMessage(
                        exception,
                        "the preference reader failed without a diagnostic."));
        }

        if (string.IsNullOrWhiteSpace(projectRoot))
        {
            return ProjectSessionOperationResult.Failure(
                "No recent project is available.");
        }
        return OpenProject(projectRoot);
    }

    private ProjectSessionOperationResult Execute(
        Func<ProjectDescriptorGatewaySnapshot> operation,
        Func<ProjectDescriptorGatewaySnapshot, string> successMessage)
    {
        ProjectDescriptorGatewaySnapshot descriptor;
        try
        {
            descriptor = operation();
        }
        catch (Exception exception)
        {
            return ProjectSessionOperationResult.Failure(
                ExceptionMessage(
                    exception,
                    "The project operation failed without a diagnostic."));
        }

        var session = ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                descriptor.RootPath,
                descriptor.ProjectName,
                descriptor.ProjectId));

        var message = successMessage(descriptor);
        try
        {
            recentProjects_.Write(descriptor.RootPath);
        }
        catch (Exception exception)
        {
            message += " The recent-project preference was not updated: "
                + ExceptionMessage(
                    exception,
                    "the preference writer failed without a diagnostic.");
        }

        lock (gate_)
        {
            current_ = session;
        }
        SnapshotChanged?.Invoke(this, EventArgs.Empty);
        return ProjectSessionOperationResult.Success(session, message);
    }

    private static string ExceptionMessage(
        Exception exception,
        string fallback)
    {
        return string.IsNullOrWhiteSpace(exception.Message)
            ? fallback
            : exception.Message;
    }
}
