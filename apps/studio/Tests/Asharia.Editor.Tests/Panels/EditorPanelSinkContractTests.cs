using System;
using Asharia.Editor.Panels;
using Xunit;

namespace Asharia.Editor.Tests.Panels;

public sealed class EditorPanelSinkContractTests
{
    [Fact]
    public void Panel_sink_contracts_are_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(IEditorPanelFrameUpdateSink),
            typeof(IEditorPanelLifecycleSink),
        };

        Assert.All(types, type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));
        Assert.All(types, type => Assert.Equal("Asharia.Editor.Panels", type.Namespace));
    }
}
