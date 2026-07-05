using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportSchedulerOptions
{
    private static readonly double MaxSafelyConvertibleTicks =
        Math.BitDecrement(Math.Pow(2, 63));

    public ViewportSchedulerOptions(
        double interactiveBurstFramesPerSecond = 60,
        double sceneIdleFramesPerSecond = 5,
        double previewFramesPerSecond = 15,
        double runtimeFramesPerSecond = 60)
    {
        ValidateFrameRate(
            interactiveBurstFramesPerSecond,
            nameof(interactiveBurstFramesPerSecond));
        ValidateFrameRate(
            sceneIdleFramesPerSecond,
            nameof(sceneIdleFramesPerSecond));
        ValidateFrameRate(
            previewFramesPerSecond,
            nameof(previewFramesPerSecond));
        ValidateFrameRate(
            runtimeFramesPerSecond,
            nameof(runtimeFramesPerSecond));

        InteractiveBurstFramesPerSecond = interactiveBurstFramesPerSecond;
        SceneIdleFramesPerSecond = sceneIdleFramesPerSecond;
        PreviewFramesPerSecond = previewFramesPerSecond;
        RuntimeFramesPerSecond = runtimeFramesPerSecond;
    }

    public static ViewportSchedulerOptions Default { get; } = new();

    public double InteractiveBurstFramesPerSecond { get; }

    public double SceneIdleFramesPerSecond { get; }

    public double PreviewFramesPerSecond { get; }

    public double RuntimeFramesPerSecond { get; }

    public TimeSpan InteractiveBurstInterval =>
        GetTargetFrameInterval(
            InteractiveBurstFramesPerSecond,
            nameof(InteractiveBurstFramesPerSecond));

    public TimeSpan SceneIdleInterval =>
        GetTargetFrameInterval(
            SceneIdleFramesPerSecond,
            nameof(SceneIdleFramesPerSecond));

    public TimeSpan PreviewInterval =>
        GetTargetFrameInterval(
            PreviewFramesPerSecond,
            nameof(PreviewFramesPerSecond));

    public TimeSpan RuntimeInterval =>
        GetTargetFrameInterval(
            RuntimeFramesPerSecond,
            nameof(RuntimeFramesPerSecond));

    private static void ValidateFrameRate(
        double framesPerSecond,
        string parameterName)
    {
        _ = GetTargetFrameIntervalTicks(framesPerSecond, parameterName);
    }

    private static TimeSpan GetTargetFrameInterval(
        double framesPerSecond,
        string parameterName)
    {
        return TimeSpan.FromTicks(
            GetTargetFrameIntervalTicks(framesPerSecond, parameterName));
    }

    private static long GetTargetFrameIntervalTicks(
        double framesPerSecond,
        string parameterName)
    {
        if (framesPerSecond <= 0 || !double.IsFinite(framesPerSecond))
        {
            throw CreateFrameRateOutOfRangeException(
                framesPerSecond,
                parameterName);
        }

        var intervalTicks = Math.Ceiling(
            TimeSpan.TicksPerSecond / framesPerSecond);

        if (!double.IsFinite(intervalTicks)
            || intervalTicks < 1
            || intervalTicks > MaxSafelyConvertibleTicks)
        {
            throw CreateFrameRateOutOfRangeException(
                framesPerSecond,
                parameterName);
        }

        return checked((long)intervalTicks);
    }

    private static ArgumentOutOfRangeException CreateFrameRateOutOfRangeException(
        double framesPerSecond,
        string parameterName)
    {
        return new ArgumentOutOfRangeException(
            parameterName,
            framesPerSecond,
            "Viewport scheduler frame rate must produce a representable positive interval.");
    }
}
