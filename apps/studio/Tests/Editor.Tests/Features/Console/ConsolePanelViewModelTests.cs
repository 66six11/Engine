using System;
using System.Collections.Generic;
using Asharia.Editor.Diagnostics;
using Editor.Core.Abstractions;
using Editor.Core.Services;
using Editor.Features.Console.ViewModels;
using Xunit;

namespace Editor.Tests.Features.Console;

public sealed class ConsolePanelViewModelTests
{
    [Fact]
    public void Console_panel_exposes_empty_state_before_diagnostics()
    {
        var diagnostics = new EditorDiagnosticService();
        var viewModel = new ConsolePanelViewModel(
            diagnostics,
            new CapturingUiDispatcher(hasAccess: true));

        Assert.Empty(viewModel.Records);
        Assert.False(viewModel.HasRecords);
        Assert.True(viewModel.HasNoRecords);
        Assert.Equal("No console diagnostics", viewModel.EmptyStateText);
    }

    [Fact]
    public void Console_panel_tracks_recent_diagnostics()
    {
        var diagnostics = new EditorDiagnosticService();
        var viewModel = new ConsolePanelViewModel(
            diagnostics,
            new CapturingUiDispatcher(hasAccess: true));

        var record = diagnostics.Publish(
            EditorDiagnosticSeverity.Info,
            EditorDiagnosticChannel.Debug,
            "command",
            "workbench",
            "Command completed.");

        Assert.Equal([record], viewModel.Records);
        Assert.Equal("1", viewModel.RecordCountText);
        Assert.True(viewModel.HasRecords);
        Assert.False(viewModel.HasNoRecords);
    }

    [Fact]
    public void Console_panel_posts_refresh_when_dispatcher_has_no_access()
    {
        var diagnostics = new EditorDiagnosticService();
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var viewModel = new ConsolePanelViewModel(diagnostics, dispatcher);

        var record = diagnostics.Publish(
            EditorDiagnosticSeverity.Warning,
            EditorDiagnosticChannel.Debug,
            "provider",
            "scene",
            "Provider warning.");

        Assert.Empty(viewModel.Records);
        var action = Assert.Single(dispatcher.PostedActions);

        action();

        Assert.Equal([record], viewModel.Records);
        Assert.True(viewModel.HasRecords);
        Assert.False(viewModel.HasNoRecords);
    }

    [Fact]
    public void Dispose_unsubscribes_from_diagnostics()
    {
        var diagnostics = new EditorDiagnosticService();
        var viewModel = new ConsolePanelViewModel(
            diagnostics,
            new CapturingUiDispatcher(hasAccess: true));
        viewModel.Dispose();

        diagnostics.Publish(
            EditorDiagnosticSeverity.Info,
            EditorDiagnosticChannel.Debug,
            "command",
            "workbench",
            "Command completed.");

        Assert.Empty(viewModel.Records);
    }

    private sealed class CapturingUiDispatcher(bool hasAccess) : IEditorUiDispatcher
    {
        public List<Action> PostedActions { get; } = [];

        public bool CheckAccess() => hasAccess;

        public void Post(Action action)
        {
            PostedActions.Add(action);
        }
    }
}
