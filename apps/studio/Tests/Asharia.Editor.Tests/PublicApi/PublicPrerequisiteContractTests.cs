using System;
using System.Linq;
using Asharia.Editor.Commands;
using Asharia.Editor.Diagnostics;
using Asharia.Editor.Panels;
using Xunit;

namespace Asharia.Editor.Tests.PublicApi;

public sealed class PublicPrerequisiteContractTests
{
    [Fact]
    public void Diagnostic_severity_is_owned_by_public_editor_api()
    {
        Assert.Equal("Asharia.Editor", typeof(EditorDiagnosticSeverity).Assembly.GetName().Name);
        Assert.Equal(
            ["Debug", "Info", "Warning", "Error"],
            Enum.GetNames<EditorDiagnosticSeverity>());
        Assert.Equal([0, 1, 2, 3], Enum.GetValues<EditorDiagnosticSeverity>().Select(value => Convert.ToInt32(value)));
    }

    [Theory]
    [InlineData(EditorCommandExecutionStatus.Succeeded, true)]
    [InlineData(EditorCommandExecutionStatus.NotFound, false)]
    [InlineData(EditorCommandExecutionStatus.Disabled, false)]
    [InlineData(EditorCommandExecutionStatus.Failed, false)]
    public void Command_result_preserves_status_semantics(
        EditorCommandExecutionStatus status,
        bool succeeded)
    {
        var result = new EditorCommandExecutionResult(status, "workbench.test", "message");

        Assert.Equal(succeeded, result.Succeeded);
        Assert.Equal("workbench.test", result.CommandId);
    }

    [Fact]
    public void Command_result_factories_preserve_messages()
    {
        Assert.True(EditorCommandExecutionResult.Success("workbench.test").Succeeded);
        Assert.Equal(
            "Command 'missing.command' is not registered.",
            EditorCommandExecutionResult.NotFound("missing.command").Message);
        Assert.Equal(
            "Disabled by test",
            EditorCommandExecutionResult.Disabled("workbench.test", "Disabled by test").Message);
        Assert.Equal(
            "Failed by test",
            EditorCommandExecutionResult.Failed("workbench.test", "Failed by test").Message);
    }

    [Fact]
    public void Panel_frame_request_validates_rate_and_mode()
    {
        Assert.Same(EditorPanelFrameUpdateRequest.Manual, EditorPanelFrameUpdateRequest.Manual);
        Assert.Equal(30d, EditorPanelFrameUpdateRequest.Active(30d).TargetFramesPerSecond);
        Assert.Equal(10d, EditorPanelFrameUpdateRequest.Visible(10d).TargetFramesPerSecond);
        Assert.Throws<ArgumentOutOfRangeException>(() => EditorPanelFrameUpdateRequest.Visible(0d));
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new EditorPanelFrameUpdateRequest((EditorPanelFrameUpdateMode)42));
    }

    [Fact]
    public void Panel_frame_context_preserves_host_state_and_repaint_request()
    {
        var panel = new EditorPanelLifecycleContext(
            "render.frame-debugger",
            "Frame Debugger",
            EditorDockArea.Bottom,
            IsFloatingWorkspace: false);
        var context = new EditorPanelFrameContext(
            panel,
            DateTimeOffset.UnixEpoch,
            TimeSpan.FromMilliseconds(16),
            sequence: 7);

        context.RequestRepaint();

        Assert.Equal(EditorDockArea.Bottom, panel.DockArea);
        Assert.Equal(
            typeof(EditorDockArea),
            typeof(EditorPanelLifecycleContext).GetProperty(nameof(EditorPanelLifecycleContext.DockArea))?.PropertyType);
        Assert.True(panel.IsMainWorkspace);
        Assert.True(context.IsRepaintRequested);
        Assert.Equal(7, context.Sequence);
    }

    [Fact]
    public void Public_contract_enum_values_are_stable()
    {
        Assert.Equal([0, 1, 2, 3], Enum.GetValues<EditorDockArea>().Select(value => Convert.ToInt32(value)));
        Assert.Equal([0, 1, 2, 3], Enum.GetValues<EditorCommandExecutionStatus>().Select(value => Convert.ToInt32(value)));
        Assert.Equal([0, 1, 2], Enum.GetValues<EditorPanelFrameUpdateMode>().Select(value => Convert.ToInt32(value)));
    }
}
