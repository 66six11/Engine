using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Observe.CommandLine;
using Asharia.Studio.Observe.Mcp;

namespace Asharia.Studio.Observe;

internal static class Program
{
    public static async Task<int> Main(string[] args)
    {
        using var cancellation = new CancellationTokenSource();
        ConsoleCancelEventHandler cancelHandler = (_, eventArgs) =>
        {
            eventArgs.Cancel = true;
            cancellation.Cancel();
        };
        Console.CancelKeyPress += cancelHandler;
        try
        {
            if (args.Length == 1
                && string.Equals(args[0], "mcp", StringComparison.Ordinal))
            {
                if (!OperatingSystem.IsWindows())
                {
                    await Console.Error.WriteLineAsync(
                        "observation.client.unsupported-platform: Studio observation MCP adapter is currently Windows-only.");
                    return (int)StudioObserveExitCode.Unavailable;
                }

                return await StudioMcpServer.RunStandardIoAsync(cancellation.Token);
            }

            return await StudioObserveCommand.RunAsync(
                args,
                Console.Out,
                Console.Error,
                cancellation.Token);
        }
        finally
        {
            Console.CancelKeyPress -= cancelHandler;
        }
    }
}
