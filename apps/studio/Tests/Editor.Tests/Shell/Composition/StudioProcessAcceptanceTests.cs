using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Editor.Shell.Composition;
using Xunit;

namespace Editor.Tests.Shell.Composition;

[CollectionDefinition(kCollectionName, DisableParallelization = true)]
public sealed class StudioProcessAcceptanceCollection
{
    public const string kCollectionName = "Studio process acceptance";
}

[Collection(StudioProcessAcceptanceCollection.kCollectionName)]
public sealed class StudioProcessAcceptanceTests
{
    private static readonly TimeSpan kReadyTimeout = TimeSpan.FromSeconds(15);
    private static readonly TimeSpan kExitTimeout = TimeSpan.FromSeconds(15);
    private static readonly TimeSpan kKillTimeout = TimeSpan.FromSeconds(5);
    private const string GpuAcceptanceEnvironmentVariable =
        "ASHARIA_RUN_STUDIO_GPU_ACCEPTANCE";

    [Fact]
    public async Task Production_editor_clean_close_returns_zero_after_real_managed_teardown()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        await using var child = DisposableEditorProcess.Start(kKillTimeout);
        await child.WaitUntilReadyAsync(kReadyTimeout);

        Assert.True(child.RequestClose());
        var receipt = await child.ObserveExitAsync(kExitTimeout);

