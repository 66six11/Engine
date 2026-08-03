using Avalonia;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Editor;

[assembly: AvaloniaTestApplication(typeof(Asharia.Studio.Headless.Tests.TestAppBuilder))]
[assembly: AvaloniaTestIsolation(AvaloniaTestIsolationLevel.PerTest)]

namespace Asharia.Studio.Headless.Tests;

public static class TestAppBuilder
{
    public static AppBuilder BuildAvaloniaApp()
    {
        return AppBuilder.Configure<App>()
            .UseHeadless(new AvaloniaHeadlessPlatformOptions());
    }
}
