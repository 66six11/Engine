using System;
using Avalonia.Threading;

namespace Editor.Shell.CodeFirstUI.Adapters;

internal sealed class DispatcherGuiTextCommitScheduler : IGuiTextCommitScheduler
{
    public static DispatcherGuiTextCommitScheduler Instance { get; } = new();

    public IDisposable Schedule(TimeSpan delay, Action action)
    {
        ArgumentNullException.ThrowIfNull(action);

        var timer = new DispatcherTimer
        {
            Interval = delay,
        };
        EventHandler? tick = null;
        tick = (_, _) =>
        {
            timer.Stop();
            timer.Tick -= tick;
            action();
        };
        timer.Tick += tick;
        timer.Start();
        return new TimerLease(timer, tick);
    }

    private sealed class TimerLease(
        DispatcherTimer timer,
        EventHandler tick) : IDisposable
    {
        private bool isDisposed_;

        public void Dispose()
        {
            if (isDisposed_)
            {
                return;
            }

            timer.Stop();
            timer.Tick -= tick;
            isDisposed_ = true;
        }
    }
}
