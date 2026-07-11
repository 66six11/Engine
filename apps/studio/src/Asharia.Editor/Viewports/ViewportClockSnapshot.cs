using System;

namespace Asharia.Editor.Viewports;

public sealed record ViewportClockSnapshot
{
    public ViewportClockSnapshot(
        ViewportClockMode mode,
        double timeSeconds,
        double deltaSeconds,
        ulong frameIndex,
        double playbackSpeed)
    {
        if (!Enum.IsDefined(mode))
        {
            throw new ArgumentOutOfRangeException(
                nameof(mode),
                mode,
                "Viewport clock mode is not defined.");
        }

        if (timeSeconds < 0 || !double.IsFinite(timeSeconds))
        {
            throw new ArgumentOutOfRangeException(
                nameof(timeSeconds),
                timeSeconds,
                "Viewport clock time must be finite and greater than or equal to zero.");
        }

        if (deltaSeconds < 0 || !double.IsFinite(deltaSeconds))
        {
            throw new ArgumentOutOfRangeException(
                nameof(deltaSeconds),
                deltaSeconds,
                "Viewport clock delta must be finite and greater than or equal to zero.");
        }

        if (playbackSpeed <= 0 || !double.IsFinite(playbackSpeed))
        {
            throw new ArgumentOutOfRangeException(
                nameof(playbackSpeed),
                playbackSpeed,
                "Viewport clock playback speed must be finite and greater than zero.");
        }

        Mode = mode;
        TimeSeconds = timeSeconds;
        DeltaSeconds = deltaSeconds;
        FrameIndex = frameIndex;
        PlaybackSpeed = playbackSpeed;
    }

    public ViewportClockMode Mode { get; }

    public double TimeSeconds { get; }

    public double DeltaSeconds { get; }

    public ulong FrameIndex { get; }

    public double PlaybackSpeed { get; }

    public ViewportClockSnapshot Advance(TimeSpan realElapsed)
    {
        if (realElapsed < TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(
                nameof(realElapsed),
                realElapsed,
                "Viewport clock elapsed time must be greater than or equal to zero.");
        }

        if (Mode is ViewportClockMode.FrozenTime
            or ViewportClockMode.ManualStepTime
            or ViewportClockMode.CapturedFrameTime)
        {
            return new ViewportClockSnapshot(
                Mode,
                TimeSeconds,
                deltaSeconds: 0,
                FrameIndex,
                PlaybackSpeed);
        }

        var deltaSeconds = realElapsed.TotalSeconds * PlaybackSpeed;
        return new ViewportClockSnapshot(
            Mode,
            TimeSeconds + deltaSeconds,
            deltaSeconds,
            FrameIndex + 1,
            PlaybackSpeed);
    }
}
