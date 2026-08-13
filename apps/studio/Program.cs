using Avalonia;
using System;
using System.IO;
using Asharia.Studio.EngineBridge.Viewports.Abi;
using Editor.Shell.Composition;
namespace Editor;

sealed class Program
{
    internal const string VerifyNativeContractSwitch = "--verify-native-contract";

    // Initialization code. Don't use any Avalonia, third-party APIs or any
    // SynchronizationContext-reliant code before AppMain is called: things aren't initialized
    // yet and stuff might break.
    [STAThread]
    public static int Main(string[] args)
    {
        if (args.Length == 1 &&
            string.Equals(args[0], VerifyNativeContractSwitch, StringComparison.Ordinal))
        {
            return VerifyNativeContract();
        }

        return BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    private static int VerifyNativeContract()
    {
        var result = ViewportNativeRuntimeContract.Inspect(Path.Combine(
            AppContext.BaseDirectory,
            "editor_native.dll"));
        if (result.Succeeded)
        {
            return 0;
        }

        Console.Error.WriteLine(result.Diagnostic);
        return 3;
    }

    // Avalonia configuration, don't remove; also used by visual designer.
    public static AppBuilder BuildAvaloniaApp()
    {
        return StudioPlatformComposition.ConfigurePlatform(
                AppBuilder.Configure<App>()
                    .UsePlatformDetect())
            .WithInterFont();
    }
}
