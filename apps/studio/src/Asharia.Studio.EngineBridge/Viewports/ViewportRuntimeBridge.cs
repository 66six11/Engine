using System;
using Asharia.Studio.EngineBridge.Viewports.Abi;

namespace Asharia.Studio.EngineBridge.Viewports;

public sealed class ViewportRuntimeBridge
{
    private readonly IViewportNativeApi nativeApi_;

    public ViewportRuntimeBridge()
        : this(ViewportNativeLibraryApi.Instance)
    {
    }

    internal ViewportRuntimeBridge(IViewportNativeApi nativeApi)
    {
        ArgumentNullException.ThrowIfNull(nativeApi);
        nativeApi_ = nativeApi;
    }

    public void Shutdown()
    {
        try
        {
            nativeApi_.Shutdown();
        }
        catch (Exception exception) when (
            exception is DllNotFoundException or
                EntryPointNotFoundException or
                BadImageFormatException)
        {
            // No runtime could have been admitted when its binding was unavailable.
        }
    }
}
