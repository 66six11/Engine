using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.ProjectCode;

internal enum ProjectCodeBuildProcessOutcome
{
    Exited = 0,
    LaunchFailed = 1,
    Canceled = 2,
    TimedOut = 3,
    OutputLimitExceeded = 4,
    TerminationFailed = 5,
    CaptureFailed = 6,
}

internal sealed record ProjectCodeBuildProcessRequest
{
    public ProjectCodeBuildProcessRequest(
        ProjectCodeSdkBuildStepKind kind,
        string executable,
        string workingDirectory,
        IReadOnlyList<string> arguments,
        IReadOnlyDictionary<string, string> environment,
        IReadOnlyDictionary<string, string> redactions,
        TimeSpan timeout,
        int maxCapturedBytesPerStream)
    {
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind));
        }

        ArgumentException.ThrowIfNullOrWhiteSpace(executable);
        ArgumentException.ThrowIfNullOrWhiteSpace(workingDirectory);
        ArgumentNullException.ThrowIfNull(arguments);
        ArgumentNullException.ThrowIfNull(environment);
        ArgumentNullException.ThrowIfNull(redactions);
        if (!Path.IsPathFullyQualified(executable)
            || !Path.IsPathFullyQualified(workingDirectory))
        {
            throw new ArgumentException(
                "Process executable and working directory must be absolute.");
        }

        if (timeout <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout));
        }

        if (maxCapturedBytesPerStream <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maxCapturedBytesPerStream));
        }

        var argumentSnapshot = arguments.ToArray();
        var environmentSnapshot = environment.ToDictionary(
            pair => pair.Key,
            pair => pair.Value,
            OperatingSystem.IsWindows()
                ? StringComparer.OrdinalIgnoreCase
                : StringComparer.Ordinal);
        var redactionSnapshot = redactions.ToDictionary(
            pair => pair.Key,
            pair => pair.Value,
            OperatingSystem.IsWindows()
                ? StringComparer.OrdinalIgnoreCase
                : StringComparer.Ordinal);
        if (argumentSnapshot.Any(argument => argument is null)
            || environmentSnapshot.Any(pair =>
                string.IsNullOrWhiteSpace(pair.Key)
                || pair.Value is null)
            || redactionSnapshot.Any(pair =>
                string.IsNullOrWhiteSpace(pair.Key)
                || string.IsNullOrWhiteSpace(pair.Value)))
        {
            throw new ArgumentException(
                "Process arguments, environment, and redactions must be canonical non-null snapshots.");
        }

        Kind = kind;
        Executable = executable;
        WorkingDirectory = workingDirectory;
        Arguments = Array.AsReadOnly(argumentSnapshot);
        Environment = environmentSnapshot;
        Redactions = redactionSnapshot;
        Timeout = timeout;
        MaxCapturedBytesPerStream = maxCapturedBytesPerStream;
    }

    public ProjectCodeSdkBuildStepKind Kind { get; }

    public string Executable { get; }

    public string WorkingDirectory { get; }

    public IReadOnlyList<string> Arguments { get; }

    public IReadOnlyDictionary<string, string> Environment { get; }

    public IReadOnlyDictionary<string, string> Redactions { get; }

    public TimeSpan Timeout { get; }

    public int MaxCapturedBytesPerStream { get; }
}

internal sealed record ProjectCodeBuildProcessResult(
    ProjectCodeBuildProcessOutcome Outcome,
    int? ExitCode,
    TimeSpan Duration,
    string StandardOutput,
    string StandardError,
    bool OutputTruncated,
    bool TerminationConfirmed);

internal interface IProjectCodeSdkBuildProcessRunner
{
    Task<ProjectCodeBuildProcessResult> RunAsync(
        ProjectCodeBuildProcessRequest request,
        CancellationToken cancellationToken);
}

