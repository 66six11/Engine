using System.Runtime.CompilerServices;
using Avalonia;

namespace Editor.Tests.Shell.Views;

internal sealed class AvaloniaTestApplication : Application;

internal static class AvaloniaTestApplicationBootstrap
{
#pragma warning disable CA2255 // Test-wide Avalonia setup must run before any view fixture.
    [ModuleInitializer]
    internal static void Initialize()
    {
        if (Application.Current is null)
        {
            AppBuilder.Configure<AvaloniaTestApplication>()
                .UsePlatformDetect()
                .SetupWithoutStarting();
        }
    }
#pragma warning restore CA2255
}
