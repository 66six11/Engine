using System;
using System.Collections.Immutable;
using System.Linq;
using Asharia.Studio.Application.Diagnostics;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Editor.Shell.Docking.DropTargets;
using Editor.Shell.Diagnostics;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.Views.Docking;
using Editor.Shell.Views.Windowing;
using Editor.Shell.Views.Panels;
using Asharia.Studio.TestSupport;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class StudioDiagnosticsPanelHeadlessTests
{
    [AvaloniaFact]
    public void One_panel_hosts_console_timeline_and_actionable_problems()
    {
        var hub = new StudioDiagnosticHub();
        hub.PublishLog(Log(
            hub,
            StudioLogLevel.Information,
            "render",
            "renderer",
            "Frame submitted."));
        hub.PublishDiagnostic(Problem(
            hub,
            StudioDiagnosticSeverity.Error,
            "STUDIO-RENDER-001",
            "renderer",
            "Shader compilation failed.",
            "Open the shader and correct the reported syntax."));
        using var viewModel = new StudioDiagnosticsPanelViewModel(hub);
        var view = new StudioDiagnosticsPanelView { DataContext = viewModel };
        var window = new Window { Width = 900, Height = 360, Content = view };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var tabs = Assert.IsType<TabControl>(
                view.FindControl<TabControl>("DiagnosticsTabs"));
            Assert.Equal(2, tabs.ItemCount);

            var consoleList = Assert.IsType<ListBox>(
                view.FindControl<ListBox>("ConsoleList"));
            Assert.Equal(1, consoleList.ItemCount);
            Assert.Single(consoleList.GetVisualDescendants().OfType<VirtualizingStackPanel>());
            consoleList.SelectedIndex = 0;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("Frame submitted.", viewModel.Console.SelectedRow?.Message);
            Assert.Contains(
                "Frame submitted.",
                Assert.IsType<Border>(view.FindControl<Border>("ConsoleDetails"))
                    .GetVisualDescendants()
                    .OfType<TextBlock>()
                    .Single()
                    .Text);

            tabs.SelectedIndex = 1;
            Dispatcher.UIThread.RunJobs();
            var problemsList = Assert.IsType<ListBox>(
                view.FindControl<ListBox>("ProblemsList"));
            Assert.Equal(1, problemsList.ItemCount);
            Assert.Single(problemsList.GetVisualDescendants().OfType<VirtualizingStackPanel>());
            problemsList.SelectedIndex = 0;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("STUDIO-RENDER-001", viewModel.Problems.SelectedRow?.Code);
            var problemDetails = Assert.IsType<Border>(
                    view.FindControl<Border>("ProblemsDetails"))
                .GetVisualDescendants()
                .OfType<TextBlock>()
                .Single()
                .Text;
            Assert.Contains("Shader compilation failed.", problemDetails);
            Assert.Contains("Action: Open the shader", problemDetails);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void Toolbars_bind_search_filters_collapse_pause_and_view_only_clear()
    {
        var hub = new StudioDiagnosticHub();
        hub.PublishLog(Log(
            hub,
            StudioLogLevel.Debug,
            "editor",
            "project",
            "Project scan started."));
        hub.PublishLog(Log(
            hub,
            StudioLogLevel.Error,
            "render",
            "renderer",
            "Frame submission failed."));
        hub.PublishDiagnostic(Problem(
            hub,
            StudioDiagnosticSeverity.Warning,
            "STUDIO-PROJECT-001",
            "project",
            "Project metadata is incomplete.",
            "Fill in the missing project metadata."));
        using var viewModel = new StudioDiagnosticsPanelViewModel(hub);
        var view = new StudioDiagnosticsPanelView { DataContext = viewModel };
        var window = new Window { Width = 900, Height = 360, Content = view };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var consoleSearch = Assert.IsType<TextBox>(
                view.FindControl<TextBox>("ConsoleSearchBox"));
            consoleSearch.Text = "submission";
            Dispatcher.UIThread.RunJobs();
            Assert.Single(viewModel.Console.Rows);
            Assert.Equal("Frame submission failed.", viewModel.Console.Rows[0].Message);

            var collapse = Assert.IsType<ToggleButton>(
                view.FindControl<ToggleButton>("ConsoleCollapseToggle"));
            collapse.IsChecked = false;
            Dispatcher.UIThread.RunJobs();
            Assert.False(viewModel.Console.CollapseRepeated);

            var follow = Assert.IsType<ToggleButton>(
                view.FindControl<ToggleButton>("ConsoleFollowToggle"));
            Assert.True(follow.IsChecked);
            follow.IsChecked = false;
            Dispatcher.UIThread.RunJobs();
            Assert.False(viewModel.Console.FollowTail);

            var pause = Assert.IsType<Button>(
                view.FindControl<Button>("ConsolePauseButton"));
            Assert.NotNull(pause.Command);
            pause.Command!.Execute(pause.CommandParameter);
            Dispatcher.UIThread.RunJobs();
            Assert.True(viewModel.Console.IsPaused);
            Assert.Equal("Resume", pause.Content);

            var clear = Assert.IsType<Button>(
                view.FindControl<Button>("ConsoleClearButton"));
            Assert.NotNull(clear.Command);
            clear.Command!.Execute(clear.CommandParameter);
            Dispatcher.UIThread.RunJobs();
            Assert.Empty(viewModel.Console.Rows);
            Assert.Equal(2, hub.ReadLogs(maxCount: hub.LogCapacity).Items.Length);

            Assert.NotNull(view.FindControl<ComboBox>("ConsoleLevelFilter"));
            Assert.NotNull(view.FindControl<ComboBox>("ConsoleSourceFilter"));
            Assert.NotNull(view.FindControl<TextBox>("ProblemsSearchBox"));
            Assert.NotNull(view.FindControl<ComboBox>("ProblemsSeverityFilter"));
            Assert.NotNull(view.FindControl<ComboBox>("ProblemsSourceFilter"));
            Assert.NotNull(view.FindControl<ToggleButton>("ProblemsCollapseToggle"));
            Assert.NotNull(view.FindControl<Button>("ProblemsClearButton"));
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void Timeline_uses_dense_virtualized_rows_for_bounded_history()
    {
        var hub = new StudioDiagnosticHub(logCapacity: 256);
        for (var index = 0; index < 192; index++)
        {
            hub.PublishLog(Log(
                hub,
                StudioLogLevel.Information,
                "render",
                "renderer",
                $"Frame {index:000} submitted."));
        }

        using var viewModel = new StudioDiagnosticsPanelViewModel(hub);
        var view = new StudioDiagnosticsPanelView { DataContext = viewModel };
        var window = new Window { Width = 900, Height = 240, Content = view };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var list = Assert.IsType<ListBox>(view.FindControl<ListBox>("ConsoleList"));
            Assert.Equal(192, list.ItemCount);
            Assert.Single(list.GetVisualDescendants().OfType<VirtualizingStackPanel>());
            var realizedRows = list.GetVisualDescendants()
                .OfType<ListBoxItem>()
                .ToArray();
            Assert.NotEmpty(realizedRows);
            Assert.True(realizedRows.Length < list.ItemCount);
            Assert.All(
                realizedRows,
                row => Assert.InRange(row.Bounds.Height, 21.5d, 22.5d));
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void Closing_and_reopening_diagnostics_reuses_one_keep_alive_projection()
    {
        using var shell = StudioShellTestFactory.Create();
        var workspace = shell.DockWorkspace;
        var firstTab = workspace.BottomWindow.Tabs.Single(
            tab => string.Equals(tab.Id, "diagnostics", StringComparison.Ordinal));
        var firstProjection = Assert.IsType<StudioDiagnosticsPanelViewModel>(
            firstTab.Content);

        Assert.True(workspace.ClosePanel("diagnostics"));
        Assert.False(workspace.ContainsPanel("diagnostics"));
        Assert.True(workspace.OpenPanel("diagnostics"));

        var reopenedTab = workspace.BottomWindow.Tabs.Single(
            tab => string.Equals(tab.Id, "diagnostics", StringComparison.Ordinal));
        Assert.Same(firstProjection, reopenedTab.Content);
    }

    [AvaloniaFact]
    public void Console_and_problems_lists_remain_visible_in_the_default_bottom_layout()
    {
        AssertListsHaveViewportInDefaultBottomLayout();
    }

    [AvaloniaFact]
    public void Console_and_problems_lists_remain_visible_in_a_minimum_floating_window()
    {
        AssertListsHaveViewportInMinimumFloatingLayout();
    }

    private static void AssertListsHaveViewportInDefaultBottomLayout()
    {
        using var shell = StudioShellTestFactory.Create();
        var window = new Window
        {
            Width = 900,
            Height = 560,
            Content = new EditorDockWorkspaceView
            {
                DataContext = shell.DockWorkspace,
            },
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var view = Assert.Single(
                window.GetVisualDescendants().OfType<StudioDiagnosticsPanelView>());
            AssertListsHaveViewport(view, "default 210px bottom dock");
        }
        finally
        {
            window.Close();
        }
    }

    private static void AssertListsHaveViewportInMinimumFloatingLayout()
    {
        using var shell = StudioShellTestFactory.Create();
        var hub = Assert.IsType<StudioDiagnosticHub>(shell.DiagnosticSource);
        hub.PublishLog(Log(
            hub,
            StudioLogLevel.Information,
            "render",
            "renderer",
            "A compact viewport log entry."));
        hub.PublishDiagnostic(Problem(
            hub,
            StudioDiagnosticSeverity.Warning,
            "STUDIO-COMPACT-001",
            "renderer",
            "A compact viewport problem.",
            "Review the compact viewport details."));
        var diagnosticsTab = shell.DockWorkspace.BottomWindow.Tabs.Single(
            tab => string.Equals(tab.Id, "diagnostics", StringComparison.Ordinal));
        shell.DockWorkspace.BeginDrag(diagnosticsTab);
        var request = Assert.IsType<EditorDockFloatingWindowRequest>(
            shell.DockWorkspace.CompleteDrag(new EditorDockDropTarget(
                EditorDockDropOperation.Float,
                EditorDockDropGuideKind.Float,
                null,
                null,
                new Avalonia.Rect(0, 0, 240, 180),
                "Float diagnostics")));
        var window = new EditorDockFloatingWindow
        {
            Width = 240,
            Height = 180,
            DataContext = request.Window,
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var view = Assert.Single(
                window.GetVisualDescendants().OfType<StudioDiagnosticsPanelView>());
            Assert.IsType<StudioDiagnosticsPanelViewModel>(view.DataContext).Refresh();
            AssertListsHaveViewport(view, "240x180 floating window");
        }
        finally
        {
            window.Close();
        }
    }

    private static void AssertListsHaveViewport(
        StudioDiagnosticsPanelView view,
        string layout)
    {
        var tabs = Assert.IsType<TabControl>(
            view.FindControl<TabControl>("DiagnosticsTabs"));
        tabs.SelectedIndex = 0;
        Dispatcher.UIThread.RunJobs();
        var console = Assert.IsType<ListBox>(view.FindControl<ListBox>("ConsoleList"));
        if (console.ItemCount > 0)
        {
            console.SelectedIndex = 0;
            Dispatcher.UIThread.RunJobs();
        }
        Assert.True(console.Bounds.Height > 0, $"Console has no viewport in {layout}.");

        tabs.SelectedIndex = 1;
        Dispatcher.UIThread.RunJobs();
        var problems = Assert.IsType<ListBox>(view.FindControl<ListBox>("ProblemsList"));
        if (problems.ItemCount > 0)
        {
            problems.SelectedIndex = 0;
            Dispatcher.UIThread.RunJobs();
        }
        Assert.True(problems.Bounds.Height > 0, $"Problems has no viewport in {layout}.");
    }

    private static StudioLogWrite Log(
        IStudioDiagnosticSource source,
        StudioLogLevel level,
        string channel,
        string component,
        string message) =>
        new(
            level,
            channel,
            Context(source, component),
            message,
            message);

    private static StudioDiagnosticWrite Problem(
        IStudioDiagnosticSource source,
        StudioDiagnosticSeverity severity,
        string code,
        string component,
        string message,
        string remediation) =>
        new(
            severity,
            StudioDiagnosticChannel.Problem,
            code,
            "validation",
            Context(source, component),
            message,
            remediation,
            ImmutableArray<StudioDiagnosticAttribute>.Empty);

    private static StudioDiagnosticContext Context(
        IStudioDiagnosticSource source,
        string component) =>
        new(
            StudioRecordOrigin.Managed,
            "asharia.studio.tests",
            component,
            StudioDiagnosticScope.Process(source.ProcessIdentity));
}
