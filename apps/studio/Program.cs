using Avalonia;
using System;
using Editor.Shell.Composition;
namespace Editor;

sealed class Program
{
    // Initialization code. Don't use any Avalonia, third-party APIs or any
    // SynchronizationContext-reliant code before AppMain is called: things aren't initialized
    // yet and stuff might break.
    [STAThread]
    public static int Main(string[] args) => BuildAvaloniaApp()
        .StartWithClassicDesktopLifetime(args);

    // Avalonia configuration, don't remove; also used by visual designer.
    public static AppBuilder BuildAvaloniaApp()
    {
        return StudioPlatformComposition.ConfigurePlatform(
                AppBuilder.Configure<App>()
                    .UsePlatformDetect())
            .WithInterFont();
    }
}
