using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.TestSupport;
using Editor.Shell.ViewModels.Panels;
using Editor.Shell.ViewModels.Windowing;
using Editor.Shell.Views.Panels;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class StudioHierarchyPanelHeadlessTests
{
    private static int nextRuntimeEntityIndex_;

    [AvaloniaFact]
    public void Snapshot_replacement_remaps_selection_to_the_new_entity_instance()
    {
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        var sessionId = ProjectSessionId.CreateNew();
        var sceneId = Guid.NewGuid();
        var objectId = Guid.NewGuid();
        var original = Entity(objectId, "Camera");
        projectSession.Publish(Ready(sessionId, sceneId, revision: 1, original));
        shell.SelectedEntity = original;
        using var viewModel = new StudioHierarchyPanelViewModel(shell);

        var replacement = Entity(objectId, "Renamed Camera");
        projectSession.Publish(Ready(sessionId, sceneId, revision: 2, replacement));
        Dispatcher.UIThread.RunJobs();

        Assert.NotSame(original, shell.SelectedEntity);
        Assert.Same(replacement, shell.SelectedEntity);
        Assert.Same(replacement, viewModel.SelectedRow?.Entity);
        Assert.Equal(objectId, viewModel.SelectedRow?.StableId);
        Assert.Equal("Renamed Camera", shell.InspectorName);
    }

    [AvaloniaFact]
    public void Projection_tracks_each_shell_applied_snapshot_in_order()
    {
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        using var viewModel = new StudioHierarchyPanelViewModel(shell);
        var sessionId = ProjectSessionId.CreateNew();
        var sceneId = Guid.NewGuid();
        var observedEntityNames = new List<string>();
        shell.PropertyChanged += OnShellPropertyChanged;

        Task.Run(() =>
        {
            projectSession.Publish(Ready(
                sessionId,
                sceneId,
                revision: 1,
                Entity("Snapshot A")));
            projectSession.Publish(Ready(
                sessionId,
                sceneId,
                revision: 2,
                Entity("Snapshot B")));
        }).GetAwaiter().GetResult();
        Dispatcher.UIThread.RunJobs();

        Assert.Equal(["Snapshot A", "Snapshot B"], observedEntityNames);

        void OnShellPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (string.Equals(
                    e.PropertyName,
                    nameof(StudioShellViewModel.SceneEntities),
                    StringComparison.Ordinal))
            {
                observedEntityNames.Add(viewModel.VisibleRows[1].DisplayName);
            }
        }
    }

    [AvaloniaFact]
    public void Changing_scene_or_project_session_scope_clears_selection()
    {
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        var sessionId = ProjectSessionId.CreateNew();
        var sceneId = Guid.NewGuid();
        var objectId = Guid.NewGuid();
        var original = Entity(objectId, "Camera");
        projectSession.Publish(Ready(sessionId, sceneId, revision: 1, original));
        shell.SelectedEntity = original;
        using var viewModel = new StudioHierarchyPanelViewModel(shell);

        var otherSceneEntity = Entity(objectId, "Other Scene Camera");
        var otherSceneId = Guid.NewGuid();
        projectSession.Publish(Ready(
            sessionId,
            otherSceneId,
            revision: 1,
            otherSceneEntity));
        Dispatcher.UIThread.RunJobs();

        Assert.Null(shell.SelectedEntity);
        Assert.Null(viewModel.SelectedRow);

        shell.SelectedEntity = otherSceneEntity;
        var otherSessionEntity = Entity(objectId, "Other Session Camera");
        projectSession.Publish(Ready(
            ProjectSessionId.CreateNew(),
            otherSceneId,
            revision: 2,
            otherSessionEntity));
        Dispatcher.UIThread.RunJobs();

        Assert.Null(shell.SelectedEntity);
        Assert.Null(viewModel.SelectedRow);
    }

    [AvaloniaFact]
    public void Presentation_root_selection_does_not_leak_to_another_scene_scope()
    {
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        var sessionId = ProjectSessionId.CreateNew();
        projectSession.Publish(Ready(
            sessionId,
            Guid.NewGuid(),
            revision: 1,
            Entity("Camera")));
        using var viewModel = new StudioHierarchyPanelViewModel(shell);
        viewModel.SelectedRow = viewModel.VisibleRows[0];

        projectSession.Publish(Ready(
            sessionId,
            Guid.NewGuid(),
            revision: 1,
            Entity("Light")));
        Dispatcher.UIThread.RunJobs();

        Assert.Null(shell.SelectedEntity);
        Assert.Null(viewModel.SelectedRow);
    }

    [AvaloniaFact]
    public void Expander_button_routes_to_the_panel_projection_and_toggles_visible_rows()
    {
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        projectSession.Publish(Ready(
            Guid.NewGuid(),
            revision: 1,
            Entity("Camera"),
            Entity("Light")));
        using var viewModel = new StudioHierarchyPanelViewModel(shell);
        var view = new StudioHierarchyPanelView { DataContext = viewModel };
        var window = new Window { Width = 320, Height = 220, Content = view };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var list = Assert.IsType<ListBox>(view.FindControl<ListBox>("HierarchyList"));
            Assert.Equal(3, list.ItemCount);

            var expander = list.GetVisualDescendants()
                .OfType<Button>()
                .Single(button => button.IsVisible);
            expander.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(1, list.ItemCount);
            expander = list.GetVisualDescendants()
                .OfType<Button>()
                .Single(button => button.IsVisible);
            expander.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(3, list.ItemCount);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void Hierarchy_realizes_dense_virtualized_rows_and_projects_selection_to_inspector()
    {
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        var entities = Enumerable.Range(0, 64)
            .Select(index => Entity($"Entity {index:00}"))
            .ToArray();
        projectSession.Publish(Ready(Guid.NewGuid(), revision: 1, entities));
        using var viewModel = new StudioHierarchyPanelViewModel(shell);
        var view = new StudioHierarchyPanelView
        {
            DataContext = viewModel,
        };
        var window = new Window
        {
            Width = 320,
            Height = 180,
            Content = view,
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var list = Assert.IsType<ListBox>(view.FindControl<ListBox>("HierarchyList"));
            Assert.Equal(65, list.ItemCount);
            Assert.Single(list.GetVisualDescendants().OfType<VirtualizingStackPanel>());

            var realizedRows = list.GetVisualDescendants()
                .OfType<ListBoxItem>()
                .ToArray();
            Assert.NotEmpty(realizedRows);
            Assert.True(realizedRows.Length < list.ItemCount);
            Assert.All(
                realizedRows,
                row => Assert.InRange(row.Bounds.Height, 19.5d, 20.5d));

            var selectedRow = viewModel.VisibleRows.Single(
                row => row.StableId == entities[27].ObjectId);
            list.SelectedItem = selectedRow;
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(entities[27].ObjectId, shell.SelectedEntity?.ObjectId);
            Assert.Equal("Entity 27", shell.InspectorName);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void Filtering_hides_but_does_not_clear_selection_and_clear_restores_it()
    {
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        var camera = Entity("Main Camera");
        var light = Entity("Key Light");
        projectSession.Publish(Ready(Guid.NewGuid(), revision: 1, camera, light));
        shell.SelectedEntity = light;
        using var viewModel = new StudioHierarchyPanelViewModel(shell);
        var view = new StudioHierarchyPanelView
        {
            DataContext = viewModel,
        };
        var window = new Window
        {
            Width = 320,
            Height = 220,
            Content = view,
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var search = Assert.IsType<TextBox>(
                view.FindControl<TextBox>("HierarchySearchBox"));
            var list = Assert.IsType<ListBox>(view.FindControl<ListBox>("HierarchyList"));
            search.Text = "camera";
            Dispatcher.UIThread.RunJobs();

            Assert.Null(list.SelectedItem);
            Assert.Same(light, shell.SelectedEntity);
            Assert.Equal("Key Light", shell.InspectorName);

            search.Text = string.Empty;
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(light.ObjectId, viewModel.SelectedRow?.StableId);
            Assert.Same(viewModel.SelectedRow, list.SelectedItem);
            Assert.Same(light, shell.SelectedEntity);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void Explicit_list_deselection_clears_a_visible_entity_selection()
    {
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        var camera = Entity("Main Camera");
        projectSession.Publish(Ready(Guid.NewGuid(), revision: 1, camera));
        shell.SelectedEntity = camera;
        using var viewModel = new StudioHierarchyPanelViewModel(shell);
        var view = new StudioHierarchyPanelView { DataContext = viewModel };
        var window = new Window { Width = 320, Height = 220, Content = view };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var list = Assert.IsType<ListBox>(view.FindControl<ListBox>("HierarchyList"));
            Assert.NotNull(list.SelectedItem);

            list.SelectedItem = null;
            Dispatcher.UIThread.RunJobs();

            Assert.Null(shell.SelectedEntity);
            Assert.Null(viewModel.SelectedRow);
            Assert.Equal(string.Empty, shell.InspectorName);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void Empty_state_distinguishes_an_unloaded_scene_from_no_filter_matches()
    {
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        using var viewModel = new StudioHierarchyPanelViewModel(shell);
        var view = new StudioHierarchyPanelView
        {
            DataContext = viewModel,
        };
        var window = new Window
        {
            Width = 320,
            Height = 220,
            Content = view,
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var emptyState = Assert.IsType<Border>(
                view.FindControl<Border>("HierarchyEmptyState"));
            var emptyStateText = Assert.IsType<TextBlock>(
                view.FindControl<TextBlock>("HierarchyEmptyStateText"));
            Assert.True(emptyState.IsVisible);
            Assert.Equal("No scene loaded", emptyStateText.Text);

            projectSession.Publish(Ready(
                Guid.NewGuid(),
                revision: 1,
                Entity("Main Camera")));
            viewModel.FilterText = "missing";
            Dispatcher.UIThread.RunJobs();

            Assert.True(emptyState.IsVisible);
            Assert.Equal("No matching objects", emptyStateText.Text);

            viewModel.FilterText = string.Empty;
            Dispatcher.UIThread.RunJobs();

            Assert.False(emptyState.IsVisible);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void Dispose_detaches_projection_from_later_shell_snapshots()
    {
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        projectSession.Publish(Ready(
            Guid.NewGuid(),
            revision: 1,
            Entity("Camera")));
        Dispatcher.UIThread.RunJobs();
        var viewModel = new StudioHierarchyPanelViewModel(shell);
        var rowsBeforeDispose = viewModel.VisibleRows;

        viewModel.Dispose();
        viewModel.Dispose();
        projectSession.Publish(Ready(
            Guid.NewGuid(),
            revision: 2,
            Entity("Light")));
        Dispatcher.UIThread.RunJobs();

        Assert.Same(rowsBeforeDispose, viewModel.VisibleRows);
        Assert.Equal("Camera", viewModel.VisibleRows[1].DisplayName);
    }

    private static SceneEntitySnapshot Entity(string name) =>
        Entity(Guid.NewGuid(), name);

    private static SceneEntitySnapshot Entity(Guid objectId, string name) =>
        new(
            objectId,
            new EntityId(
                checked((uint)Interlocked.Increment(ref nextRuntimeEntityIndex_)),
                1U),
            name,
            TransformValue.Identity);

    private static ProjectSessionSnapshot Ready(
        Guid sceneId,
        ulong revision,
        params SceneEntitySnapshot[] entities) =>
        Ready(ProjectSessionId.CreateNew(), sceneId, revision, entities);

    private static ProjectSessionSnapshot Ready(
        ProjectSessionId sessionId,
        Guid sceneId,
        ulong revision,
        params SceneEntitySnapshot[] entities) =>
        ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                sessionId,
                Guid.NewGuid(),
                "Sample",
                "C:\\Projects\\Sample"),
            new SceneDocumentSnapshot(
                sceneId,
                "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
                revision,
                savedRevision: 1,
                entities),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
}
