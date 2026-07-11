using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Editor.Core.Abstractions;
using Asharia.Editor.Diagnostics;
using Editor.Core.Models.Extensions;
using Editor.Features.Console.ViewModels;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Shell.Compatibility;
using Editor.Shell.Composition;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class StudioCompositionRootTests
{
    [Fact]
    public void CreateDefaultComposition_declares_modules_once_for_panel_and_action_registries()
    {
        var composition = StudioCompositionRoot.CreateDefaultComposition();

        Assert.Equal(
            ["scene-view", "hierarchy", "inspector", "console", "problems", "frame-debugger", "ui-style"],
            composition.PanelRegistry.GetAll().Select(panel => panel.Id));
        Assert.Equal(
            [
                "workbench.commandPalette.open",
                "workbench.about.open",
                "workbench.panel.scene-view",
                "workbench.panel.hierarchy",
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
        await using var session = new StudioCompositionRoot().CreateMainWindowSession(savedLayout: null);
        var viewModel = session.MainWindowViewModel;

        var hierarchy = Assert.IsType<HierarchyPanelViewModel>(
            viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy").Content);
        var inspector = Assert.IsType<InspectorPanelViewModel>(
            viewModel.DockWorkspace.RightWindow.Tabs.Single(tab => tab.Id == "inspector").Content);

        var cube = hierarchy.Nodes.Single(node => node.Id == "scene:main/cube");
        hierarchy.SelectedNode = cube;

        Assert.Equal("hierarchy", inspector.CurrentSelection.ActiveContextId);
        Assert.Equal("Demo Cube", inspector.Document?.Title);
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
}
