using System;
using Asharia.Studio.Presentation.Avalonia.Windowing;
using Avalonia;
#if ASHARIA_STUDIO_WINDOWS
using Asharia.Studio.Presentation.Avalonia.Windows;
#endif

namespace Editor.Shell.Composition;

internal static class StudioPlatformComposition
{
    internal static IInteractiveTopLevelResizeAdapterFactory?
        CreateInteractiveTopLevelResizeAdapterFactory()
    {
#if ASHARIA_STUDIO_WINDOWS
        return Win32StudioPlatform.InteractiveTopLevelResizeAdapterFactory;
#else
        return null;
#endif
    }

    internal static AppBuilder ConfigurePlatform(AppBuilder builder)
    {
        ArgumentNullException.ThrowIfNull(builder);
#if ASHARIA_STUDIO_WINDOWS
        return Win32StudioPlatform.Configure(builder);
#else
        return builder;
#endif
    }
}
