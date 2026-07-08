using System;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Editor.Core.Interop.Viewports.Api;
using Editor.Shell.Composition;
using Editor.Shell.Views.Windowing;

namespace Editor;

// ReSharper disable once PartialTypeWithSinglePart
public partial class App : Application
{
    private StudioCompositionSession? compositionSession_;

    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            compositionSession_ = new StudioCompositionRoot().CreateMainWindowSession();
            desktop.Exit += OnDesktopExit;
            desktop.MainWindow = new MainWindow
            {
                DataContext = compositionSession_.MainWindowViewModel,
            };
        }

        base.OnFrameworkInitializationCompleted();
    }

    private void OnDesktopExit(object? sender, ControlledApplicationLifetimeExitEventArgs e)
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.Exit -= OnDesktopExit;
        }

        var session = compositionSession_;
        compositionSession_ = null;
        session?.DisposeAsync().AsTask().GetAwaiter().GetResult();
        ShutdownNativeViewportRuntime();
    }

    private static void ShutdownNativeViewportRuntime()
    {
        try
        {
            ViewportNativeLibraryApi.Instance.Shutdown();
        }
        catch (Exception ex) when (IsNativeBindingException(ex))
        {
        }
    }

    private static bool IsNativeBindingException(Exception ex)
    {
        return ex is DllNotFoundException
            or EntryPointNotFoundException
            or BadImageFormatException;
    }
}
