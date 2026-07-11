using System;
using System.Linq;
using Asharia.Editor.Diagnostics;
using Xunit;

namespace Asharia.Editor.Tests.Diagnostics;

public sealed class EditorDiagnosticContractTests
{
    [Fact]
    public void Diagnostic_contracts_are_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(EditorDiagnosticSeverity),
            typeof(EditorDiagnosticChannel),
            typeof(EditorDiagnosticRecord),
            typeof(IEditorDiagnosticService),
        };

        Assert.All(types, type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));
        Assert.All(types, type => Assert.Equal("Asharia.Editor.Diagnostics", type.Namespace));
    }

    [Fact]
    public void Diagnostic_channel_values_are_stable()
    {
        Assert.Equal(
            [0, 1],
            Enum.GetValues<EditorDiagnosticChannel>().Select(value => Convert.ToInt32(value)));
    }
}
