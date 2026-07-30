using System;
using System.Threading.Tasks;

namespace Editor.Features.SceneView.Interop;

internal readonly record struct SceneViewResourceRetirementResult(
    bool Released,
    Exception? Failure);

internal static class SceneViewResourceRetirement
{
    public static async Task<SceneViewResourceRetirementResult> RunAsync(
        Func<Task> releaseConsumer,
        Func<Task> releaseProducer)
    {
        ArgumentNullException.ThrowIfNull(releaseConsumer);
        ArgumentNullException.ThrowIfNull(releaseProducer);

        try
        {
            await releaseConsumer();
        }
        catch (Exception ex)
        {
            return new SceneViewResourceRetirementResult(
                Released: false,
                ex);
        }

        try
        {
            await releaseProducer();
        }
        catch (Exception ex)
        {
            return new SceneViewResourceRetirementResult(
                Released: false,
                ex);
        }

        return new SceneViewResourceRetirementResult(
            Released: true,
            Failure: null);
    }
}
