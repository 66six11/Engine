using System;
using System.Collections.Generic;
using Asharia.Editor.UI.CodeFirst.Models;

namespace Asharia.Editor.UI.CodeFirst.Events;

public sealed class GuiEventQueue
{
    private readonly List<GuiNodeId> buttonClicks_ = [];

    public void EnqueueButtonClicked(GuiNodeId nodeId)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        if (nodeId.Kind != GuiNodeKind.Button)
        {
            throw new ArgumentException("Button click events must target button nodes.", nameof(nodeId));
        }

        buttonClicks_.Add(nodeId);
    }

    public bool ConsumeButtonClicked(GuiNodeId nodeId)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        if (nodeId.Kind != GuiNodeKind.Button)
        {
            return false;
        }

        var eventIndex = buttonClicks_.FindIndex(candidate => candidate == nodeId);
        if (eventIndex < 0)
        {
            return false;
        }

        buttonClicks_.RemoveAt(eventIndex);
        return true;
    }
}
