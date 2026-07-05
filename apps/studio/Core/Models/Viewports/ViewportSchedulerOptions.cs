using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportSchedulerOptions
{
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
        GetTargetFrameInterval(InteractiveBurstFramesPerSecond);

    public TimeSpan SceneIdleInterval =>
        GetTargetFrameInterval(SceneIdleFramesPerSecond);

    public TimeSpan PreviewInterval =>
        GetTargetFrameInterval(PreviewFramesPerSecond);

    public TimeSpan RuntimeInterval =>
        GetTargetFrameInterval(RuntimeFramesPerSecond);

    private static void ValidateFrameRate(
        double framesPerSecond,
        string parameterName)
    {
        if (framesPerSecond <= 0 || !double.IsFinite(framesPerSecond))
        {
            throw new ArgumentOutOfRangeException(
                parameterName,
                framesPerSecond,
                "Viewport scheduler frame rate must be finite and greater than zero.");
        }
    }

    private static TimeSpan GetTargetFrameInterval(double framesPerSecond)
    {
        return TimeSpan.FromTicks(
            (long)Math.Ceiling(TimeSpan.TicksPerSecond / framesPerSecond));
    }
}
