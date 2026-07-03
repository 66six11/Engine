using System;
using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Core.Models.Diagnostics;
using Editor.Core.Services;
using Editor.Features.Problems.ViewModels;
using Xunit;

namespace Editor.Tests.Features.Problems;

public sealed class ProblemsPanelViewModelTests
{
    [Fact]
    public void Problems_panel_exposes_empty_state_before_diagnostics()
    {
        var diagnostics = new EditorDiagnosticService();
        var viewModel = new ProblemsPanelViewModel(
            diagnostics,
            new CapturingUiDispatcher(hasAccess: true));

        Assert.Empty(viewModel.Records);
        Assert.False(viewModel.HasRecords);
        Assert.True(viewModel.HasNoRecords);
        Assert.Equal("No problems", viewModel.EmptyStateText);
    }

    [Fact]
    public void Problems_panel_tracks_problem_diagnostics_only()
    {
        var diagnostics = new EditorDiagnosticService();
        var viewModel = new ProblemsPanelViewModel(
            diagnostics,
            new CapturingUiDispatcher(hasAccess: true));
        diagnostics.Publish(
            EditorDiagnosticSeverity.Info,
            EditorDiagnosticChannel.Debug,
            "debug",
            "command",
            "Debug");
        var problem = diagnostics.Publish(
            EditorDiagnosticSeverity.Error,
            EditorDiagnosticChannel.Problem,
            "validation",
            "scene",
            "Problem");

        Assert.Equal([problem], viewModel.Records);
        Assert.Equal("1", viewModel.RecordCountText);
        Assert.True(viewModel.HasRecords);
        Assert.False(viewModel.HasNoRecords);
    }

    [Fact]
    public void Problems_panel_posts_refresh_when_dispatcher_has_no_access()
    {
        var diagnostics = new EditorDiagnosticService();
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var viewModel = new ProblemsPanelViewModel(diagnostics, dispatcher);

        var problem = diagnostics.Publish(
            EditorDiagnosticSeverity.Warning,
            EditorDiagnosticChannel.Problem,
            "validation",
            "asset",
            "Missing asset.");

        Assert.Empty(viewModel.Records);
        var action = Assert.Single(dispatcher.PostedActions);

        action();

        Assert.Equal([problem], viewModel.Records);
        Assert.True(viewModel.HasRecords);
        Assert.False(viewModel.HasNoRecords);
    }

    [Fact]
    public void Dispose_unsubscribes_from_diagnostics()
    {
        var diagnostics = new EditorDiagnosticService();
        var viewModel = new ProblemsPanelViewModel(
            diagnostics,
            new CapturingUiDispatcher(hasAccess: true));
        viewModel.Dispose();

        diagnostics.Publish(
            EditorDiagnosticSeverity.Error,
            EditorDiagnosticChannel.Problem,
            "validation",
            "asset",
            "Missing asset.");

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
