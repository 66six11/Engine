using System;
using System.Linq;
using Asharia.Editor.Lifecycle;
using Xunit;

namespace Asharia.Editor.Tests.Lifecycle;

public sealed class EditorLifecycleContractTests
{
    [Fact]
    public void Lifecycle_contracts_are_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(EditorLifecycleEventKind),
            typeof(EditorLifecycleEventSnapshot),
            typeof(IEditorLifecycleEventService),
        };

        Assert.All(types, type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));
        Assert.All(types, type => Assert.Equal("Asharia.Editor.Lifecycle", type.Namespace));
    }

    [Fact]
    public void Lifecycle_event_values_are_stable()
    {
        Assert.Equal(
            Enumerable.Range(0, 10),
            Enum.GetValues<EditorLifecycleEventKind>().Select(value => Convert.ToInt32(value)));
    }
}
