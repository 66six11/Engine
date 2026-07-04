using System;
using Editor.Core.Models.Workbench;
using Xunit;

namespace Editor.Tests.Core;

public sealed class EditorCommandFeedbackSnapshotTests
{
    [Fact]
    public void FromResult_maps_success_to_success_feedback()
    {
        var snapshot = EditorCommandFeedbackSnapshot.FromResult(
            WorkbenchCommandExecutionResult.Success("workbench.panel.console"));

        Assert.Equal(EditorCommandFeedbackSeverity.Success, snapshot.Severity);
        Assert.Equal(WorkbenchCommandExecutionStatus.Succeeded, snapshot.Status);
        Assert.Equal("workbench.panel.console", snapshot.CommandId);
        Assert.Equal("Command 'workbench.panel.console' completed.", snapshot.Message);
    }

    [Fact]
    public void FromResult_maps_disabled_to_warning_feedback()
    {
        var snapshot = EditorCommandFeedbackSnapshot.FromResult(
            WorkbenchCommandExecutionResult.Disabled("workbench.panel.console", "Disabled by test"));

        Assert.Equal(EditorCommandFeedbackSeverity.Warning, snapshot.Severity);
        Assert.Equal(WorkbenchCommandExecutionStatus.Disabled, snapshot.Status);
        Assert.Equal("Disabled by test", snapshot.Message);
    }

    [Fact]
    public void FromResult_maps_not_found_to_error_feedback()
    {
        var snapshot = EditorCommandFeedbackSnapshot.FromResult(
            WorkbenchCommandExecutionResult.NotFound("missing.command"));

        Assert.Equal(EditorCommandFeedbackSeverity.Error, snapshot.Severity);
        Assert.Equal(WorkbenchCommandExecutionStatus.NotFound, snapshot.Status);
        Assert.Contains("is not registered", snapshot.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void FromResult_maps_failed_to_error_feedback()
    {
        var snapshot = EditorCommandFeedbackSnapshot.FromResult(
            WorkbenchCommandExecutionResult.Failed("workbench.panel.console", "Failed by test"));

        Assert.Equal(EditorCommandFeedbackSeverity.Error, snapshot.Severity);
        Assert.Equal(WorkbenchCommandExecutionStatus.Failed, snapshot.Status);
        Assert.Equal("Failed by test", snapshot.Message);
    }

    [Fact]
    public void FromResult_uses_fallback_message_for_blank_failure()
    {
        var snapshot = EditorCommandFeedbackSnapshot.FromResult(
            WorkbenchCommandExecutionResult.Failed("workbench.panel.console", "   "));

        Assert.Equal("Command 'workbench.panel.console' did not complete.", snapshot.Message);
    }
}
