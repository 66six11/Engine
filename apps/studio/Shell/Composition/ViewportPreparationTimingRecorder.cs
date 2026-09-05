using System;
using System.Collections.Generic;
using System.Text.Json;
using Asharia.Studio.Presentation.Avalonia.Viewports;

namespace Editor.Shell.Composition;

internal sealed class ViewportPreparationTimingRecorder
{
    private readonly object gate_ = new();
    private readonly List<ViewportPreparationTiming> events_ = new(2048);
    private int dropped_;

    public void Record(ViewportPreparationTiming timing)
    {
        lock (gate_)
        {
            if (events_.Count < 2048) events_.Add(timing);
            else dropped_++;
        }
    }

    public void Write()
    {
        lock (gate_)
        {
            Console.Out.WriteLine("viewport-preparation-timing " +
                JsonSerializer.Serialize(new { events = events_, dropped = dropped_ }));
        }
    }
}
