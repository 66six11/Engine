using System;
using System.Linq;
using Asharia.Editor.Tasks;
using Xunit;

namespace Asharia.Editor.Tests.Tasks;

public sealed class EditorBackgroundTaskContractTests
{
    [Fact]
    public void Background_task_contracts_are_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(EditorBackgroundTaskId),
            typeof(EditorBackgroundTaskSnapshot),
            typeof(EditorBackgroundTaskState),
            typeof(IEditorBackgroundTaskService),
        };

        Assert.All(types, type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));
        Assert.All(types, type => Assert.Equal("Asharia.Editor.Tasks", type.Namespace));
    }

    [Fact]
    public void New_task_ids_are_non_default_and_unique()
    {
        var first = EditorBackgroundTaskId.NewId();
        var second = EditorBackgroundTaskId.NewId();

        Assert.NotEqual(default, first);
        Assert.NotEqual(first, second);
    }

    [Fact]
    public void Background_task_state_values_are_stable()
    {
        Assert.Equal(
            [0, 1, 2, 3],
            Enum.GetValues<EditorBackgroundTaskState>().Select(value => Convert.ToInt32(value)));
    }
}
