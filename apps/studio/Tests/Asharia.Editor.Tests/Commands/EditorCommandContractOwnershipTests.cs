using System;
using Asharia.Editor.Commands;
using Xunit;

namespace Asharia.Editor.Tests.Commands;

public sealed class EditorCommandContractOwnershipTests
{
    [Fact]
    public void Status_message_contracts_are_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(EditorStatusMessageSeverity),
            typeof(EditorStatusMessageSnapshot),
            typeof(EditorStatusMessageSource),
        };

        Assert.All(types, type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));
        Assert.All(types, type => Assert.Equal("Asharia.Editor.Commands", type.Namespace));
    }
}
