using Asharia.Editor.UI.CodeFirst.Events;
using Asharia.Editor.UI.CodeFirst.Models;
using Xunit;

namespace Asharia.Editor.Tests.UI.CodeFirst.Events;

public sealed class GuiEventQueueTests
{
    [Fact]
    public void Button_click_event_is_consumed_once_for_matching_node()
    {
        var queue = new GuiEventQueue();
        var capture = new GuiNodeId(
            "render.frameDebugger",
            "toolbar/capture",
            GuiNodeKind.Button);

        queue.EnqueueButtonClicked(capture);

        Assert.True(queue.ConsumeButtonClicked(capture));
        Assert.False(queue.ConsumeButtonClicked(capture));
    }

    [Fact]
    public void Button_click_event_does_not_match_different_key_or_kind()
    {
        var queue = new GuiEventQueue();
        var capture = new GuiNodeId(
            "render.frameDebugger",
            "toolbar/capture",
            GuiNodeKind.Button);
        var textFieldWithSameKey = capture with { Kind = GuiNodeKind.TextField };
        var otherButton = capture with { KeyPath = "footer/capture" };

        queue.EnqueueButtonClicked(capture);

        Assert.False(queue.ConsumeButtonClicked(textFieldWithSameKey));
        Assert.False(queue.ConsumeButtonClicked(otherButton));
        Assert.True(queue.ConsumeButtonClicked(capture));
    }
}
