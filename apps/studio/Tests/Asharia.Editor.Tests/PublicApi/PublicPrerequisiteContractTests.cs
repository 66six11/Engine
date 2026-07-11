using System;
using Asharia.Editor.Commands;
using Asharia.Editor.Diagnostics;
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
}
