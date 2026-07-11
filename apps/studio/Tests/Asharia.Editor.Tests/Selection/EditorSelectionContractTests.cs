using System;
using Asharia.Editor.Selection;
using Xunit;

namespace Asharia.Editor.Tests.Selection;

public sealed class EditorSelectionContractTests
{
    [Fact]
    public void Selection_contracts_are_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(EditorSelectionChangedEventArgs),
            typeof(EditorSelectionItem),
            typeof(EditorSelectionSnapshot),
            typeof(IEditorSelectionService),
        };

        Assert.All(types, type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));
        Assert.All(types, type => Assert.Equal("Asharia.Editor.Selection", type.Namespace));
    }

    [Fact]
    public void Empty_selection_has_no_primary_item()
    {
        Assert.False(EditorSelectionSnapshot.Empty.HasSelection);
        Assert.Null(EditorSelectionSnapshot.Empty.PrimaryItem);
    }
}
