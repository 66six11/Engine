using System;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls.ApplicationLifetimes;

namespace Editor.Shell.Composition;

internal static class StudioViewportTransactionSmoke
{
    public static bool IsRequested(string[] arguments) =>
        arguments.Any(static argument =>
            string.Equals(
                argument,
                StudioViewportTransactionResizeSmoke.CommandLineSwitch,
                StringComparison.Ordinal) ||
            string.Equals(
                argument,
                StudioViewportTransactionOverloadSmoke.CommandLineSwitch,
                StringComparison.Ordinal) ||
            string.Equals(
                argument,
                StudioViewportTransactionSupersedeSmoke.CommandLineSwitch,
                StringComparison.Ordinal) ||
            string.Equals(
                argument,
                StudioViewportMultiEndpointSmoke.CommandLineSwitch,
                StringComparison.Ordinal) ||
            string.Equals(
                argument,
                StudioViewportTransactionFaultSmoke.CommandLineSwitch,
                StringComparison.Ordinal) ||
            string.Equals(
                argument,
                StudioViewportTransactionFlashSmoke.CommandLineSwitch,
                StringComparison.Ordinal) ||
            string.Equals(
                argument,
                StudioViewportTransactionWindowResizeSmoke.CommandLineSwitch,
                StringComparison.Ordinal) ||
            string.Equals(
                argument,
                StudioSceneMeshSmoke.CommandLineSwitch,
                StringComparison.Ordinal));

    public static Task RunAsync(
        IClassicDesktopStyleApplicationLifetime desktop,
        string[] arguments)
    {
        ArgumentNullException.ThrowIfNull(desktop);
        ArgumentNullException.ThrowIfNull(arguments);
        if (arguments.Contains(
                StudioViewportTransactionResizeSmoke.CommandLineSwitch,
                StringComparer.Ordinal))
        {
            return StudioViewportTransactionResizeSmoke.RunAsync(desktop, arguments);
        }
        if (arguments.Contains(
                StudioViewportTransactionOverloadSmoke.CommandLineSwitch,
                StringComparer.Ordinal))
        {
            return StudioViewportTransactionOverloadSmoke.RunAsync(desktop, arguments);
        }
        if (arguments.Contains(
                StudioViewportTransactionSupersedeSmoke.CommandLineSwitch,
                StringComparer.Ordinal))
        {
            return StudioViewportTransactionSupersedeSmoke.RunAsync(desktop, arguments);
        }
        if (arguments.Contains(
                StudioViewportMultiEndpointSmoke.CommandLineSwitch,
                StringComparer.Ordinal))
        {
            return StudioViewportMultiEndpointSmoke.RunAsync(desktop, arguments);
        }
        if (arguments.Contains(
                StudioViewportTransactionFaultSmoke.CommandLineSwitch,
                StringComparer.Ordinal))
        {
            return StudioViewportTransactionFaultSmoke.RunAsync(desktop, arguments);
        }
        if (arguments.Contains(
                StudioViewportTransactionFlashSmoke.CommandLineSwitch,
                StringComparer.Ordinal))
        {
            return StudioViewportTransactionFlashSmoke.RunAsync(desktop, arguments);
        }
        if (arguments.Contains(
                StudioViewportTransactionWindowResizeSmoke.CommandLineSwitch,
                StringComparer.Ordinal))
        {
            return StudioViewportTransactionWindowResizeSmoke.RunAsync(desktop, arguments);
        }
        if (arguments.Contains(
                StudioSceneMeshSmoke.CommandLineSwitch,
                StringComparer.Ordinal))
        {
            return StudioSceneMeshSmoke.RunAsync(desktop);
        }

        throw new InvalidOperationException(
            "No viewport presentation transaction smoke was selected.");
    }
}
