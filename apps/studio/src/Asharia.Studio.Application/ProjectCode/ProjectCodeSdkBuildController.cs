using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed class ProjectCodeSdkBuildController : IDisposable
{
    private const int CopyBufferSize = 1024 * 1024;
    private const int MaxCapturedBytesPerStream = 1024 * 1024;
    private const int MaxExecutionTreeEntries = 32_768;
    private const int MaxLegacyWindowsPathLength = 259;
    private const long MaxRawOutputFileBytes = 512L * 1024 * 1024;
    private readonly Dictionary<Guid, ActiveInvocation> activeInvocations_ = [];
    private readonly object stateGate_ = new();
    private readonly IProjectCodeSdkBuildProcessRunner processRunner_;
    private long nextInvocation_;
    private bool isDisposed_;

    public ProjectCodeSdkBuildController()
        : this(new ProjectCodeSdkBuildProcessRunner())
    {
    }

    internal ProjectCodeSdkBuildController(
        IProjectCodeSdkBuildProcessRunner processRunner)
    {
        ArgumentNullException.ThrowIfNull(processRunner);
        processRunner_ = processRunner;
    }

    public Task<ProjectCodeSdkBuildResult> ExecuteLatestAsync(
        ProjectCodeSdkBuildRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        ActiveInvocation? superseded = null;
        ActiveInvocation invocation;
        lock (stateGate_)
        {
            ObjectDisposedException.ThrowIf(isDisposed_, this);
            var invocationId = checked(++nextInvocation_);
            invocation = new ActiveInvocation(
                invocationId,
                request.WorkspaceLease.Workspace.ProjectId,
                cancellationToken);
            activeInvocations_.TryGetValue(
                invocation.ProjectId,
                out superseded);
            activeInvocations_[invocation.ProjectId] = invocation;
        }

        superseded?.Supersede();
        return ExecuteAndRetireAsync(request, invocation);
    }

    public static async Task<bool> IsRawOutputCurrentAsync(
        ProjectCodeRawBuildOutputLease lease,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(lease);
        if (!lease.IsCurrent
            || !await ProjectCodeImplicitSdkWorkspaceBuilder
                .IsBuildInputCurrentAsync(
                    lease.WorkspaceLease,
                    cancellationToken).ConfigureAwait(false))
        {
            return false;
        }

        var actual = await SnapshotRawOutputAsync(
            lease.Output.AbsoluteRoot,
            ExpectedOutputPaths(lease.Output),
            cancellationToken).ConfigureAwait(false);
        return lease.IsCurrent
            && actual is not null
            && HasSameRawOutputFiles(lease.Output.Files, actual);
    }

    public void Dispose()
    {
        ActiveInvocation[] active;
        lock (stateGate_)
        {
            if (isDisposed_)
            {
                return;
            }

            isDisposed_ = true;
            active = activeInvocations_.Values.ToArray();
            activeInvocations_.Clear();
        }

        foreach (var invocation in active)
        {
            invocation.Cancel();
        }
    }

    private async Task<ProjectCodeSdkBuildResult> ExecuteAndRetireAsync(
        ProjectCodeSdkBuildRequest request,
        ActiveInvocation invocation)
    {
        try
        {
            return await ExecuteCoreAsync(request, invocation)
                .ConfigureAwait(false);
        }
        finally
        {
            lock (stateGate_)
            {
                if (activeInvocations_.TryGetValue(
                        invocation.ProjectId,
                        out var active)
                    && ReferenceEquals(active, invocation))
                {
                    activeInvocations_.Remove(invocation.ProjectId);
                }
            }

            invocation.Dispose();
        }
    }

    private async Task<ProjectCodeSdkBuildResult> ExecuteCoreAsync(
        ProjectCodeSdkBuildRequest request,
        ActiveInvocation invocation)
    {
        var diagnostics = new List<ProjectCodeSdkBuildDiagnostic>();
        var steps = new List<ProjectCodeSdkBuildStepReceipt>();
        string? workRoot = null;
        string? outputCandidate = null;
        DotnetExecutionRoot? executionDotnet = null;
        ProjectCodeRawBuildOutputLease? successLease = null;
        var published = false;
        var failureOutcome = ProjectCodeSdkBuildOutcome.Failed;
        try
        {
            invocation.Token.ThrowIfCancellationRequested();
            if (!await ProjectCodeImplicitSdkWorkspaceBuilder
                    .IsBuildInputCurrentAsync(
                        request.WorkspaceLease,
                        invocation.Token).ConfigureAwait(false))
            {
                throw Failure(
                    "project-code.build.input-not-current",
                    null,
                    "workspace",
                    "SDK build requires one current source, credential, and immutable workspace.");
            }

            var outputPath = ResolveNewOutputPath(request, diagnostics);
            if (outputPath is null)
            {
                throw new BuildFailureException(
                    ProjectCodeSdkBuildOutcome.Failed,
                    diagnostics[0]);
            }

            var suffix = Guid.NewGuid().ToString("N");
            var workParent = Path.Combine(
                Path.GetTempPath(),
                "asharia-sdk-build");
            workRoot = Path.Combine(
                workParent,
                $"build-{suffix}");
            outputCandidate = Path.Combine(
                outputPath.Value.Parent,
                $".{outputPath.Value.Leaf}.candidate-{suffix}");
            if (!IsPathMapSafe(workRoot)
                || Directory.Exists(workRoot)
                || File.Exists(workRoot)
                || HasReparsePointInPath(workParent))
            {
                throw Failure(
                    "project-code.build.path-invalid",
                    null,
                    "workingRoot",
                    "Controller-owned SDK working root is not one new regular PathMap-safe path.");
            }

            ValidateWorkRootSeparation(
                request,
                workRoot,
                outputPath.Value.Root);
            Directory.CreateDirectory(workParent);
            if (HasReparsePointInPath(workParent))
            {
                throw Failure(
                    "project-code.build.path-invalid",
                    null,
                    "workingRoot",
                    "Controller-owned SDK working parent resolved through a reparse point.");
            }

            Directory.CreateDirectory(workRoot);
            await CopyWorkspaceAsync(
                request.WorkspaceLease.Workspace,
                workRoot,
                invocation.Token).ConfigureAwait(false);
            if (!await ValidateExecutionInputsAsync(
                    request,
                    workRoot,
                    requireExactTree: true,
                    invocation.Token).ConfigureAwait(false))
            {
                throw Failure(
                    "project-code.build.execution-input-drift",
                    null,
                    "workspace",
                    "Copied SDK execution inputs do not match the current immutable workspace.");
            }

            executionDotnet = await MaterializeDotnetExecutionRootAsync(
                request.WorkspaceLease.CredentialLease,
                workRoot,
                invocation.Token).ConfigureAwait(false);
            var controlRoot = Path.Combine(workRoot, ".asharia-control");
            CreateControlledDirectories(controlRoot);
            var environment = CreateEnvironment(
                executionDotnet,
                controlRoot);
            var redactions = CreateRedactions(
                request,
                workRoot,
                controlRoot);

            var probe = await RunStepAsync(
                request,
                invocation,
                workRoot,
                executionDotnet,
                environment,
                redactions,
                ProjectCodeSdkBuildStepKind.ProbeSdk,
                ["--version"],
                steps).ConfigureAwait(false);
            var resolvedSdk = probe.StandardOutput.Trim();
            if (!string.Equals(
                    resolvedSdk,
                    request.WorkspaceLease.Workspace.SdkVersion,
                    StringComparison.Ordinal))
            {
                throw Failure(
                    "project-code.build.sdk-version-mismatch",
                    ProjectCodeSdkBuildStepKind.ProbeSdk,
                    "sdkVersion",
                    "External dotnet resolved an SDK version different from the semantic credential.");
            }

            await RunStepAsync(
                request,
                invocation,
                workRoot,
                executionDotnet,
                environment,
                redactions,
                ProjectCodeSdkBuildStepKind.Restore,
                CreateRestoreArguments(request.WorkspaceLease.Workspace),
                steps).ConfigureAwait(false);
            EnsureExpectedOutputsAbsent(
                request.WorkspaceLease.Workspace,
                workRoot);
            await RunStepAsync(
                request,
                invocation,
                workRoot,
                executionDotnet,
                environment,
                redactions,
                ProjectCodeSdkBuildStepKind.Build,
                CreateBuildArguments(request.WorkspaceLease.Workspace),
                steps).ConfigureAwait(false);

            Directory.CreateDirectory(outputCandidate);
            var candidateFiles = await CopyRawOutputsAsync(
                request.WorkspaceLease.Workspace,
                workRoot,
                outputCandidate,
                invocation.Token).ConfigureAwait(false);
            executionDotnet.Dispose();
            executionDotnet = null;
            if (!TryDeleteOwnedTree(
                    workRoot,
                    workParent,
                    "build-"))
            {
                throw Failure(
                    "project-code.build.cleanup-failed",
                    ProjectCodeSdkBuildStepKind.Build,
                    "outputRoot",
                    "Controller-owned SDK working root could not be removed before output publication.");
            }

            workRoot = null;
            invocation.Token.ThrowIfCancellationRequested();
            if (!await ProjectCodeImplicitSdkWorkspaceBuilder
                    .IsBuildInputCurrentAsync(
                        request.WorkspaceLease,
                        invocation.Token).ConfigureAwait(false))
            {
                throw Failure(
                    "project-code.build.input-drift",
                    ProjectCodeSdkBuildStepKind.Build,
                    "workspace",
                    "Source, credential, or immutable workspace changed before raw output publication.");
            }

            var candidateCheck = await SnapshotRawOutputAsync(
                outputCandidate,
                ExpectedOutputPaths(request.WorkspaceLease.Workspace),
                invocation.Token).ConfigureAwait(false);
            if (candidateCheck is null
                || !HasSameRawOutputFiles(candidateFiles, candidateCheck))
            {
                throw Failure(
                    "project-code.build.output-drift",
                    ProjectCodeSdkBuildStepKind.Build,
                    "outputRoot",
                    "Raw build output candidate changed before publication.");
            }

            var output = CreateRawOutput(
                request.WorkspaceLease.Workspace,
                outputPath.Value.Root,
                candidateCheck);
            var lease = new ProjectCodeRawBuildOutputLease(
                request.WorkspaceLease,
                output);
            PublishIfLatest(
                invocation,
                outputCandidate,
                outputPath.Value.Root);
            outputCandidate = null;
            published = true;
            successLease = lease;
        }
        catch (OperationCanceledException)
        {
            failureOutcome = invocation.IsSuperseded
                ? ProjectCodeSdkBuildOutcome.Superseded
                : ProjectCodeSdkBuildOutcome.Canceled;
            diagnostics.Add(Diagnostic(
                invocation.IsSuperseded
                    ? "project-code.build.superseded"
                    : "project-code.build.canceled",
                null,
                "invocation",
                invocation.IsSuperseded
                    ? "A newer build invocation superseded this request."
                    : "SDK build was canceled by its caller."));
        }
        catch (BuildFailureException error)
        {
            failureOutcome = error.Outcome;
            diagnostics.Add(error.Diagnostic);
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or OverflowException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.build.execution-failed",
                null,
                "outputRoot",
                "Isolated SDK build could not complete its controlled file operation."));
        }
        finally
        {
            executionDotnet?.Dispose();
            if (workRoot is not null
                && Directory.Exists(workRoot)
                && !TryDeleteOwnedTree(
                    workRoot,
                    Path.GetDirectoryName(workRoot)!,
                    "build-"))
            {
                diagnostics.Add(Diagnostic(
                    "project-code.build.cleanup-failed",
                    null,
                    "outputRoot",
                    "Controller-owned SDK working root could not be removed."));
            }

            if (!published
                && outputCandidate is not null
                && Directory.Exists(outputCandidate)
                && !TryDeleteOwnedTree(
                    outputCandidate,
                    Path.GetDirectoryName(outputCandidate)!,
                    ".candidate-"))
            {
                diagnostics.Add(Diagnostic(
                    "project-code.build.cleanup-failed",
                    null,
                    "outputRoot",
                    "Controller-owned raw output candidate could not be removed."));
            }
        }

        return successLease is not null && diagnostics.Count == 0
            ? ProjectCodeSdkBuildResult.Success(
                invocation.Id,
                successLease,
                Array.AsReadOnly(steps.ToArray()))
            : ProjectCodeSdkBuildResult.Failure(
                invocation.Id,
                failureOutcome,
                Array.AsReadOnly(steps.ToArray()),
                diagnostics);
    }

    private async Task<ProjectCodeSdkBuildStepReceipt> RunStepAsync(
        ProjectCodeSdkBuildRequest request,
        ActiveInvocation invocation,
        string workRoot,
        DotnetExecutionRoot executionDotnet,
        IReadOnlyDictionary<string, string> environment,
        IReadOnlyDictionary<string, string> redactions,
        ProjectCodeSdkBuildStepKind kind,
        IReadOnlyList<string> arguments,
        ICollection<ProjectCodeSdkBuildStepReceipt> steps)
    {
        invocation.Token.ThrowIfCancellationRequested();
        var processResult = await processRunner_.RunAsync(
            new ProjectCodeBuildProcessRequest(
                kind,
                executionDotnet.Executable,
                workRoot,
                arguments,
                environment,
                redactions,
                request.StepTimeout,
                MaxCapturedBytesPerStream),
            invocation.Token).ConfigureAwait(false);
        var receipt = new ProjectCodeSdkBuildStepReceipt(
            kind,
            processResult.ExitCode,
            processResult.Duration,
            processResult.StandardOutput,
            processResult.StandardError,
            processResult.OutputTruncated);
        steps.Add(receipt);
        if (processResult.Outcome == ProjectCodeBuildProcessOutcome.Canceled)
        {
            invocation.Token.ThrowIfCancellationRequested();
            throw Failure(
                "project-code.build.process-canceled",
                kind,
                "process",
                "External SDK process reported cancellation without a canceled invocation.");
        }

        if (processResult.Outcome == ProjectCodeBuildProcessOutcome.TimedOut)
        {
            throw new BuildFailureException(
                ProjectCodeSdkBuildOutcome.TimedOut,
                Diagnostic(
                    "project-code.build.step-timeout",
                    kind,
                    "process",
                    "External SDK process exceeded its bounded step timeout."));
        }

        if (processResult.Outcome
            == ProjectCodeBuildProcessOutcome.OutputLimitExceeded)
        {
            throw Failure(
                "project-code.build.output-limit-exceeded",
                kind,
                "process",
                "External SDK process exceeded its bounded output capture budget.");
        }

        if (processResult.Outcome
            == ProjectCodeBuildProcessOutcome.TerminationFailed)
        {
            throw Failure(
                "project-code.build.process-termination-failed",
                kind,
                "process",
                "External SDK process tree could not be confirmed terminated.");
        }

        if (processResult.Outcome
            == ProjectCodeBuildProcessOutcome.LaunchFailed)
        {
            throw Failure(
                "project-code.build.process-launch-failed",
                kind,
                "process",
                "Exact credential-bound dotnet host could not be started.");
        }

        if (processResult.Outcome
            == ProjectCodeBuildProcessOutcome.CaptureFailed)
        {
            throw Failure(
                "project-code.build.output-capture-failed",
                kind,
                "process",
                "External SDK process output streams could not be drained.");
        }

        if (processResult.Outcome != ProjectCodeBuildProcessOutcome.Exited
            || processResult.ExitCode != 0)
        {
            throw Failure(
                "project-code.build.step-failed",
                kind,
                "process",
                "External SDK process exited unsuccessfully.");
        }

        invocation.Token.ThrowIfCancellationRequested();
        if (!request.WorkspaceLease.IsCurrent)
        {
            throw Failure(
                "project-code.build.input-drift",
                kind,
                "workspace",
                "Source, credential, or immutable workspace changed during an external step.");
        }

        if (!await ValidateDotnetExecutionRootAsync(
                executionDotnet,
                allowKnownScratch: true,
                invocation.Token).ConfigureAwait(false))
        {
            throw Failure(
                "project-code.build.sdk-execution-drift",
                kind,
                "dotnet",
                "Credential-derived SDK execution mirror changed during an external step.");
        }

        if (!await ValidateExecutionInputsAsync(
                request,
                workRoot,
                requireExactTree: false,
                invocation.Token).ConfigureAwait(false))
        {
            throw Failure(
                "project-code.build.execution-input-drift",
                kind,
                "workspace",
                "SDK execution input changed during an external step.");
        }

        return receipt;
    }

    private static async Task<bool> ValidateExecutionInputsAsync(
        ProjectCodeSdkBuildRequest request,
        string workRoot,
        bool requireExactTree,
        CancellationToken cancellationToken)
    {
        if (!request.WorkspaceLease.IsCurrent
            || !Directory.Exists(workRoot)
            || HasReparsePointInPath(workRoot))
        {
            return false;
        }

        foreach (var expected in request.WorkspaceLease.Workspace.Files)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var path = Path.Combine(
                workRoot,
                expected.RelativePath.Replace(
                    '/',
                    Path.DirectorySeparatorChar));
            var hash = await HashStableFileAsync(
                path,
                expected.Size,
                cancellationToken).ConfigureAwait(false);
            if (hash is null
                || hash.Size != expected.Size
                || !string.Equals(
                    hash.Sha256,
                    expected.Sha256,
                    StringComparison.Ordinal))
            {
                return false;
            }
        }

        return ValidateExecutionTree(
            workRoot,
            request.WorkspaceLease.Workspace.Files,
            requireExactTree);
    }

    private static bool ValidateExecutionTree(
        string workRoot,
        IReadOnlyList<ProjectCodeImplicitWorkspaceFile> inputFiles,
        bool requireExactTree)
    {
        try
        {
            var directories = new Stack<string>();
            directories.Push(workRoot);
            var actual = new List<string>();
            var entries = 0;
            while (directories.TryPop(out var directory))
            {
                foreach (var entry in Directory.EnumerateFileSystemEntries(
                    directory))
                {
                    if (++entries > MaxExecutionTreeEntries)
                    {
                        return false;
                    }

                    var attributes = File.GetAttributes(entry);
                    if ((attributes & FileAttributes.ReparsePoint) != 0)
                    {
                        return false;
                    }

                    var relative = Path.GetRelativePath(workRoot, entry)
                        .Replace(Path.DirectorySeparatorChar, '/');
                    if (!ProjectCodeSdkBuildPath.IsPortableRelativePath(
                            relative))
                    {
                        return false;
                    }

                    if ((attributes & FileAttributes.Directory) != 0)
                    {
                        directories.Push(entry);
                        actual.Add("d/" + relative);
                    }
                    else if (File.Exists(entry))
                    {
                        actual.Add("f/" + relative);
                    }
                    else
                    {
                        return false;
                    }
                }
            }

            if (!requireExactTree)
            {
                return true;
            }

            var expected = inputFiles
                .SelectMany(file =>
                    EnumerateExpectedClosureEntries(file.RelativePath))
                .Distinct(StringComparer.Ordinal)
                .OrderBy(value => value, Utf8Comparer.Instance)
                .ToArray();
            return actual
                .OrderBy(value => value, Utf8Comparer.Instance)
                .SequenceEqual(expected, StringComparer.Ordinal);
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static async Task CopyWorkspaceAsync(
        ProjectCodeImplicitSdkWorkspace workspace,
        string workRoot,
        CancellationToken cancellationToken)
    {
        foreach (var file in workspace.Files)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var destination = Path.Combine(
                workRoot,
                file.RelativePath.Replace(
                    '/',
                    Path.DirectorySeparatorChar));
            if (!await CopyVerifiedFileAsync(
                    file.AbsolutePath,
                    destination,
                    file.Size,
                    file.Sha256,
                    cancellationToken).ConfigureAwait(false))
            {
                throw Failure(
                    "project-code.build.workspace-copy-failed",
                    null,
                    file.RelativePath,
                    "Immutable workspace file changed during execution copy.");
            }
        }
    }

    private static async Task<DotnetExecutionRoot>
        MaterializeDotnetExecutionRootAsync(
            ProjectCodeBuildEnvironmentCredentialLease credentialLease,
            string workRoot,
            CancellationToken cancellationToken)
    {
        const string prefix = "managed/dotnet/";
        var sourceFiles = credentialLease.SourceLease.Projection.SelectedFiles
            .Where(file => file.RelativePath.StartsWith(
                prefix,
                StringComparison.Ordinal))
            .OrderBy(file => file.RelativePath, Utf8Comparer.Instance)
            .ToArray();
        if (sourceFiles.Length == 0)
        {
            throw Failure(
                "project-code.build.sdk-mirror-invalid",
                null,
                "dotnet",
                "Semantic credential contains no exact dotnet execution closure.");
        }

        var root = Path.Combine(workRoot, ".asharia-sdk");
        Directory.CreateDirectory(root);
        var files = new List<DotnetExecutionFile>(sourceFiles.Length);
        foreach (var source in sourceFiles)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var relativePath = source.RelativePath[prefix.Length..];
            if (!ProjectCodeSdkBuildPath.IsPortableRelativePath(
                    relativePath))
            {
                throw Failure(
                    "project-code.build.sdk-mirror-invalid",
                    null,
                    source.RelativePath,
                    "Credential dotnet path cannot be represented in the isolated SDK mirror.");
            }

            var destination = Path.Combine(
                root,
                relativePath.Replace(
                    '/',
                    Path.DirectorySeparatorChar));
            if (OperatingSystem.IsWindows()
                && destination.Length > MaxLegacyWindowsPathLength)
            {
                throw Failure(
                    "project-code.build.sdk-mirror-path-too-long",
                    null,
                    source.RelativePath,
                    "Credential dotnet path exceeds the supported Windows execution-path budget.");
            }

            Directory.CreateDirectory(
                Path.GetDirectoryName(destination)!);
            var copied = await CopyVerifiedFileAsync(
                source.AbsolutePath,
                destination,
                source.Size,
                source.Sha256,
                cancellationToken).ConfigureAwait(false);
            if (!copied)
            {
                throw Failure(
                    "project-code.build.sdk-mirror-copy-failed",
                    null,
                    source.RelativePath,
                    "Credential dotnet file changed during isolated SDK mirror materialization.");
            }

            files.Add(new DotnetExecutionFile(
                relativePath,
                destination,
                source.Size,
                source.Sha256));
        }

        var hostRelativePath =
            credentialLease.Credential.DotnetHost.RelativePath[
                prefix.Length..];
        var guards = new List<FileStream>(files.Count);
        try
        {
            foreach (var file in files)
            {
                var guard = await OpenVerifiedReadGuardAsync(
                    file,
                    cancellationToken).ConfigureAwait(false);
                if (guard is null)
                {
                    throw Failure(
                        "project-code.build.sdk-mirror-invalid",
                        null,
                        file.RelativePath,
                        "Materialized SDK mirror file could not be sealed against mutation.");
                }

                guards.Add(guard);
            }

            var execution = new DotnetExecutionRoot(
                root,
                Path.Combine(
                    root,
                    hostRelativePath.Replace(
                        '/',
                        Path.DirectorySeparatorChar)),
                Array.AsReadOnly(files.ToArray()),
                Array.AsReadOnly(guards.ToArray()));
            if (!await ValidateDotnetExecutionRootAsync(
                    execution,
                    allowKnownScratch: false,
                    cancellationToken).ConfigureAwait(false))
            {
                throw Failure(
                    "project-code.build.sdk-mirror-invalid",
                    null,
                    "dotnet",
                    "Materialized SDK mirror does not match the exact credential closure.");
            }

            return execution;
        }
        catch
        {
            foreach (var guard in guards)
            {
                guard.Dispose();
            }

            throw;
        }
    }

    private static async Task<bool> ValidateDotnetExecutionRootAsync(
        DotnetExecutionRoot execution,
        bool allowKnownScratch,
        CancellationToken cancellationToken)
    {
        if (!Directory.Exists(execution.Root)
            || HasReparsePointInPath(execution.Root))
        {
            return false;
        }

        if (OperatingSystem.IsWindows())
        {
            if (execution.Guards.Count != execution.Files.Count)
            {
                return false;
            }

            for (var index = 0; index < execution.Files.Count; ++index)
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (!execution.Guards[index].CanRead
                    || execution.Guards[index].Length
                        != execution.Files[index].Size)
                {
                    return false;
                }
            }
        }
        else
        {
            foreach (var expected in execution.Files)
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (!await HasExpectedFileAsync(
                        expected.AbsolutePath,
                        expected.Size,
                        expected.Sha256,
                        cancellationToken).ConfigureAwait(false))
                {
                    return false;
                }
            }
        }

        try
        {
            var directories = new Stack<string>();
            directories.Push(execution.Root);
            var actual = new List<string>();
            var entries = 0;
            while (directories.TryPop(out var directory))
            {
                foreach (var entry in Directory.EnumerateFileSystemEntries(
                    directory))
                {
                    if (++entries > MaxExecutionTreeEntries)
                    {
                        return false;
                    }

                    var attributes = File.GetAttributes(entry);
                    if ((attributes & FileAttributes.ReparsePoint) != 0)
                    {
                        return false;
                    }

                    var relativePath = Path.GetRelativePath(
                            execution.Root,
                            entry)
                        .Replace(Path.DirectorySeparatorChar, '/');
                    if (!ProjectCodeSdkBuildPath.IsPortableRelativePath(
                            relativePath))
                    {
                        return false;
                    }

                    if ((attributes & FileAttributes.Directory) != 0)
                    {
                        directories.Push(entry);
                        actual.Add("d/" + relativePath);
                    }
                    else if (File.Exists(entry))
                    {
                        actual.Add("f/" + relativePath);
                    }
                    else
                    {
                        return false;
                    }
                }
            }

            var expected = execution.Files
                .SelectMany(file =>
                    EnumerateExpectedClosureEntries(file.RelativePath))
                .Distinct(StringComparer.Ordinal)
                .ToHashSet(StringComparer.Ordinal);
            if (allowKnownScratch
                && actual.Contains(
                    "d/metadata",
                    StringComparer.Ordinal))
            {
                expected.Add("d/metadata");
            }

            return actual.Count == expected.Count
                && actual.All(expected.Contains);
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static async Task<bool> HasExpectedFileAsync(
        string path,
        long expectedSize,
        string expectedSha256,
        CancellationToken cancellationToken)
    {
        var hash = await HashStableFileAsync(
            path,
            expectedSize,
            cancellationToken).ConfigureAwait(false);
        return hash is not null
            && hash.Size == expectedSize
            && string.Equals(
                hash.Sha256,
                expectedSha256,
                StringComparison.Ordinal);
    }

    private static async Task<FileStream?> OpenVerifiedReadGuardAsync(
        DotnetExecutionFile file,
        CancellationToken cancellationToken)
    {
        FileStream? stream = null;
        try
        {
            if (HasReparsePointInPath(file.AbsolutePath))
            {
                return null;
            }

            stream = new FileStream(
                file.AbsolutePath,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                CopyBufferSize,
                FileOptions.Asynchronous | FileOptions.SequentialScan);
            if (stream.Length != file.Size)
            {
                stream.Dispose();
                return null;
            }

            var digest = await SHA256.HashDataAsync(
                stream,
                cancellationToken).ConfigureAwait(false);
            if (stream.Length != file.Size
                || !string.Equals(
                    Convert.ToHexString(digest).ToLowerInvariant(),
                    file.Sha256,
                    StringComparison.Ordinal))
            {
                stream.Dispose();
                return null;
            }

            stream.Position = 0;
            return stream;
        }
        catch (Exception error) when (
            error is IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            stream?.Dispose();
            return null;
        }
    }

    private static async Task<IReadOnlyList<ProjectCodeRawBuildOutputFile>>
        CopyRawOutputsAsync(
            ProjectCodeImplicitSdkWorkspace workspace,
            string workRoot,
            string outputCandidate,
            CancellationToken cancellationToken)
    {
        var files = new List<ProjectCodeRawBuildOutputFile>();
        foreach (var relativePath in ExpectedOutputPaths(workspace))
        {
            cancellationToken.ThrowIfCancellationRequested();
            var source = Path.Combine(
                workRoot,
                relativePath.Replace('/', Path.DirectorySeparatorChar));
            var hash = await HashStableFileAsync(
                source,
                MaxRawOutputFileBytes,
                cancellationToken).ConfigureAwait(false);
            if (hash is null)
            {
                throw Failure(
                    "project-code.build.output-missing",
                    ProjectCodeSdkBuildStepKind.Build,
                    relativePath,
                    "SDK build did not produce one stable bounded expected raw output.");
            }

            var destination = Path.Combine(
                outputCandidate,
                relativePath.Replace('/', Path.DirectorySeparatorChar));
            if (!await CopyVerifiedFileAsync(
                    source,
                    destination,
                    hash.Size,
                    hash.Sha256,
                    cancellationToken).ConfigureAwait(false))
            {
                throw Failure(
                    "project-code.build.output-changed",
                    ProjectCodeSdkBuildStepKind.Build,
                    relativePath,
                    "SDK raw output changed during clean handoff copy.");
            }

            files.Add(new ProjectCodeRawBuildOutputFile(
                relativePath,
                destination,
                hash.Size,
                hash.Sha256));
        }

        return Array.AsReadOnly(files.ToArray());
    }

    private static async Task<IReadOnlyList<ProjectCodeRawBuildOutputFile>?>
        SnapshotRawOutputAsync(
            string root,
            IReadOnlyList<string> expectedPaths,
            CancellationToken cancellationToken)
    {
        try
        {
            if (!Directory.Exists(root) || HasReparsePointInPath(root))
            {
                return null;
            }

            var expected = expectedPaths.ToHashSet(StringComparer.Ordinal);
            if (!TryEnumerateRawOutputTree(
                    root,
                    out var actualPaths,
                    out var actualClosure)
                || !actualPaths.ToHashSet(StringComparer.Ordinal)
                    .SetEquals(expected)
                || !HasExpectedClosure(actualPaths, actualClosure))
            {
                return null;
            }

            var files = new List<ProjectCodeRawBuildOutputFile>(
                actualPaths.Length);
            foreach (var relativePath in actualPaths)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var path = Path.Combine(
                    root,
                    relativePath.Replace(
                        '/',
                        Path.DirectorySeparatorChar));
                var hash = await HashStableFileAsync(
                    path,
                    MaxRawOutputFileBytes,
                    cancellationToken).ConfigureAwait(false);
                if (hash is null)
                {
                    return null;
                }

                files.Add(new ProjectCodeRawBuildOutputFile(
                    relativePath,
                    path,
                    hash.Size,
                    hash.Sha256));
            }

            return Array.AsReadOnly(files.ToArray());
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static bool TryEnumerateRawOutputTree(
        string root,
        out string[] filePaths,
        out string[] closure)
    {
        filePaths = [];
        closure = [];
        try
        {
            var files = new List<string>();
            var actual = new List<string>();
            var directories = new Stack<string>();
            directories.Push(root);
            var entries = 0;
            while (directories.TryPop(out var directory))
            {
                foreach (var entry in Directory.EnumerateFileSystemEntries(
                    directory))
                {
                    if (++entries > MaxExecutionTreeEntries)
                    {
                        return false;
                    }

                    var attributes = File.GetAttributes(entry);
                    if ((attributes & FileAttributes.ReparsePoint) != 0)
                    {
                        return false;
                    }

                    var relative = Path.GetRelativePath(root, entry)
                        .Replace(Path.DirectorySeparatorChar, '/');
                    if (!ProjectCodeSdkBuildPath.IsPortableRelativePath(
                            relative))
                    {
                        return false;
                    }

                    if ((attributes & FileAttributes.Directory) != 0)
                    {
                        actual.Add("d/" + relative);
                        directories.Push(entry);
                    }
                    else if (File.Exists(entry))
                    {
                        files.Add(relative);
                        actual.Add("f/" + relative);
                    }
                    else
                    {
                        return false;
                    }
                }
            }

            filePaths = files
                .OrderBy(value => value, Utf8Comparer.Instance)
                .ToArray();
            closure = actual
                .OrderBy(value => value, Utf8Comparer.Instance)
                .ToArray();
            return true;
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static bool HasExpectedClosure(
        IReadOnlyList<string> filePaths,
        IReadOnlyList<string> actualClosure)
    {
        var expected = filePaths
            .SelectMany(EnumerateExpectedClosureEntries)
            .Distinct(StringComparer.Ordinal)
            .OrderBy(value => value, Utf8Comparer.Instance)
            .ToArray();
        return actualClosure.SequenceEqual(expected, StringComparer.Ordinal);
    }

    private static ProjectCodeRawBuildOutput CreateRawOutput(
        ProjectCodeImplicitSdkWorkspace workspace,
        string finalRoot,
        IReadOnlyList<ProjectCodeRawBuildOutputFile> candidateFiles)
    {
        var outputId = ComputeRawOutputId(workspace, candidateFiles);
        var finalFiles = candidateFiles
            .Select(file => new ProjectCodeRawBuildOutputFile(
                file.RelativePath,
                Path.Combine(
                    finalRoot,
                    file.RelativePath.Replace(
                        '/',
                        Path.DirectorySeparatorChar)),
                file.Size,
                file.Sha256))
            .ToArray();
        return new ProjectCodeRawBuildOutput(
            outputId,
            workspace.ProjectId,
            workspace.WorkspaceId,
            workspace.CredentialId,
            workspace.SdkVersion,
            workspace.TargetFramework,
            workspace.AssemblyName,
            finalRoot,
            workspace.OutputAssemblyRelativePath,
            workspace.ReferenceAssemblyRelativePath,
            workspace.PortablePdbRelativePath,
            workspace.DependencyFileRelativePath,
            Array.AsReadOnly(finalFiles));
    }

    private static string ComputeRawOutputId(
        ProjectCodeImplicitSdkWorkspace workspace,
        IReadOnlyList<ProjectCodeRawBuildOutputFile> files)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        AppendString(hash, "project-code-raw-sdk-build-output-v1");
        AppendString(hash, workspace.WorkspaceId);
        AppendString(hash, workspace.CredentialId);
        AppendString(hash, workspace.AssemblyName);
        Span<byte> size = stackalloc byte[sizeof(long)];
        foreach (var file in files.OrderBy(
            file => file.RelativePath,
            Utf8Comparer.Instance))
        {
            AppendString(hash, file.RelativePath);
            BinaryPrimitives.WriteInt64LittleEndian(size, file.Size);
            hash.AppendData(size);
            hash.AppendData(Convert.FromHexString(file.Sha256));
        }

        return "sha256-"
            + Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    private static IReadOnlyList<string> CreateRestoreArguments(
        ProjectCodeImplicitSdkWorkspace workspace) =>
        [
            "restore",
            workspace.EntryProjectRelativePath,
            "--configfile",
            "NuGet.Config",
            "--packages",
            ".asharia-control/nuget-packages",
            "--disable-build-servers",
            "--disable-parallel",
            "--no-http-cache",
            "--force",
            "--tl:off",
            "--verbosity",
            "minimal",
            "-p:RestoreIgnoreFailedSources=false",
            "-p:RestoreNoCache=true",
            "-p:MSBuildEnableWorkloadResolver=false",
            "-p:EnableTargetingPackDownload=false",
            "-p:EnableRuntimePackDownload=false",
            "-p:EnableAppHostPackDownload=false",
            "-p:DisableTransitiveFrameworkReferenceDownloads=true",
            "-p:EnableNETAnalyzers=false",
            "-p:RunAnalyzers=false",
            "-p:SuppressImplicitGitSourceLink=true",
            "-p:ImportByWildcardBeforeMicrosoftCommonProps=false",
            "-p:ImportByWildcardAfterMicrosoftCommonProps=false",
            "-p:ImportByWildcardBeforeMicrosoftCommonTargets=false",
            "-p:ImportByWildcardAfterMicrosoftCommonTargets=false",
            "-noAutoResponse",
        ];

    private static IReadOnlyList<string> CreateBuildArguments(
        ProjectCodeImplicitSdkWorkspace workspace) =>
        [
            "build",
            workspace.EntryProjectRelativePath,
            "--configuration",
            "Release",
            "--no-restore",
            "--disable-build-servers",
            "--nologo",
            "--tl:off",
            "--verbosity",
            "minimal",
            "-p:UseSharedCompilation=false",
            "-p:MSBuildEnableWorkloadResolver=false",
            "-p:EnableTargetingPackDownload=false",
            "-p:EnableRuntimePackDownload=false",
            "-p:EnableAppHostPackDownload=false",
            "-p:DisableTransitiveFrameworkReferenceDownloads=true",
            "-p:EnableNETAnalyzers=false",
            "-p:RunAnalyzers=false",
            "-p:SuppressImplicitGitSourceLink=true",
            "-p:ImportByWildcardBeforeMicrosoftCommonProps=false",
            "-p:ImportByWildcardAfterMicrosoftCommonProps=false",
            "-p:ImportByWildcardBeforeMicrosoftCommonTargets=false",
            "-p:ImportByWildcardAfterMicrosoftCommonTargets=false",
            "-noAutoResponse",
        ];

    private static IReadOnlyDictionary<string, string> CreateEnvironment(
        DotnetExecutionRoot executionDotnet,
        string controlRoot)
    {
        var comparer = OperatingSystem.IsWindows()
            ? StringComparer.OrdinalIgnoreCase
            : StringComparer.Ordinal;
        var environment = new Dictionary<string, string>(comparer);
        foreach (var name in new[]
        {
            "SystemRoot",
            "WINDIR",
            "NUMBER_OF_PROCESSORS",
            "PROCESSOR_ARCHITECTURE",
        })
        {
            var value = Environment.GetEnvironmentVariable(name);
            if (!string.IsNullOrEmpty(value))
            {
                environment[name] = value;
            }
        }

        var temp = Path.Combine(controlRoot, "temp");
        var userProfile = Path.Combine(controlRoot, "user");
        var programFiles = Path.Combine(controlRoot, "program-files");
        var programData = Path.Combine(controlRoot, "program-data");
        environment["DOTNET_ROOT"] = executionDotnet.Root;
        environment["DOTNET_HOST_PATH"] = executionDotnet.Executable;
        environment["DOTNET_CLI_HOME"] =
            Path.Combine(controlRoot, "dotnet-home");
        environment["DOTNET_BUNDLE_EXTRACT_BASE_DIR"] =
            Path.Combine(controlRoot, "bundle-extract");
        environment["NUGET_PACKAGES"] =
            Path.Combine(controlRoot, "nuget-packages");
        environment["NUGET_HTTP_CACHE_PATH"] =
            Path.Combine(controlRoot, "nuget-http-cache");
        environment["NUGET_SCRATCH"] =
            Path.Combine(controlRoot, "nuget-scratch");
        environment["NUGET_PLUGINS_CACHE_PATH"] =
            Path.Combine(controlRoot, "nuget-plugins-cache");
        environment["MSBuildUserExtensionsPath"] =
            Path.Combine(controlRoot, "msbuild-user-extensions");
        environment["USERPROFILE"] = userProfile;
        environment["HOME"] = userProfile;
        environment["APPDATA"] =
            Path.Combine(userProfile, "AppData", "Roaming");
        environment["LOCALAPPDATA"] =
            Path.Combine(userProfile, "AppData", "Local");
        environment["PROGRAMFILES"] = programFiles;
        environment["PROGRAMFILES(X86)"] = programFiles;
        environment["PROGRAMW6432"] = programFiles;
        environment["PROGRAMDATA"] = programData;
        environment["ALLUSERSPROFILE"] = programData;
        environment["TEMP"] = temp;
        environment["TMP"] = temp;
        environment["TMPDIR"] = temp;
        environment["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1";
        environment["DOTNET_NOLOGO"] = "1";
        environment["DOTNET_SKIP_FIRST_TIME_EXPERIENCE"] = "1";
        environment["DOTNET_MULTILEVEL_LOOKUP"] = "0";
        environment["DOTNET_ADD_GLOBAL_TOOLS_TO_PATH"] = "0";
        environment["DOTNET_GENERATE_ASPNET_CERTIFICATE"] = "0";
        environment["DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE"] = "1";
        environment["DOTNET_SKIP_WORKLOAD_INTEGRITY_CHECK"] = "1";
        environment["DOTNET_SDK_VULNERABILITY_CHECK_DISABLE"] = "1";
        environment["DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER"] = "1";
        environment["DOTNET_CLI_UI_LANGUAGE"] = "en-US";
        environment["VSLANG"] = "1033";
        environment["MSBUILDDISABLENODEREUSE"] = "1";
        environment["MSBuildEnableWorkloadResolver"] = "false";
        environment["NUGET_XMLDOC_MODE"] = "skip";
        return environment;
    }

    private static IReadOnlyDictionary<string, string> CreateRedactions(
        ProjectCodeSdkBuildRequest request,
        string workRoot,
        string controlRoot)
    {
        var comparer = OperatingSystem.IsWindows()
            ? StringComparer.OrdinalIgnoreCase
            : StringComparer.Ordinal;
        var credential = request.WorkspaceLease.CredentialLease.Credential;
        var redactions = new Dictionary<string, string>(comparer);
        foreach (var pair in new[]
        {
            new KeyValuePair<string, string>(
                controlRoot,
                "<control-root>"),
            new KeyValuePair<string, string>(
                workRoot,
                "<build-root>"),
            new KeyValuePair<string, string>(
                request.WorkspaceLease.Workspace.AbsoluteRoot,
                "<workspace-root>"),
            new KeyValuePair<string, string>(
                request.WorkspaceLease.ProjectRoot,
                "<project-root>"),
            new KeyValuePair<string, string>(
                Path.GetDirectoryName(credential.DotnetExecutable)!,
                "<dotnet-root>"),
        })
        {
            redactions.TryAdd(pair.Key, pair.Value);
        }

        return redactions;
    }

    private static void CreateControlledDirectories(string controlRoot)
    {
        foreach (var relative in new[]
        {
            "dotnet-home",
            "bundle-extract",
            "nuget-packages",
            "nuget-http-cache",
            "nuget-scratch",
            "nuget-plugins-cache",
            "msbuild-user-extensions",
            "user/AppData/Roaming",
            "user/AppData/Local",
            "program-files",
            "program-data",
            "temp",
        })
        {
            Directory.CreateDirectory(Path.Combine(controlRoot, relative));
        }
    }

    private static void EnsureExpectedOutputsAbsent(
        ProjectCodeImplicitSdkWorkspace workspace,
        string workRoot)
    {
        foreach (var relativePath in ExpectedOutputPaths(workspace))
        {
            var path = Path.Combine(
                workRoot,
                relativePath.Replace('/', Path.DirectorySeparatorChar));
            if (File.Exists(path) || Directory.Exists(path))
            {
                throw Failure(
                    "project-code.build.output-preexisting",
                    ProjectCodeSdkBuildStepKind.Restore,
                    relativePath,
                    "Restore unexpectedly produced one declared build output.");
            }
        }
    }

    private static IReadOnlyList<string> ExpectedOutputPaths(
        ProjectCodeImplicitSdkWorkspace workspace) =>
        [
            workspace.OutputAssemblyRelativePath,
            workspace.ReferenceAssemblyRelativePath,
            workspace.PortablePdbRelativePath,
            workspace.DependencyFileRelativePath,
        ];

    private static IReadOnlyList<string> ExpectedOutputPaths(
        ProjectCodeRawBuildOutput output) =>
        [
            output.ImplementationAssemblyRelativePath,
            output.ReferenceAssemblyRelativePath,
            output.PortablePdbRelativePath,
            output.DependencyFileRelativePath,
        ];

    private static OutputPath? ResolveNewOutputPath(
        ProjectCodeSdkBuildRequest request,
        ICollection<ProjectCodeSdkBuildDiagnostic> diagnostics)
    {
        try
        {
            if (!Path.IsPathFullyQualified(request.OutputRoot))
            {
                throw new ArgumentException();
            }

            var root = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(request.OutputRoot));
            var parent = Path.GetDirectoryName(root);
            var leaf = Path.GetFileName(root);
            if (string.IsNullOrEmpty(parent)
                || string.IsNullOrWhiteSpace(leaf)
                || leaf.Length > 100
                || Directory.Exists(root)
                || File.Exists(root)
                || !Directory.Exists(parent)
                || HasReparsePointInPath(parent)
                || !IsPathMapSafe(root))
            {
                throw new IOException();
            }

            ValidateOutputSeparation(request, root);
            return new OutputPath(root, parent, leaf);
        }
        catch (BuildFailureException error)
        {
            diagnostics.Add(error.Diagnostic);
            return null;
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.build.output-path-invalid",
                null,
                "outputRoot",
                "Raw build output root must be one new absolute regular path with a safe bounded leaf."));
            return null;
        }
    }

    private static void ValidateOutputSeparation(
        ProjectCodeSdkBuildRequest request,
        string outputRoot)
    {
        var workspaceLease = request.WorkspaceLease;
        if (Overlaps(workspaceLease.ProjectRoot, outputRoot)
            || Overlaps(workspaceLease.Workspace.AbsoluteRoot, outputRoot))
        {
            throw Failure(
                "project-code.build.output-overlap",
                null,
                "outputRoot",
                "Raw build output must be disjoint from project and immutable workspace roots.");
        }

        var credential = workspaceLease.CredentialLease.Credential;
        var protectedRoots = new[]
        {
            Path.GetDirectoryName(credential.DotnetExecutable),
            credential.Sdk.AbsoluteRoot,
            credential.HostFxr.AbsoluteRoot,
            credential.HostRuntime.AbsoluteRoot,
            credential.ReferencePack.AbsoluteRoot,
            Path.GetDirectoryName(credential.RuntimeContract.AbsolutePath),
            Path.GetDirectoryName(credential.EditorContract.AbsolutePath),
        };
        if (protectedRoots
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .Any(root => Overlaps(root!, outputRoot)))
        {
            throw Failure(
                "project-code.build.output-overlap",
                null,
                "outputRoot",
                "Raw build output must be disjoint from the semantic build environment.");
        }
    }

    private static void ValidateWorkRootSeparation(
        ProjectCodeSdkBuildRequest request,
        string workRoot,
        string outputRoot)
    {
        if (Overlaps(workRoot, outputRoot))
        {
            throw Failure(
                "project-code.build.path-invalid",
                null,
                "workingRoot",
                "Controller-owned SDK working root must be disjoint from raw output.");
        }

        try
        {
            ValidateOutputSeparation(request, workRoot);
        }
        catch (BuildFailureException)
        {
            throw Failure(
                "project-code.build.path-invalid",
                null,
                "workingRoot",
                "Controller-owned SDK working root must be disjoint from build inputs.");
        }
    }

    private static bool Overlaps(string left, string right) =>
        IsDescendantOrSame(left, right)
        || IsDescendantOrSame(right, left);

    private static bool IsDescendantOrSame(string root, string candidate)
    {
        var normalizedRoot = Path.TrimEndingDirectorySeparator(
            Path.GetFullPath(root));
        var normalizedCandidate = Path.TrimEndingDirectorySeparator(
            Path.GetFullPath(candidate));
        var comparison = OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase
            : StringComparison.Ordinal;
        return string.Equals(normalizedRoot, normalizedCandidate, comparison)
            || normalizedCandidate.StartsWith(
                normalizedRoot + Path.DirectorySeparatorChar,
                comparison);
    }

    private static bool IsPathMapSafe(string value) =>
        value.IndexOfAny(
            ['$', '@', '%', ';', '*', '?', ',', '=']) < 0;

    private static bool HasReparsePointInPath(string path)
    {
        var comparer = OperatingSystem.IsWindows()
            ? StringComparer.OrdinalIgnoreCase
            : StringComparer.Ordinal;
        var current = Path.GetFullPath(path);
        while (!string.IsNullOrEmpty(current))
        {
            if ((File.Exists(current) || Directory.Exists(current))
                && (File.GetAttributes(current)
                    & FileAttributes.ReparsePoint) != 0)
            {
                return true;
            }

            var parent = Path.GetDirectoryName(current);
            if (string.IsNullOrEmpty(parent)
                || comparer.Equals(parent, current))
            {
                break;
            }

            current = parent;
        }

        return false;
    }

    private static async Task<FileHash?> HashStableFileAsync(
        string path,
        long maxBytes,
        CancellationToken cancellationToken)
    {
        try
        {
            if (HasReparsePointInPath(path))
            {
                return null;
            }

            var before = new FileInfo(path);
            var length = before.Length;
            var writeTime = before.LastWriteTimeUtc;
            if (length < 0 || length > maxBytes)
            {
                return null;
            }

            var buffer = ArrayPool<byte>.Shared.Rent(CopyBufferSize);
            try
            {
                using var hash = IncrementalHash.CreateHash(
                    HashAlgorithmName.SHA256);
                await using var stream = new FileStream(
                    path,
                    FileMode.Open,
                    FileAccess.Read,
                    FileShare.Read,
                    CopyBufferSize,
                    FileOptions.Asynchronous | FileOptions.SequentialScan);
                if (stream.Length != length)
                {
                    return null;
                }

                long total = 0;
                while (true)
                {
                    var read = await stream.ReadAsync(
                        buffer.AsMemory(0, buffer.Length),
                        cancellationToken).ConfigureAwait(false);
                    if (read == 0)
                    {
                        break;
                    }

                    if (read > length - total)
                    {
                        return null;
                    }

                    hash.AppendData(buffer, 0, read);
                    total = checked(total + read);
                }

                before.Refresh();
                if (!before.Exists
                    || total != length
                    || before.Length != length
                    || before.LastWriteTimeUtc != writeTime
                    || HasReparsePointInPath(path))
                {
                    return null;
                }

                return new FileHash(
                    length,
                    Convert.ToHexString(hash.GetHashAndReset())
                        .ToLowerInvariant());
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(buffer);
            }
        }
        catch (Exception error) when (
            error is IOException
                or NotSupportedException
                or OverflowException
                or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static async Task<bool> CopyVerifiedFileAsync(
        string source,
        string destination,
        long expectedSize,
        string expectedSha256,
        CancellationToken cancellationToken)
    {
        try
        {
            var parent = Path.GetDirectoryName(destination)
                ?? throw new IOException();
            Directory.CreateDirectory(parent);
            var buffer = ArrayPool<byte>.Shared.Rent(CopyBufferSize);
            try
            {
                using var hash = IncrementalHash.CreateHash(
                    HashAlgorithmName.SHA256);
                await using var input = new FileStream(
                    source,
                    FileMode.Open,
                    FileAccess.Read,
                    FileShare.Read,
                    CopyBufferSize,
                    FileOptions.Asynchronous | FileOptions.SequentialScan);
                await using var output = new FileStream(
                    destination,
                    FileMode.CreateNew,
                    FileAccess.Write,
                    FileShare.None,
                    CopyBufferSize,
                    FileOptions.Asynchronous
                        | FileOptions.SequentialScan);
                if (input.Length != expectedSize
                    || HasReparsePointInPath(source))
                {
                    return false;
                }

                long total = 0;
                while (true)
                {
                    var read = await input.ReadAsync(
                        buffer.AsMemory(0, buffer.Length),
                        cancellationToken).ConfigureAwait(false);
                    if (read == 0)
                    {
                        break;
                    }

                    if (read > expectedSize - total)
                    {
                        return false;
                    }

                    await output.WriteAsync(
                        buffer.AsMemory(0, read),
                        cancellationToken).ConfigureAwait(false);
                    hash.AppendData(buffer, 0, read);
                    total = checked(total + read);
                }

                await output.FlushAsync(cancellationToken)
                    .ConfigureAwait(false);
                return total == expectedSize
                    && string.Equals(
                        Convert.ToHexString(hash.GetHashAndReset())
                            .ToLowerInvariant(),
                        expectedSha256,
                        StringComparison.Ordinal);
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(buffer);
            }
        }
        catch (Exception error) when (
            error is IOException
                or NotSupportedException
                or OverflowException
                or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static bool HasSameRawOutputFiles(
        IReadOnlyList<ProjectCodeRawBuildOutputFile> expected,
        IReadOnlyList<ProjectCodeRawBuildOutputFile> actual)
    {
        var comparer = OperatingSystem.IsWindows()
            ? StringComparer.OrdinalIgnoreCase
            : StringComparer.Ordinal;
        if (expected.Count != actual.Count)
        {
            return false;
        }

        var actualByPath = actual.ToDictionary(
            file => file.RelativePath,
            StringComparer.Ordinal);
        return expected.All(file =>
            actualByPath.TryGetValue(file.RelativePath, out var candidate)
            && comparer.Equals(
                Path.GetFullPath(file.AbsolutePath),
                Path.GetFullPath(candidate.AbsolutePath))
            && file.Size == candidate.Size
            && string.Equals(
                file.Sha256,
                candidate.Sha256,
                StringComparison.Ordinal));
    }

    private void PublishIfLatest(
        ActiveInvocation invocation,
        string candidate,
        string destination)
    {
        lock (stateGate_)
        {
            invocation.Token.ThrowIfCancellationRequested();
            if (isDisposed_
                || !activeInvocations_.TryGetValue(
                    invocation.ProjectId,
                    out var active)
                || !ReferenceEquals(active, invocation))
            {
                invocation.Supersede();
                invocation.Token.ThrowIfCancellationRequested();
            }

            Directory.Move(candidate, destination);
        }
    }

    private static IEnumerable<string> EnumerateExpectedClosureEntries(
        string filePath)
    {
        yield return "f/" + filePath;
        var separator = filePath.LastIndexOf('/');
        while (separator > 0)
        {
            filePath = filePath[..separator];
            yield return "d/" + filePath;
            separator = filePath.LastIndexOf('/');
        }
    }

    private static bool TryDeleteOwnedTree(
        string root,
        string parent,
        string marker)
    {
        try
        {
            var resolvedRoot = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(root));
            var resolvedParent = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(parent));
            if (!IsDescendantOrSame(resolvedParent, resolvedRoot)
                || string.Equals(
                    resolvedParent,
                    resolvedRoot,
                    OperatingSystem.IsWindows()
                        ? StringComparison.OrdinalIgnoreCase
                        : StringComparison.Ordinal)
                || !Path.GetFileName(resolvedRoot).Contains(
                    marker,
                    StringComparison.Ordinal))
            {
                return false;
            }

            Directory.Delete(resolvedRoot, recursive: true);
            return true;
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static void AppendString(
        IncrementalHash hash,
        string value)
    {
        var bytes = Encoding.UTF8.GetBytes(value);
        Span<byte> length = stackalloc byte[sizeof(int)];
        BinaryPrimitives.WriteInt32LittleEndian(length, bytes.Length);
        hash.AppendData(length);
        hash.AppendData(bytes);
    }

    private static BuildFailureException Failure(
        string code,
        ProjectCodeSdkBuildStepKind? step,
        string location,
        string message) =>
        new(
            ProjectCodeSdkBuildOutcome.Failed,
            Diagnostic(code, step, location, message));

    private static ProjectCodeSdkBuildDiagnostic Diagnostic(
        string code,
        ProjectCodeSdkBuildStepKind? step,
        string location,
        string message) =>
        new(code, step, location, message);

    private readonly record struct OutputPath(
        string Root,
        string Parent,
        string Leaf);

    private sealed record FileHash(long Size, string Sha256);

    private sealed record DotnetExecutionFile(
        string RelativePath,
        string AbsolutePath,
        long Size,
        string Sha256);

    private sealed class DotnetExecutionRoot(
        string root,
        string executable,
        IReadOnlyList<DotnetExecutionFile> files,
        IReadOnlyList<FileStream> guards) : IDisposable
    {
        public string Root { get; } = root;

        public string Executable { get; } = executable;

        public IReadOnlyList<DotnetExecutionFile> Files { get; } = files;

        public IReadOnlyList<FileStream> Guards { get; } = guards;

        public void Dispose()
        {
            foreach (var guard in Guards)
            {
                guard.Dispose();
            }
        }
    }

    private sealed class BuildFailureException(
        ProjectCodeSdkBuildOutcome outcome,
        ProjectCodeSdkBuildDiagnostic diagnostic) : Exception(
            diagnostic.Message)
    {
        public ProjectCodeSdkBuildOutcome Outcome { get; } = outcome;

        public ProjectCodeSdkBuildDiagnostic Diagnostic { get; } =
            diagnostic;
    }

    private sealed class ActiveInvocation : IDisposable
    {
        private readonly CancellationTokenSource cancellation_;
        private int isSuperseded_;
        private int isDisposed_;

        public ActiveInvocation(
            long id,
            Guid projectId,
            CancellationToken cancellationToken)
        {
            Id = id;
            ProjectId = projectId;
            cancellation_ =
                CancellationTokenSource.CreateLinkedTokenSource(
                    cancellationToken);
        }

        public long Id { get; }

        public Guid ProjectId { get; }

        public CancellationToken Token => cancellation_.Token;

        public bool IsSuperseded =>
            Volatile.Read(ref isSuperseded_) != 0;

        public void Supersede()
        {
            Interlocked.Exchange(ref isSuperseded_, 1);
            Cancel();
        }

        public void Cancel()
        {
            try
            {
                cancellation_.Cancel();
            }
            catch (ObjectDisposedException)
            {
            }
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref isDisposed_, 1) == 0)
            {
                cancellation_.Dispose();
            }
        }
    }

    private sealed class Utf8Comparer : IComparer<string>
    {
        public static Utf8Comparer Instance { get; } = new();

        public int Compare(string? left, string? right)
        {
            if (ReferenceEquals(left, right))
            {
                return 0;
            }

            if (left is null)
            {
                return -1;
            }

            if (right is null)
            {
                return 1;
            }

            return Encoding.UTF8.GetBytes(left)
                .AsSpan()
                .SequenceCompareTo(Encoding.UTF8.GetBytes(right));
        }
    }
}
