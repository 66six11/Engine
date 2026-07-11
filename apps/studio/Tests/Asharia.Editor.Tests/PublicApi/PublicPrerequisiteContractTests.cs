using System;
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
}
