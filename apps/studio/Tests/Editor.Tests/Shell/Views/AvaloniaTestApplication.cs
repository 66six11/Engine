using System.Runtime.CompilerServices;
using Avalonia;

namespace Editor.Tests.Shell.Views;

internal sealed class AvaloniaTestApplication : Application;

internal static class AvaloniaTestApplicationBootstrap
{
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
}
