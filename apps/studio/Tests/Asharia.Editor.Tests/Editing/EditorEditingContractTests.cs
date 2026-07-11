using System;
using System.Linq;
using Asharia.Editor.Editing;
using Xunit;

namespace Asharia.Editor.Tests.Editing;

public sealed class EditorEditingContractTests
{
    [Fact]
    public void Editing_contracts_are_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(EditorEditCommandDescriptor),
            typeof(EditorEditMergePolicy),
            typeof(EditorEditValidationResult),
            typeof(IEditorEditCommand),
        };

        Assert.All(types, type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));
        Assert.All(types, type => Assert.Equal("Asharia.Editor.Editing", type.Namespace));
    }

    [Fact]
    public void Merge_policy_values_are_stable()
    {
        Assert.Equal(
            [0, 1],
            Enum.GetValues<EditorEditMergePolicy>().Select(value => Convert.ToInt32(value)));
    }

    [Fact]
    public void Invalid_result_requires_a_message()
    {
        Assert.Throws<ArgumentException>(() => EditorEditValidationResult.Invalid(" "));
        Assert.Equal("Locked", EditorEditValidationResult.Invalid("Locked").Message);
    }
}
