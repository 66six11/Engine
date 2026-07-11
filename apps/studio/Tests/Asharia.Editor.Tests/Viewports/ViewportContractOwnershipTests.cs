using System;
using System.Linq;
using Asharia.Editor.Viewports;
using Xunit;

namespace Asharia.Editor.Tests.Viewports;

public sealed class ViewportContractOwnershipTests
{
    [Fact]
    public void Viewport_contracts_are_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(ViewportClockMode),
            typeof(ViewportClockSnapshot),
            typeof(ViewportExtent),
            typeof(ViewportId),
            typeof(ViewportKind),
            typeof(ViewportRenderReason),
            typeof(ViewportRenderRequest),
            typeof(ViewportRenderResult),
            typeof(ViewportSchedulerContext),
            typeof(ViewportSchedulerOptions),
            typeof(ViewportStateSnapshot),
            typeof(ViewportUpdatePolicy),
        };

        Assert.All(types, type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));
        Assert.All(types, type => Assert.Equal("Asharia.Editor.Viewports", type.Namespace));
    }

    [Fact]
    public void Public_viewport_api_excludes_native_composition_types()
    {
        var exportedNames = typeof(ViewportId).Assembly
            .GetExportedTypes()
            .Where(type => type.Namespace == "Asharia.Editor.Viewports")
            .Select(type => type.Name)
            .ToArray();

        Assert.DoesNotContain(exportedNames, name => name.Contains("Native", StringComparison.Ordinal));
        Assert.DoesNotContain(exportedNames, name => name.Contains("Composition", StringComparison.Ordinal));
        Assert.DoesNotContain(exportedNames, name => name.Contains("Handle", StringComparison.Ordinal));
    }
}
