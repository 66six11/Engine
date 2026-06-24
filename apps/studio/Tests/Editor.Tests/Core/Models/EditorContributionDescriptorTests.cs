using Editor.Core.Models;
using Xunit;

namespace Editor.Tests.Core.Models;

public sealed class EditorContributionDescriptorTests
{
    [Fact]
    public void Source_id_is_data_only_and_preserves_imported_value()
    {
        var sourceId = new EditorContributionSourceId("project.editor");

        Assert.Equal("project.editor", sourceId.Value);
        Assert.Equal("project.editor", sourceId.ToString());
    }

    [Fact]
    public void Source_id_allows_invalid_imported_value_for_validator_reporting()
    {
        var sourceId = new EditorContributionSourceId(" ");

        Assert.Equal(" ", sourceId.Value);
    }

    [Fact]
    public void Source_kind_defines_supported_descriptor_origins()
    {
        Assert.Equal(0, (int)EditorContributionSourceKind.BuiltIn);
        Assert.Equal(1, (int)EditorContributionSourceKind.ProjectEditor);
        Assert.Equal(2, (int)EditorContributionSourceKind.PackagedPlugin);
        Assert.Equal(3, (int)EditorContributionSourceKind.NativeAdapter);
    }
}
