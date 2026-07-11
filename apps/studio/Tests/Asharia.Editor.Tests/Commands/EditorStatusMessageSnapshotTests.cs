using System;
using Asharia.Editor.Commands;
using Xunit;

namespace Asharia.Editor.Tests.Commands;

public sealed class EditorStatusMessageSnapshotTests
{
    [Fact]
    public void FromCommandResult_maps_success_to_success_command_message()
    {
        var snapshot = EditorStatusMessageSnapshot.FromCommandResult(
            EditorCommandExecutionResult.Success("workbench.panel.console"));

        Assert.Equal(EditorStatusMessageSeverity.Success, snapshot.Severity);
        Assert.Equal(EditorStatusMessageSource.Command, snapshot.Source);
        Assert.Equal("Command 'workbench.panel.console' completed.", snapshot.Message);
        Assert.Null(snapshot.TargetPanelId);
    }

    [Fact]
    public void FromCommandResult_maps_disabled_to_warning_command_message()
    {
        var snapshot = EditorStatusMessageSnapshot.FromCommandResult(
            EditorCommandExecutionResult.Disabled("workbench.panel.console", "Disabled by test"));

        Assert.Equal(EditorStatusMessageSeverity.Warning, snapshot.Severity);
        Assert.Equal(EditorStatusMessageSource.Command, snapshot.Source);
        Assert.Equal("Disabled by test", snapshot.Message);
        Assert.Null(snapshot.TargetPanelId);
    }

    [Fact]
    public void FromCommandResult_maps_not_found_to_error_command_message()
    {
        var snapshot = EditorStatusMessageSnapshot.FromCommandResult(
            EditorCommandExecutionResult.NotFound("missing.command"));

        Assert.Equal(EditorStatusMessageSeverity.Error, snapshot.Severity);
        Assert.Equal(EditorStatusMessageSource.Command, snapshot.Source);
        Assert.Contains("is not registered", snapshot.Message, StringComparison.Ordinal);
        Assert.Null(snapshot.TargetPanelId);
    }

    [Fact]
    public void FromCommandResult_maps_failed_to_error_command_message()
    {
        var snapshot = EditorStatusMessageSnapshot.FromCommandResult(
            EditorCommandExecutionResult.Failed("workbench.panel.console", "Failed by test"));

        Assert.Equal(EditorStatusMessageSeverity.Error, snapshot.Severity);
        Assert.Equal(EditorStatusMessageSource.Command, snapshot.Source);
        Assert.Equal("Failed by test", snapshot.Message);
        Assert.Null(snapshot.TargetPanelId);
    }

    [Fact]
    public void FromCommandResult_uses_fallback_message_for_blank_failure()
    {
        var snapshot = EditorStatusMessageSnapshot.FromCommandResult(
            EditorCommandExecutionResult.Failed("workbench.panel.console", "   "));

        Assert.Equal("Command 'workbench.panel.console' did not complete.", snapshot.Message);
        Assert.Null(snapshot.TargetPanelId);
    }

    [Fact]
    public void Constructor_preserves_console_source_and_target_panel()
    {
        var snapshot = new EditorStatusMessageSnapshot(
            EditorStatusMessageSeverity.Debug,
            EditorStatusMessageSource.Console,
            "Console debug line",
            TargetPanelId: "console");

        Assert.Equal(EditorStatusMessageSeverity.Debug, snapshot.Severity);
        Assert.Equal(EditorStatusMessageSource.Console, snapshot.Source);
        Assert.Equal("Console debug line", snapshot.Message);
        Assert.Equal("console", snapshot.TargetPanelId);
    }
}
