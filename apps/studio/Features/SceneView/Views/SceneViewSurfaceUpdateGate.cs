using System;
using System.Threading;
using System.Threading.Tasks;

namespace Editor.Features.SceneView.Views;

internal sealed class SceneViewSurfaceUpdateGate
{
    private readonly SemaphoreSlim gate_ = new(initialCount: 1, maxCount: 1);

    public async Task<bool> RunAsync(
        Func<bool> canPresent,
        Func<Task<bool>> updateSurface,
        Func<bool> tryAcceptPresentation)
    {
        ArgumentNullException.ThrowIfNull(canPresent);
        ArgumentNullException.ThrowIfNull(updateSurface);
        ArgumentNullException.ThrowIfNull(tryAcceptPresentation);

        await gate_.WaitAsync();
        try
        {
            if (!canPresent() || !await updateSurface())
            {
                return false;
            }

            return canPresent() && tryAcceptPresentation();
        }
        finally
        {
            gate_.Release();
        }
    }
}
