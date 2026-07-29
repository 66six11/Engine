using System;
using System.Collections.Generic;
using Asharia.Editor.Projects;
using Asharia.Editor.Worlds.Snapshots;
using Editor.Core.Abstractions;
using Editor.Core.Services;
using Editor.Shell.Composition;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class ProjectSceneSessionProjectionTests
{
    [Fact]
    public void Active_project_creates_minimal_scene_on_ui_dispatcher()
    {
        var projectSessions = new StubProjectSessionService();
        var scenes = new InMemorySceneSnapshotProvider(SceneSnapshot.Empty);
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        using var projection = new ProjectSceneSessionProjection(
            projectSessions,
            scenes,
            dispatcher);

        projectSessions.Publish(ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                @"D:\Projects\Example",
                "Example",
                Guid.Parse("45ad5a9c-4c1f-4723-966c-21c0ac638932"))));

        Assert.Same(SceneSnapshot.Empty, scenes.GetCurrentSnapshot());
        Assert.Equal(1, dispatcher.PostCount);

        dispatcher.RunPostedActions();

        var snapshot = scenes.GetCurrentSnapshot();
        Assert.Equal("Untitled Scene", snapshot.DisplayName);
        Assert.Equal(1, snapshot.Revision);
        Assert.Collection(
            snapshot.Objects,
            root =>
            {
                Assert.Equal("Untitled Scene", root.DisplayName);
                Assert.Null(root.ParentId);
            },
            camera =>
            {
                Assert.Equal("Main Camera", camera.DisplayName);
                Assert.Equal(snapshot.Id, camera.ParentId);
            });
    }

    [Fact]
    public void Dispose_stops_project_scene_updates()
    {
        var projectSessions = new StubProjectSessionService();
        var scenes = new InMemorySceneSnapshotProvider(SceneSnapshot.Empty);
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var projection = new ProjectSceneSessionProjection(
            projectSessions,
            scenes,
            dispatcher);

        projection.Dispose();
        projectSessions.Publish(ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                @"D:\Projects\Example",
                "Example",
                Guid.Parse("45ad5a9c-4c1f-4723-966c-21c0ac638932"))));

        Assert.Equal(0, dispatcher.PostCount);
        Assert.Same(SceneSnapshot.Empty, scenes.GetCurrentSnapshot());
    }

    private sealed class StubProjectSessionService : IProjectSessionService
    {
        public event EventHandler? SnapshotChanged;

        public ProjectSessionSnapshot Current { get; private set; } =
            ProjectSessionSnapshot.NoProject;

        public ProjectSessionOperationResult CreateMinimalProject(
            string projectRoot,
            string projectName)
        {
            return ProjectSessionOperationResult.Failure(
                "Not used by this test.");
        }

        public ProjectSessionOperationResult OpenProject(string projectRoot)
        {
            return ProjectSessionOperationResult.Failure(
                "Not used by this test.");
        }

        public void Publish(ProjectSessionSnapshot snapshot)
        {
            Current = snapshot;
            SnapshotChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    private sealed class CapturingUiDispatcher(bool hasAccess) : IEditorUiDispatcher
    {
        private readonly List<Action> postedActions_ = [];

        public int PostCount => postedActions_.Count;

        public bool CheckAccess() => hasAccess;

        public void Post(Action action)
        {
            postedActions_.Add(action);
        }

        public void RunPostedActions()
        {
            foreach (var action in postedActions_.ToArray())
            {
                action();
            }
            postedActions_.Clear();
        }
    }
}