        Assert.Equal(ProcessAcceptanceStatus.Exited, receipt.Status);
        Assert.Equal(0, receipt.ExitCode);
        Assert.False(receipt.TerminationRequested);
        Assert.True(receipt.ExitConfirmed);
    }

    [Fact]
    public async Task Production_editor_forced_termination_is_nonzero_and_reaped()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        await using var child = DisposableEditorProcess.Start(kKillTimeout);
        await child.WaitUntilReadyAsync(kReadyTimeout);

        var receipt = await child.ForceTerminateAsync();

        Assert.Equal(ProcessAcceptanceStatus.ForcedTermination, receipt.Status);
        Assert.NotEqual(0, receipt.ExitCode);
        Assert.True(receipt.TerminationRequested);
        Assert.True(receipt.ExitConfirmed);
    }

    [Fact]
    public async Task Production_editor_acceptance_timeout_kills_and_reaps_child()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        await using var child = DisposableEditorProcess.Start(kKillTimeout);
        await child.WaitUntilReadyAsync(kReadyTimeout);

        var receipt = await child.ObserveExitAsync(TimeSpan.FromMilliseconds(100));

        Assert.Equal(ProcessAcceptanceStatus.TimedOut, receipt.Status);
        Assert.NotEqual(0, receipt.ExitCode);
        Assert.True(receipt.TerminationRequested);
        Assert.True(receipt.ExitConfirmed);
    }

    [Fact]
    public async Task Canceling_process_acceptance_does_not_abandon_child()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        await using var child = DisposableEditorProcess.Start(kKillTimeout);
        await child.WaitUntilReadyAsync(kReadyTimeout);
        using var cancellation = new CancellationTokenSource(TimeSpan.FromMilliseconds(100));

        var receipt = await child.ObserveExitAsync(kExitTimeout, cancellation.Token);

        Assert.Equal(ProcessAcceptanceStatus.Canceled, receipt.Status);
        Assert.NotEqual(0, receipt.ExitCode);
        Assert.True(receipt.TerminationRequested);
        Assert.True(receipt.ExitConfirmed);
    }

    [Fact]
    [Trait("Category", "StudioGpuAcceptance")]
    public void Realtime_scene_viewport_and_panel_resize_sustain_at_least_60_fps()
    {
        if (!OperatingSystem.IsWindows() ||
            !string.Equals(
                Environment.GetEnvironmentVariable(GpuAcceptanceEnvironmentVariable),
                "1",
                StringComparison.Ordinal))
        {
            return;
        }

        var executablePath = Path.Combine(AppContext.BaseDirectory, "Editor.exe");
        if (!File.Exists(executablePath))
        {
            throw new FileNotFoundException(
                "The Studio GPU acceptance test requires the built Editor apphost.",
                executablePath);
        }

        using var process = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = executablePath,
                WorkingDirectory = Path.GetDirectoryName(executablePath)!,
                UseShellExecute = false,
                CreateNoWindow = true,
            },
        };
        process.StartInfo.ArgumentList.Add(StudioViewportCadenceSmoke.CommandLineSwitch);
        Assert.True(process.Start());
        if (!process.WaitForExit(milliseconds: 35_000))
        {
            process.Kill(entireProcessTree: true);
            process.WaitForExit(milliseconds: 5_000);
            throw new TimeoutException(
                "The Studio viewport cadence smoke did not finish within 35 seconds.");
        }

        Assert.True(
            process.ExitCode == 0,
            $"Studio viewport cadence smoke exited with {process.ExitCode}.");
    }

    private enum ProcessAcceptanceStatus
    {
        Exited,
        ForcedTermination,
        TimedOut,
        Canceled,
    }

    private sealed record ProcessAcceptanceReceipt(
        ProcessAcceptanceStatus Status,
        int ExitCode,
        bool TerminationRequested,
        bool ExitConfirmed);

    private sealed class DisposableEditorProcess : IAsyncDisposable
    {
        private readonly Process process_;
        private readonly Task standardOutputDrain_;
        private readonly Task standardErrorDrain_;
        private readonly TimeSpan killTimeout_;
        private bool disposed_;

        private DisposableEditorProcess(
            Process process,
            Task standardOutputDrain,
            Task standardErrorDrain,
            TimeSpan killTimeout)
        {
            process_ = process;
            standardOutputDrain_ = standardOutputDrain;
            standardErrorDrain_ = standardErrorDrain;
            killTimeout_ = killTimeout;
        }

        public static DisposableEditorProcess Start(TimeSpan killTimeout)
        {
            var executablePath = Path.Combine(AppContext.BaseDirectory, "Editor.exe");
            if (!File.Exists(executablePath))
            {
                throw new FileNotFoundException(
                    "The process acceptance test requires the built production Editor apphost.",
                    executablePath);
            }

            var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = executablePath,
                    WorkingDirectory = Path.GetDirectoryName(executablePath)!,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    WindowStyle = ProcessWindowStyle.Hidden,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                },
            };

            if (!process.Start())
            {
                process.Dispose();
                throw new InvalidOperationException("The production Editor process did not start.");
            }

            var standardOutputDrain = process.StandardOutput.BaseStream.CopyToAsync(Stream.Null);
            var standardErrorDrain = process.StandardError.BaseStream.CopyToAsync(Stream.Null);
            return new DisposableEditorProcess(
                process,
                standardOutputDrain,
                standardErrorDrain,
                killTimeout);
        }

        public async Task WaitUntilReadyAsync(TimeSpan timeout)
        {
            using var deadline = new CancellationTokenSource(timeout);
            try
            {
                while (true)
                {
                    if (HasExited())
                    {
                        await DrainOutputAsync();
                        throw new InvalidOperationException(
                            $"The production Editor exited before Ready with code {process_.ExitCode}.");
                    }

                    process_.Refresh();
                    if (process_.MainWindowHandle != IntPtr.Zero
                        && process_.MainWindowTitle.StartsWith(
                            "No Document",
                            StringComparison.Ordinal))
                    {
                        return;
                    }

                    await Task.Delay(TimeSpan.FromMilliseconds(25), deadline.Token);
                }
            }
            catch (OperationCanceledException) when (deadline.IsCancellationRequested)
            {
                throw new TimeoutException(
                    $"The production Editor did not publish its Ready window within {timeout}.");
            }
        }

        public bool RequestClose()
        {
            ThrowIfDisposed();
            return process_.CloseMainWindow();
        }

        public async Task<ProcessAcceptanceReceipt> ObserveExitAsync(
            TimeSpan timeout,
            CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            using var deadline = new CancellationTokenSource(timeout);
            using var observation = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken,
                deadline.Token);
            try
            {
                await process_.WaitForExitAsync(observation.Token);
                await DrainOutputAsync();
                return CreateReceipt(
                    ProcessAcceptanceStatus.Exited,
                    terminationRequested: false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return await TerminateAndCreateReceiptAsync(ProcessAcceptanceStatus.Canceled);
            }
            catch (OperationCanceledException) when (deadline.IsCancellationRequested)
            {
                return await TerminateAndCreateReceiptAsync(ProcessAcceptanceStatus.TimedOut);
            }
        }

        public Task<ProcessAcceptanceReceipt> ForceTerminateAsync()
        {
            ThrowIfDisposed();
            return TerminateAndCreateReceiptAsync(ProcessAcceptanceStatus.ForcedTermination);
        }

        public async ValueTask DisposeAsync()
        {
            if (disposed_)
            {
                return;
            }

            disposed_ = true;
            try
            {
                if (!HasExited())
                {
                    await TerminateAndWaitAsync();
                }

                await DrainOutputAsync();
            }
            finally
            {
                process_.Dispose();
            }
        }

        private async Task<ProcessAcceptanceReceipt> TerminateAndCreateReceiptAsync(
            ProcessAcceptanceStatus status)
        {
            var terminationRequested = !HasExited();
            if (terminationRequested)
            {
                await TerminateAndWaitAsync();
            }

            await DrainOutputAsync();
            return CreateReceipt(status, terminationRequested);
        }

        private async Task TerminateAndWaitAsync()
        {
            try
            {
                process_.Kill(entireProcessTree: true);
            }
            catch (InvalidOperationException) when (HasExited())
            {
                return;
            }

            using var killDeadline = new CancellationTokenSource(killTimeout_);
            try
            {
                await process_.WaitForExitAsync(killDeadline.Token);
            }
            catch (OperationCanceledException) when (killDeadline.IsCancellationRequested)
            {
                throw new TimeoutException(
                    $"The production Editor did not exit within {killTimeout_} after Kill.");
            }
        }

        private async Task DrainOutputAsync()
        {
            var drains = Task.WhenAll(standardOutputDrain_, standardErrorDrain_);
            try
            {
                await drains.WaitAsync(killTimeout_);
            }
            catch (TimeoutException)
            {
                throw new TimeoutException(
                    $"The production Editor output did not drain within {killTimeout_}.");
            }
        }

        private ProcessAcceptanceReceipt CreateReceipt(
            ProcessAcceptanceStatus status,
            bool terminationRequested)
        {
            if (!HasExited())
            {
                throw new InvalidOperationException(
                    "A process acceptance receipt cannot be created before exit is confirmed.");
            }

            return new ProcessAcceptanceReceipt(
                status,
                process_.ExitCode,
                terminationRequested,
                ExitConfirmed: true);
        }

        private bool HasExited()
        {
            try
            {
                return process_.HasExited;
            }
            catch (InvalidOperationException)
            {
                return true;
            }
        }

        private void ThrowIfDisposed()
        {
            ObjectDisposedException.ThrowIf(disposed_, this);
        }
    }
}
