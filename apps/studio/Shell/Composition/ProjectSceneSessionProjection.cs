using System;
using Asharia.Editor.Projects;
using Asharia.Editor.Worlds.Snapshots;
using Editor.Core.Abstractions;
using Editor.Core.Services;

namespace Editor.Shell.Composition;

internal sealed class ProjectSceneSessionProjection : IDisposable
{
    private readonly IProjectSessionService projectSessions_;
    private readonly InMemorySceneSnapshotProvider sceneSnapshots_;
    private readonly IEditorUiDispatcher uiDispatcher_;
    private long revision_;
    private bool isDisposed_;

    public ProjectSceneSessionProjection(
        IProjectSessionService projectSessions,
        InMemorySceneSnapshotProvider sceneSnapshots,
        IEditorUiDispatcher uiDispatcher)
    {
        ArgumentNullException.ThrowIfNull(projectSessions);
        ArgumentNullException.ThrowIfNull(sceneSnapshots);
        ArgumentNullException.ThrowIfNull(uiDispatcher);

        projectSessions_ = projectSessions;
        sceneSnapshots_ = sceneSnapshots;
        uiDispatcher_ = uiDispatcher;
        Refresh();
        projectSessions_.SnapshotChanged += OnProjectSessionChanged;
    }

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        projectSessions_.SnapshotChanged -= OnProjectSessionChanged;
    }

    private void OnProjectSessionChanged(object? sender, EventArgs e)
    {
        if (isDisposed_)
        {
            return;
        }
        if (uiDispatcher_.CheckAccess())
        {
            Refresh();
            return;
        }

        uiDispatcher_.Post(Refresh);
    }

    private void Refresh()
    {
        if (isDisposed_)
        {
            return;
        }

        var project = projectSessions_.Current.Project;
        if (project is null)
        {
            sceneSnapshots_.ReplaceSnapshot(SceneSnapshot.Empty);
            return;
        }

        revision_++;
        sceneSnapshots_.ReplaceSnapshot(CreateMinimalScene(project, revision_));
    }

    private static SceneSnapshot CreateMinimalScene(
        ActiveProjectSnapshot project,
        long revision)
    {
        var sceneId = $"project:{project.ProjectId:N}/scene:untitled";
        return new SceneSnapshot(
            sceneId,
            "Untitled Scene",
            revision,
            [
                new SceneObjectSnapshot(
                    sceneId,
                    "Untitled Scene",
                    "scene"),
                new SceneObjectSnapshot(
                    sceneId + "/camera",
                    "Main Camera",
                    "camera",
                    parentId: sceneId),
            ]);
    }
}
