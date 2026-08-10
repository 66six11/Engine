using System;
using Asharia.Studio.Presentation.Avalonia.Windowing;
using Asharia.Studio.Presentation.Avalonia.Windows.Windowing;
using Avalonia;

namespace Asharia.Studio.Presentation.Avalonia.Windows;

public static class Win32StudioPlatform
{
    public static IInteractiveTopLevelResizeAdapterFactory
        InteractiveTopLevelResizeAdapterFactory =>
            Win32InteractiveTopLevelResizeAdapterFactory.Instance;

    public static AppBuilder Configure(AppBuilder builder)
    {
        ArgumentNullException.ThrowIfNull(builder);
        return builder.With(new Win32PlatformOptions
        {
            RenderingMode =
            [
                Win32RenderingMode.Vulkan,
                Win32RenderingMode.AngleEgl,
                Win32RenderingMode.Software,
            ],
        });
    }
}
