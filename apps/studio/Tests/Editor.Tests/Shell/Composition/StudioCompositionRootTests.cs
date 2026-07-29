using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Editor.Diagnostics;
using Asharia.Editor.Panels;
using Asharia.Editor.Projects;
using Asharia.Studio.Application.Projects;
using Editor.Core.Abstractions;
using Editor.Core.Models.Extensions;
using Editor.Features.Console.ViewModels;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Features.Project.ViewModels;
using Editor.Shell.Compatibility;
using Editor.Shell.Composition;
using Editor.Shell.Docking.Layout;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class StudioCompositionRootTests
{
    [Fact]
    public async Task CreateMainWindowSession_uses_shell_owned_default_layout_when_no_layout_is_saved()
    {
        var session = new StudioCompositionRoot().CreateMainWindowSession(savedLayout: null);
        try
        {
            var workspace = session.MainWindowViewModel.DockWorkspace;

            Assert.True(workspace.ContainsPanel("hierarchy"));
            Assert.True(workspace.ContainsPanel("project"));
            Assert.True(workspace.ContainsPanel("scene-view"));
            Assert.True(workspace.ContainsPanel("inspector"));
            Assert.False(workspace.ContainsPanel("console"));
            Assert.False(workspace.ContainsPanel("problems"));
            Assert.False(workspace.ContainsPanel("frame-debugger"));
            Assert.False(workspace.ContainsPanel("ui-style"));
        }
        finally
        {
            await session.DisposeAsync();
        }
    }

    [Fact]
    public async Task CreateMainWindowSession_preserves_a_valid_saved_layout_over_the_default_preset()
    {
        var savedLayout = new EditorDockLayoutSnapshot
        {
            ActiveWindowId = "saved-tools",
            Root = new EditorDockLayoutNodeSnapshot
            {
                Kind = "Window",
                Id = "node-saved-tools",
                WindowId = "saved-tools",
                WindowTitle = "Saved Tools",
                WindowArea = EditorDockArea.Center,
                WindowRole = "Saved user layout",
                TabIds = ["ui-style"],
                ActiveTabId = "ui-style",
            },
        };

        await using var session = new StudioCompositionRoot()
            .CreateMainWindowSession(savedLayout);

        Assert.True(session.MainWindowViewModel.DockWorkspace.ContainsPanel("ui-style"));
        Assert.False(session.MainWindowViewModel.DockWorkspace.ContainsPanel("scene-view"));
    }

    [Fact]
    public void CreateDefaultComposition_declares_modules_once_for_panel_and_action_registries()
    {
        var composition = StudioCompositionRoot.CreateDefaultComposition();

        Assert.Equal(
            ["scene-view", "hierarchy", "project", "inspector", "console", "problems", "frame-debugger", "ui-style"],
            composition.PanelRegistry.GetAll().Select(panel => panel.Id));
        Assert.Equal(
            [
                "workbench.commandPalette.open",
                "workbench.about.open",
                "workbench.panel.scene-view",
                "workbench.panel.hierarchy",
                "workbench.panel.project",
                "workbench.panel.inspector",
                "workbench.panel.console",
                "workbench.panel.problems",
                "workbench.panel.frame-debugger",
                "workbench.panel.ui-style",
            ],
            composition.ActionRegistry.GetAll().Select(action => action.Id));
    }

    [Fact]
    public async Task CreateMainWindowViewModel_uses_shared_default_composition()
    {
        await using var session = new StudioCompositionRoot().CreateMainWindowSession(
            savedLayout: null,
            projectOpenSessions: new ProjectOpenSessionSnapshotSource(),
            projectSessions: CreateReadyProjectSessions());
        var viewModel = session.MainWindowViewModel;

        var hierarchy = Assert.IsType<HierarchyPanelViewModel>(
            viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy").Content);
        var inspector = Assert.IsType<InspectorPanelViewModel>(
            viewModel.DockWorkspace.RightWindow.Tabs.Single(tab => tab.Id == "inspector").Content);

        var camera = hierarchy.Nodes.Single(node => node.DisplayName == "Main Camera");
        hierarchy.SelectedNode = camera;

        Assert.Equal("hierarchy", inspector.CurrentSelection.ActiveContextId);
        Assert.Equal("Main Camera", inspector.Document?.Title);
    }

    [Fact]
    public async Task CreateMainWindowSession_shares_diagnostics_between_status_and_panels()
    {
        await using var session = new StudioCompositionRoot().CreateMainWindowSession(savedLayout: null);
        session.MainWindowViewModel.ToolsMenuItems.Single().OpenCommand.Execute(null);

        var console = Assert.IsType<ConsolePanelViewModel>(
            session.Composition.PanelRegistry.GetRequired("console").CreateContent());

        var record = Assert.Single(console.Records);
        Assert.Equal(EditorDiagnosticChannel.Debug, record.Channel);
        Assert.Equal(record.Message, session.MainWindowViewModel.StatusMessageText);
    }

    [Fact]
    public async Task CreateMainWindowSession_shares_project_open_state_with_workbench()
    {
        var projectOpenSessions = new ProjectOpenSessionSnapshotSource(
            new ProjectOpenSessionSnapshot(
                ProjectOpenSessionState.Ready,
                ProjectOpenNextAction.ActivateProjectProfile,
                new ProjectOpenSummarySnapshot(
                    "Example",
                    Guid.Parse("7b535774-005d-47ff-90d7-83165df8bac8"),
                    assetSourceRootCount: 1)));

        await using var session = new StudioCompositionRoot()
            .CreateMainWindowSession(
                savedLayout: null,
                projectOpenSessions: projectOpenSessions);

        var projectPanel = Assert.IsType<ProjectPanelViewModel>(
            session.Composition.PanelRegistry
                .GetRequired("project")
                .CreateContent());

        Assert.Equal(
            "Example",
            session.MainWindowViewModel.ProjectLaunch.ProjectCandidateDisplayName);
        Assert.Equal(
            "No active project",
            session.MainWindowViewModel.ActiveProjectDisplayName);
        Assert.Equal("No active project", projectPanel.ProjectDisplayName);
    }

    [Fact]
    public async Task CreateMainWindowSession_projects_the_injected_active_project()
    {
        var projectSessions = new StubProjectSessionService(
            ProjectSessionSnapshot.Ready(
                new ActiveProjectSnapshot(
                    @"D:\Projects\Example",
                    "Example",
                    Guid.Parse("45ad5a9c-4c1f-4723-966c-21c0ac638932"))));

        await using var session = new StudioCompositionRoot()
            .CreateMainWindowSession(
                savedLayout: null,
                projectOpenSessions: new ProjectOpenSessionSnapshotSource(),
                projectSessions: projectSessions);

        Assert.True(session.MainWindowViewModel.HasActiveProject);
        Assert.Equal(
            "Example",
            session.MainWindowViewModel.ActiveProjectDisplayName);
        Assert.Equal(
            "Untitled Scene — Example — Asharia Studio",
            session.MainWindowViewModel.WindowTitle);
    }

    [Fact]
    public async Task CreateMainWindowSession_keeps_extension_host_alive_until_session_disposal()
    {
        var session = new StudioCompositionRoot().CreateMainWindowSession(savedLayout: null);

        Assert.NotEmpty(session.Composition.PanelRegistry.GetAll());
        Assert.NotEmpty(session.Composition.ActionRegistry.GetAll());
        Assert.NotEmpty(session.Composition.ProviderHost.GetSceneProviders());

        await session.DisposeAsync();

        Assert.Empty(session.Composition.PanelRegistry.GetAll());
        Assert.Empty(session.Composition.ActionRegistry.GetAll());
        Assert.Empty(session.Composition.ProviderHost.GetSceneProviders());
    }

    [Fact]
    public async Task CreateMainWindowSession_activates_extension_host_before_returning_session()
    {
        var activationOrder = new List<string>();
        var disposalOrder = new List<string>();
        var module = new TestExtensionModule(
            "test.lifecycle",
            new RecordingLease("test.lifecycle", disposalOrder),
            _ => activationOrder.Add("test.lifecycle"));

        var session = new StudioCompositionRoot().CreateMainWindowSession(
            savedLayout: null,
            modules: new LegacyEditorModuleCompatibilityAdapter([module]));

        Assert.Equal(["test.lifecycle"], activationOrder);
        Assert.Empty(disposalOrder);

        await session.DisposeAsync();

        Assert.Equal(["test.lifecycle"], disposalOrder);
    }

    private sealed class TestExtensionModule : IEditorExtensionModule
    {
        private readonly IAsyncDisposable? lease_;
        private readonly Action<CancellationToken>? onActivate_;

        public TestExtensionModule(
            string id,
            IAsyncDisposable? lease = null,
            Action<CancellationToken>? onActivate = null)
        {
            Id = new EditorExtensionId(id);
            lease_ = lease;
            onActivate_ = onActivate;
        }

        public EditorExtensionId Id { get; }

        public void Declare(IEditorContributionBuilder builder)
        {
        }

        public ValueTask<IAsyncDisposable?> ActivateAsync(
            IEditorExtensionActivationContext context,
            CancellationToken cancellationToken)
        {
            onActivate_?.Invoke(cancellationToken);
            return ValueTask.FromResult(lease_);
        }
    }

    private static StubProjectSessionService CreateReadyProjectSessions()
    {
        return new StubProjectSessionService(
            ProjectSessionSnapshot.Ready(
                new ActiveProjectSnapshot(
                    @"D:\Projects\Example",
                    "Example",
                    Guid.Parse("45ad5a9c-4c1f-4723-966c-21c0ac638932"))));
    }

    private sealed class RecordingLease(
        string id,
        IList<string> disposalOrder) : IAsyncDisposable
    {
        public ValueTask DisposeAsync()
        {
            disposalOrder.Add(id);
            return ValueTask.CompletedTask;
        }
    }

    private sealed class StubProjectSessionService(
        ProjectSessionSnapshot current) : IProjectSessionService
    {
        public event EventHandler? SnapshotChanged
        {
            add { }
            remove { }
        }

        public ProjectSessionSnapshot Current { get; } = current;

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
    }
}