internal sealed class ProjectCodeSdkBuildProcessRunner :
    IProjectCodeSdkBuildProcessRunner
{
    private static readonly UTF8Encoding Utf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: false);
    private static readonly Regex AnsiEscapePattern = new(
        "\u001B\\[[0-?]*[ -/]*[@-~]",
        RegexOptions.CultureInvariant);
    private static readonly TimeSpan ProcessTerminationGrace =
        TimeSpan.FromSeconds(5);
    private static readonly TimeSpan OutputDrainGrace =
        TimeSpan.FromSeconds(2);

    public async Task<ProjectCodeBuildProcessResult> RunAsync(
        ProjectCodeBuildProcessRequest request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        var stopwatch = Stopwatch.StartNew();
        if (cancellationToken.IsCancellationRequested)
        {
            return Result(
                request,
                ProjectCodeBuildProcessOutcome.Canceled,
                null,
                stopwatch.Elapsed,
                [],
                [],
                outputTruncated: false,
                terminationConfirmed: true);
        }

        using var process = new Process
        {
            StartInfo = CreateStartInfo(request),
        };
        try
        {
            if (!process.Start())
            {
                return Result(
                    request,
                    ProjectCodeBuildProcessOutcome.LaunchFailed,
                    null,
                    stopwatch.Elapsed,
                    [],
                    [],
                    outputTruncated: false,
                    terminationConfirmed: true);
            }
        }
        catch (Exception error) when (
            error is InvalidOperationException
                or NotSupportedException
                or Win32Exception)
        {
            return Result(
                request,
                ProjectCodeBuildProcessOutcome.LaunchFailed,
                null,
                stopwatch.Elapsed,
                [],
                [],
                outputTruncated: false,
                terminationConfirmed: true);
        }

        try
        {
            process.StandardInput.Close();
        }
        catch (IOException)
        {
        }

        using var captureCancellation = new CancellationTokenSource();
        var outputLimitSignal =
            new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var standardOutputTask = CaptureAsync(
            process.StandardOutput.BaseStream,
            request.MaxCapturedBytesPerStream,
            outputLimitSignal,
            captureCancellation.Token);
        var standardErrorTask = CaptureAsync(
            process.StandardError.BaseStream,
            request.MaxCapturedBytesPerStream,
            outputLimitSignal,
            captureCancellation.Token);
        var exitTask = process.WaitForExitAsync(CancellationToken.None);
        var timeoutTask = Task.Delay(request.Timeout);
        var cancellationSignal =
            new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var cancellationRegistration = cancellationToken.Register(
            () => cancellationSignal.TrySetResult());
        var completed = await Task.WhenAny(
            exitTask,
            timeoutTask,
            cancellationSignal.Task,
            outputLimitSignal.Task).ConfigureAwait(false);

        ProjectCodeBuildProcessOutcome outcome;
        var terminationConfirmed = true;
        if (completed == exitTask)
        {
            await exitTask.ConfigureAwait(false);
            outcome = ProjectCodeBuildProcessOutcome.Exited;
        }
        else
        {
            outcome = completed == cancellationSignal.Task
                ? ProjectCodeBuildProcessOutcome.Canceled
                : completed == timeoutTask
                    ? ProjectCodeBuildProcessOutcome.TimedOut
                    : ProjectCodeBuildProcessOutcome.OutputLimitExceeded;
            terminationConfirmed = await TerminateAsync(process)
                .ConfigureAwait(false);
            if (!terminationConfirmed)
            {
                outcome = ProjectCodeBuildProcessOutcome.TerminationFailed;
            }
        }

        var capturesCompleted = await WaitForCapturesAsync(
            standardOutputTask,
            standardErrorTask,
            OutputDrainGrace).ConfigureAwait(false);
        if (!capturesCompleted)
        {
            await captureCancellation.CancelAsync().ConfigureAwait(false);
        }

        var standardOutput = await GetCaptureAsync(
            standardOutputTask,
            OutputDrainGrace)
            .ConfigureAwait(false);
        var standardError = await GetCaptureAsync(
            standardErrorTask,
            OutputDrainGrace)
            .ConfigureAwait(false);
        var outputTruncated = standardOutput.Truncated
            || standardError.Truncated;
        if (outcome == ProjectCodeBuildProcessOutcome.Exited
            && outputTruncated)
        {
            outcome = ProjectCodeBuildProcessOutcome.OutputLimitExceeded;
        }
        else if (outcome == ProjectCodeBuildProcessOutcome.Exited
            && (!standardOutput.Completed || !standardError.Completed))
        {
            outcome = ProjectCodeBuildProcessOutcome.CaptureFailed;
        }

        int? exitCode = null;
        try
        {
            if (process.HasExited)
            {
                exitCode = process.ExitCode;
            }
        }
        catch (InvalidOperationException)
        {
            terminationConfirmed = false;
            outcome = ProjectCodeBuildProcessOutcome.TerminationFailed;
        }

        return Result(
            request,
            outcome,
            exitCode,
            stopwatch.Elapsed,
            standardOutput.Bytes,
            standardError.Bytes,
            outputTruncated,
            terminationConfirmed);
    }

    private static ProcessStartInfo CreateStartInfo(
        ProjectCodeBuildProcessRequest request)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = request.Executable,
            WorkingDirectory = request.WorkingDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = Utf8,
            StandardErrorEncoding = Utf8,
        };
        foreach (var argument in request.Arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        startInfo.Environment.Clear();
        foreach (var pair in request.Environment)
        {
            startInfo.Environment[pair.Key] = pair.Value;
        }

        return startInfo;
    }

    private static async Task<CapturedOutput> CaptureAsync(
        Stream stream,
        int maxBytes,
        TaskCompletionSource outputLimitSignal,
        CancellationToken cancellationToken)
    {
        var buffer = new byte[8192];
        using var captured = new MemoryStream(maxBytes);
        var truncated = false;
        try
        {
            while (true)
            {
                var read = await stream.ReadAsync(
                    buffer.AsMemory(0, buffer.Length),
                    cancellationToken).ConfigureAwait(false);
                if (read == 0)
                {
                    return new CapturedOutput(
                        captured.ToArray(),
                        truncated,
                        Completed: true);
                }

                var remaining = maxBytes - checked((int)captured.Length);
                if (remaining > 0)
                {
                    await captured.WriteAsync(
                        buffer.AsMemory(0, Math.Min(remaining, read)),
                        cancellationToken).ConfigureAwait(false);
                }

                if (read > remaining)
                {
                    truncated = true;
                    outputLimitSignal.TrySetResult();
                }
            }
        }
        catch (Exception error) when (
            error is IOException or OperationCanceledException)
        {
            return new CapturedOutput(
                captured.ToArray(),
                truncated,
                Completed: false);
        }
    }

    private static async Task<bool> WaitForCapturesAsync(
        Task<CapturedOutput> standardOutputTask,
        Task<CapturedOutput> standardErrorTask,
        TimeSpan grace)
    {
        var captures = Task.WhenAll(standardOutputTask, standardErrorTask);
        return await Task.WhenAny(captures, Task.Delay(grace))
            .ConfigureAwait(false) == captures;
    }

    private static async Task<CapturedOutput> GetCaptureAsync(
        Task<CapturedOutput> captureTask,
        TimeSpan grace)
    {
        try
        {
            if (await Task.WhenAny(captureTask, Task.Delay(grace))
                    .ConfigureAwait(false) != captureTask)
            {
                return new CapturedOutput(
                    [],
                    Truncated: false,
                    Completed: false);
            }

            return await captureTask.ConfigureAwait(false);
        }
        catch (Exception error) when (
            error is IOException or OperationCanceledException)
        {
            return new CapturedOutput([], Truncated: false, Completed: false);
        }
    }

    private static async Task<bool> TerminateAsync(Process process)
    {
        try
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
            }
        }
        catch (Exception error) when (
            error is AggregateException
                or InvalidOperationException
                or NotSupportedException
                or Win32Exception)
        {
            try
            {
                if (!process.HasExited)
                {
                    return false;
                }
            }
            catch (InvalidOperationException)
            {
                return false;
            }
        }

        try
        {
            using var cleanupCancellation = new CancellationTokenSource(
                ProcessTerminationGrace);
            await process.WaitForExitAsync(cleanupCancellation.Token)
                .ConfigureAwait(false);
            return process.HasExited;
        }
        catch (Exception error) when (
            error is AggregateException
                or InvalidOperationException
                or OperationCanceledException
                or Win32Exception)
        {
            return false;
        }
    }

    private static ProjectCodeBuildProcessResult Result(
        ProjectCodeBuildProcessRequest request,
        ProjectCodeBuildProcessOutcome outcome,
        int? exitCode,
        TimeSpan duration,
        byte[] standardOutput,
        byte[] standardError,
        bool outputTruncated,
        bool terminationConfirmed) =>
        new(
            outcome,
            exitCode,
            duration,
            NormalizeOutput(standardOutput, request.Redactions),
            NormalizeOutput(standardError, request.Redactions),
            outputTruncated,
            terminationConfirmed);

    private static string NormalizeOutput(
        byte[] bytes,
        IReadOnlyDictionary<string, string> redactions)
    {
        var value = Utf8.GetString(bytes)
            .Replace("\r\n", "\n", StringComparison.Ordinal)
            .Replace('\r', '\n')
            .Replace('\0', '\uFFFD');
        value = AnsiEscapePattern.Replace(value, string.Empty);
        var comparison = OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase
            : StringComparison.Ordinal;
        foreach (var pair in redactions
            .OrderByDescending(pair => pair.Key.Length)
            .ThenBy(pair => pair.Key, StringComparer.Ordinal))
        {
            value = value.Replace(pair.Key, pair.Value, comparison);
        }

        return value;
    }

    private sealed record CapturedOutput(
        byte[] Bytes,
        bool Truncated,
        bool Completed);
}
