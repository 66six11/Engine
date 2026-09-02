using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Viewports;
using Editor.Shell.Composition;
using Xunit;

namespace Editor.Tests.Shell.Composition;

[CollectionDefinition(kCollectionName, DisableParallelization = true)]
public sealed class StudioProcessAcceptanceCollection
{
    public const string kCollectionName = "Studio process acceptance";
}

internal static class StudioGpuAcceptanceGate
{
    internal const string EnvironmentVariable = "ASHARIA_RUN_STUDIO_GPU_ACCEPTANCE";

    internal static string? SkipReason => ResolveSkipReason(
        OperatingSystem.IsWindows(),
        Environment.GetEnvironmentVariable(EnvironmentVariable));

    internal static string? ResolveSkipReason(bool isWindows, string? optInValue)
    {
        if (!isWindows)
        {
            return "Studio GPU process acceptance requires Windows.";
        }

        return string.Equals(optInValue, "1", StringComparison.Ordinal)
            ? null
            : $"Studio GPU process acceptance is opt-in; set {EnvironmentVariable}=1 to run it.";
    }

    internal static void RequireEnabled()
    {
        if (SkipReason is { } reason)
        {
            throw new InvalidOperationException(
                $"The Studio GPU acceptance skip gate was bypassed: {reason}");
        }
    }
}

// Editor.Tests uses xUnit 2.9.3. Its supported conditional-skip path is to set
// FactAttribute.Skip during discovery; runtime SkipException handling requires xUnit v3.
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class StudioGpuFactAttribute : FactAttribute
{
    public StudioGpuFactAttribute()
    {
        Skip = StudioGpuAcceptanceGate.SkipReason;
    }
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class StudioGpuTheoryAttribute : TheoryAttribute
{
    public StudioGpuTheoryAttribute()
    {
        Skip = StudioGpuAcceptanceGate.SkipReason;
    }
}

[Collection(StudioProcessAcceptanceCollection.kCollectionName)]
public sealed class StudioProcessAcceptanceTests
{
    private static readonly TimeSpan kReadyTimeout = TimeSpan.FromSeconds(15);
    private static readonly TimeSpan kExitTimeout = TimeSpan.FromSeconds(15);
    private static readonly TimeSpan kKillTimeout = TimeSpan.FromSeconds(5);
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

    [StudioGpuFact]
    [Trait("Category", "StudioGpuAcceptance")]
    public void Realtime_scene_viewport_sustains_at_least_60_surface_updates_per_second()
    {
        RunGpuSmoke(
            "steady-realtime",
            timeoutMilliseconds: 35_000,
            StudioViewportCadenceSmoke.CommandLineSwitch);
    }

    [StudioGpuFact]
    [Trait("Category", "StudioGpuAcceptance")]
    public void Camera_navigation_does_not_starve_scene_view_surface_updates()
    {
        RunGpuSmoke(
            "camera-navigation-cadence",
            timeoutMilliseconds: 35_000,
            StudioViewportCadenceSmoke.CameraNavigationCommandLineSwitch);
    }

    [StudioGpuFact]
    [Trait("Category", "StudioGpuAcceptance")]
    public void Scene_mesh_closes_from_v2_document_to_presented_wireframe_draw()
    {
        RunGpuSmoke(
            "scene-mesh-closure",
            timeoutMilliseconds: 45_000,
            StudioSceneMeshSmoke.CommandLineSwitch);
    }

    [StudioGpuTheory]
    [Trait("Category", "StudioGpuAcceptance")]
    [MemberData(nameof(ViewportTransactionSmokeCases))]
    public void Viewport_transaction_smoke_family_runs_at_the_real_Studio_boundary(
        string scenario,
        int timeoutMilliseconds,
        string[] arguments)
    {
        RunGpuSmoke(scenario, timeoutMilliseconds, arguments);
    }

    public static IEnumerable<object[]> ViewportTransactionSmokeCases()
    {
        var patterns = new[] { "grow", "shrink", "aba", "sawtooth", "jitter" };
        var inputRates = new[] { 30, 60, 120, 240 };
        foreach (var pattern in patterns)
        {
            foreach (var inputRate in inputRates)
            {
                var inputCount = inputRate <= 60 ? 60 : 120;
                yield return
                [
                    $"resize-{pattern}-{inputRate}hz",
                    35_000,
                    new[]
                    {
                        StudioViewportTransactionResizeSmoke.CommandLineSwitch,
                        $"--viewport-resize-pattern={pattern}",
                        $"--viewport-input-hz={inputRate}",
                        $"--viewport-input-count={inputCount}",
                    },
                ];
            }
        }

        yield return
        [
            "resize-pixel-boundary-120hz",
            35_000,
            new[]
            {
                StudioViewportTransactionResizeSmoke.CommandLineSwitch,
                "--viewport-resize-pattern=pixel",
                "--viewport-input-hz=120",
                "--viewport-input-count=90",
            },
        ];

        foreach (var pattern in new[] { "grow", "shrink", "aba" })
        {
            yield return
            [
                $"window-resize-main-{pattern}-120hz-performance",
                45_000,
                new[]
                {
                    StudioViewportTransactionWindowResizeSmoke.CommandLineSwitch,
                    $"--viewport-window-pattern={pattern}",
                    "--viewport-window-input-hz=120",
                    "--viewport-window-input-count=90",
                    $"{StudioViewportTransactionWindowResizeSmoke.EvidenceOptionPrefix}" +
                        "performance",
                },
            ];
        }

        yield return
        [
            "window-resize-main-aba-structural",
            45_000,
            new[]
            {
                StudioViewportTransactionWindowResizeSmoke.CommandLineSwitch,
                "--viewport-window-pattern=aba",
                "--viewport-window-input-hz=60",
                "--viewport-window-input-count=12",
                $"{StudioViewportTransactionWindowResizeSmoke.EvidenceOptionPrefix}" +
                    "continuous",
            },
        ];

        yield return
        [
            "window-resize-main-height-aba-projection",
            45_000,
            new[]
            {
                StudioViewportTransactionWindowResizeSmoke.CommandLineSwitch,
                "--viewport-window-pattern=height-aba",
                "--viewport-window-input-hz=60",
                "--viewport-window-input-count=12",
                $"{StudioViewportTransactionWindowResizeSmoke.EvidenceOptionPrefix}" +
                    "continuous",
            },
        ];

        foreach (var delay in new[] { 5, 15, 30, 50 })
        {
            yield return
            [
                $"overload-{delay}ms",
                35_000,
                new[]
                {
                    StudioViewportTransactionOverloadSmoke.CommandLineSwitch,
                    $"--viewport-prepare-delay-ms={delay}",
                    $"--viewport-rendered-delay-ms={delay}",
                    "--viewport-input-hz=240",
                    "--viewport-input-count=120",
                },
            ];
        }

        foreach (var stage in new[]
        {
            "surface-create",
            "stream-open",
            "native-submit",
            "after-submit",
            "lease",
            "image-import",
            "surface-update",
            "after-prepared",
            "before-publish",
            "validation",
            "before-finalize",
            "rendered",
            "retirement",
        })
        {
            yield return
            [
                $"faults-{stage}",
                35_000,
                new[]
                {
                    StudioViewportTransactionFaultSmoke.CommandLineSwitch,
                    $"--viewport-fault-stage={stage}",
                },
            ];
        }

        yield return
        [
            "supersede",
            35_000,
            new[] { StudioViewportTransactionSupersedeSmoke.CommandLineSwitch },
        ];

        foreach (var mode in new[]
        {
            "success",
            "scene-game",
            "validation-reject",
            "finalize-fault",
            "apply-mid-fault",
            "rollback-layout-fault",
        })
        {
            yield return
            [
                $"multi-endpoint-{mode}",
                35_000,
                new[]
                {
                    StudioViewportMultiEndpointSmoke.CommandLineSwitch,
                    $"--viewport-multi-mode={mode}",
                },
            ];
        }

        yield return
        [
            "flash-structural",
            35_000,
            new[] { StudioViewportTransactionFlashSmoke.CommandLineSwitch },
        ];
    }

    private static void RunGpuSmoke(
        string scenario,
        int timeoutMilliseconds,
        params string[] arguments)
    {
        StudioGpuAcceptanceGate.RequireEnabled();

        var transactionContract = TryResolveTransactionSmokeContract(arguments);
        var isSceneMeshSmoke = arguments.Length == 1 && string.Equals(
            arguments[0],
            StudioSceneMeshSmoke.CommandLineSwitch,
            StringComparison.Ordinal);
        var isCameraNavigationCadence = arguments.Length == 1 && string.Equals(
            arguments[0],
            StudioViewportCadenceSmoke.CameraNavigationCommandLineSwitch,
            StringComparison.Ordinal);
        if (transactionContract is not null)
        {
            Assert.Equal(transactionContract.CaseScenario, scenario);
        }
        else if (isSceneMeshSmoke)
        {
            Assert.Equal("scene-mesh-closure", scenario);
        }
        else if (isCameraNavigationCadence)
        {
            Assert.Equal("camera-navigation-cadence", scenario);
        }
        else
        {
            Assert.Equal("steady-realtime", scenario);
            Assert.Equal(
                new[] { StudioViewportCadenceSmoke.CommandLineSwitch },
                arguments);
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
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            },
        };
        foreach (var argument in arguments)
        {
            process.StartInfo.ArgumentList.Add(argument);
        }

        Assert.True(process.Start());
        var standardOutput = process.StandardOutput.ReadToEndAsync();
        var standardError = process.StandardError.ReadToEndAsync();
        if (!process.WaitForExit(timeoutMilliseconds))
        {
            process.Kill(entireProcessTree: true);
            if (!process.WaitForExit(milliseconds: 5_000))
            {
                throw new TimeoutException(
                    $"Studio viewport smoke '{scenario}' did not reap within 5 seconds " +
                    "after process-tree termination.");
            }
            throw new TimeoutException(
                $"Studio viewport smoke '{scenario}' did not finish within " +
                $"{timeoutMilliseconds} ms.");
        }

        Assert.True(
            Task.WaitAll([standardOutput, standardError], millisecondsTimeout: 5_000),
            $"Studio viewport smoke '{scenario}' output did not drain within 5 seconds.");
        var output = standardOutput.IsCompletedSuccessfully ? standardOutput.Result : string.Empty;
        var error = standardError.IsCompletedSuccessfully ? standardError.Result : string.Empty;
        Assert.True(
            process.ExitCode == 0,
            $"Studio viewport smoke '{scenario}' exited with {process.ExitCode}." +
            Environment.NewLine + output + Environment.NewLine + error);
        Assert.DoesNotContain(" FAIL:", output + error, StringComparison.Ordinal);
        if (transactionContract is not null)
        {
            Assert.Contains(
                transactionContract.PassMarker,
                output,
                StringComparison.Ordinal);
            AssertStructuredTransactionSummary(transactionContract, output);
        }
        else if (isSceneMeshSmoke)
        {
            Assert.Contains(StudioSceneMeshSmoke.PassMarker, output, StringComparison.Ordinal);
            AssertStructuredSceneMeshEvidence(output);
        }
        else if (isCameraNavigationCadence)
        {
            Assert.Contains(
                "Studio viewport camera-navigation surface-update cadence PASS:",
                output,
                StringComparison.Ordinal);
        }
        else
        {
            Assert.Contains(
                "Studio viewport steady surface-update cadence PASS:",
                output,
                StringComparison.Ordinal);
        }
        if (Array.Exists(
                arguments,
                static argument => string.Equals(
                    argument,
                    StudioViewportTransactionResizeSmoke.CommandLineSwitch,
                    StringComparison.Ordinal)))
        {
            AssertStructuredResizeEvidence(scenario, output, arguments);
        }
        if (Array.Exists(
                arguments,
                static argument => string.Equals(
                    argument,
                    StudioViewportTransactionFlashSmoke.CommandLineSwitch,
                    StringComparison.Ordinal)))
        {
            AssertStructuredFlashEvidence(output);
        }
        if (Array.Exists(
                arguments,
                static argument => string.Equals(
                    argument,
                    StudioViewportTransactionWindowResizeSmoke.CommandLineSwitch,
                    StringComparison.Ordinal)))
        {
            AssertWindowResizeEvidence(scenario, output, arguments);
        }
    }

    internal static StudioTransactionSmokeContract ResolveTransactionSmokeContract(
        IReadOnlyList<string> arguments)
    {
        var switches = new[]
        {
            StudioViewportTransactionResizeSmoke.CommandLineSwitch,
            StudioViewportTransactionOverloadSmoke.CommandLineSwitch,
            StudioViewportTransactionFaultSmoke.CommandLineSwitch,
            StudioViewportTransactionSupersedeSmoke.CommandLineSwitch,
            StudioViewportMultiEndpointSmoke.CommandLineSwitch,
            StudioViewportTransactionFlashSmoke.CommandLineSwitch,
            StudioViewportTransactionWindowResizeSmoke.CommandLineSwitch,
        };
        var selected = switches.Where(arguments.Contains).ToArray();
        if (selected.Length != 1)
        {
            throw new ArgumentException(
                "A transaction acceptance case must select exactly one smoke family.",
                nameof(arguments));
        }

        return selected[0] switch
        {
            StudioViewportTransactionResizeSmoke.CommandLineSwitch =>
                ResolveResizeContract(arguments),
            StudioViewportTransactionOverloadSmoke.CommandLineSwitch =>
                ResolveOverloadContract(arguments),
            StudioViewportTransactionFaultSmoke.CommandLineSwitch =>
                ResolveFaultContract(arguments),
            StudioViewportTransactionSupersedeSmoke.CommandLineSwitch =>
                new StudioTransactionSmokeContract(
                    "supersede",
                    "supersede",
                    "supersede",
                    "viewport-transaction-supersede PASS:"),
            StudioViewportMultiEndpointSmoke.CommandLineSwitch =>
                ResolveMultiEndpointContract(arguments),
            StudioViewportTransactionFlashSmoke.CommandLineSwitch =>
                new StudioTransactionSmokeContract(
                    "flash-structural",
                    "flash-structural",
                    "flash-structural",
                    "viewport-transaction-flash-structural PASS:"),
            StudioViewportTransactionWindowResizeSmoke.CommandLineSwitch =>
                ResolveWindowResizeContract(arguments),
            _ => throw new UnreachableException(),
        };
    }

    private static StudioTransactionSmokeContract? TryResolveTransactionSmokeContract(
        IReadOnlyList<string> arguments)
    {
        return arguments.Any(static argument => argument.StartsWith(
            "--smoke-viewport-",
            StringComparison.Ordinal))
            ? ResolveTransactionSmokeContract(arguments)
            : null;
    }

    private static StudioTransactionSmokeContract ResolveResizeContract(
        IReadOnlyList<string> arguments)
    {
        var pattern = ReadRequiredArgument(arguments, "--viewport-resize-pattern=");
        var inputRate = ReadRequiredArgument(arguments, "--viewport-input-hz=");
        var caseScenario = pattern == "pixel"
            ? $"resize-pixel-boundary-{inputRate}hz"
            : $"resize-{pattern}-{inputRate}hz";
        return new StudioTransactionSmokeContract(
            caseScenario,
            "resize",
            "resize",
            "viewport-transaction-resize PASS:");
    }

    private static StudioTransactionSmokeContract ResolveOverloadContract(
        IReadOnlyList<string> arguments)
    {
        var delay = ReadRequiredArgument(arguments, "--viewport-prepare-delay-ms=");
        Assert.Equal(
            delay,
            ReadRequiredArgument(arguments, "--viewport-rendered-delay-ms="));
        return new StudioTransactionSmokeContract(
            $"overload-{delay}ms",
            "overload",
            "overload",
            "viewport-transaction-overload PASS:");
    }

    private static StudioTransactionSmokeContract ResolveFaultContract(
        IReadOnlyList<string> arguments)
    {
        var stage = ReadRequiredArgument(arguments, "--viewport-fault-stage=");
        return new StudioTransactionSmokeContract(
            $"faults-{stage}",
            "faults",
            $"faults-{stage}",
            "viewport-transaction-faults PASS:");
    }

    private static StudioTransactionSmokeContract ResolveMultiEndpointContract(
        IReadOnlyList<string> arguments)
    {
        var mode = ReadRequiredArgument(arguments, "--viewport-multi-mode=");
        return new StudioTransactionSmokeContract(
            $"multi-endpoint-{mode}",
            "multi-endpoint",
            $"multi-endpoint-{mode}",
            "viewport-multi-endpoint PASS:");
    }

    private static StudioTransactionSmokeContract ResolveWindowResizeContract(
        IReadOnlyList<string> arguments)
    {
        var pattern = ReadRequiredArgument(arguments, "--viewport-window-pattern=");
        var inputRate = ReadRequiredArgument(arguments, "--viewport-window-input-hz=");
        var evidence = ReadRequiredArgument(
            arguments,
            StudioViewportTransactionWindowResizeSmoke.EvidenceOptionPrefix);
        var performance = evidence == "performance";
        Assert.True(performance || evidence == "continuous");
        var capturesProjection = pattern == "height-aba";
        return new StudioTransactionSmokeContract(
            capturesProjection
                ? "window-resize-main-height-aba-projection"
                : performance
                ? $"window-resize-main-{pattern}-{inputRate}hz-performance"
                : $"window-resize-main-{pattern}-structural",
            capturesProjection
                ? "window-resize-projection"
                : performance ? "window-resize-performance" : "window-resize-structural",
            capturesProjection
                ? "window-resize-projection"
                : performance ? "window-resize-performance" : "window-resize-structural",
            "viewport-transaction-window-resize PASS:");
    }

    private static string ReadRequiredArgument(
        IReadOnlyList<string> arguments,
        string prefix)
    {
        var argument = Assert.Single(
            arguments,
            candidate => candidate.StartsWith(prefix, StringComparison.Ordinal));
        var value = argument[prefix.Length..];
        Assert.False(string.IsNullOrWhiteSpace(value));
        return value;
    }

    internal static void AssertStructuredSceneMeshEvidence(string output)
    {
        var line = Assert.Single(
            output.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries),
            static candidate => candidate.StartsWith(
                StudioSceneMeshSmoke.EvidencePrefix,
                StringComparison.Ordinal));
        using var document = JsonDocument.Parse(
            line[StudioSceneMeshSmoke.EvidencePrefix.Length..]);
        var root = document.RootElement;
        Assert.Equal("scene-mesh-closure", root.GetProperty("scenario").GetString());
        Assert.Equal(
            "studio-scene-mesh-vulkan",
            root.GetProperty("evidenceKind").GetString());
        Assert.False(root.GetProperty("pixelEvidenceAvailable").GetBoolean());
        Assert.False(root.GetProperty("physicalDisplayedEvidenceAvailable").GetBoolean());
        Assert.Equal(2, root.GetProperty("sceneSchemaVersion").GetInt32());
        Assert.True(root.GetProperty("revisionOrderStrict").GetBoolean());
        Assert.True(root.GetProperty("finalExactSurface").GetBoolean());
        Assert.True(root.GetProperty("stalePresentationExcluded").GetBoolean());

        var meshObjectId = root.GetProperty("meshObjectId").GetGuid();
        var emptyObjectId = root.GetProperty("emptyObjectId").GetGuid();
        var assetId = root.GetProperty("assetId").GetGuid();
        Assert.NotEqual(Guid.Empty, meshObjectId);
        Assert.NotEqual(Guid.Empty, emptyObjectId);
        Assert.NotEqual(meshObjectId, emptyObjectId);
        Assert.Equal(
            Asharia.Studio.Application.Scenes.SceneMeshReference
                .DirectionalWedgeValidation.AssetId,
            assetId);

        var initial = root.GetProperty("initial");
        var mesh = root.GetProperty("meshCreated");
        var empty = root.GetProperty("emptyEntityCreated");
        var transformed = root.GetProperty("transformUpdated");
        AssertStage(initial, "initial-empty", expectedEntityCount: 0);
        AssertStage(mesh, "mesh-created", expectedEntityCount: 1);
        AssertStage(empty, "empty-entity-created", expectedEntityCount: 2);
        AssertStage(transformed, "transform-updated", expectedEntityCount: 2);

        var initialRevision = initial.GetProperty("targetRevision").GetUInt64();
        var meshRevision = mesh.GetProperty("targetRevision").GetUInt64();
        var emptyRevision = empty.GetProperty("targetRevision").GetUInt64();
        var supersededRevision = root.GetProperty("supersededRevision").GetUInt64();
        var transformedRevision = transformed.GetProperty("targetRevision").GetUInt64();
        Assert.Equal(initialRevision + 1U, meshRevision);
        Assert.Equal(meshRevision + 1U, emptyRevision);
        Assert.Equal(emptyRevision + 1U, supersededRevision);
        Assert.Equal(supersededRevision + 1U, transformedRevision);
        Assert.True(
            initial.GetProperty("requestSequence").GetUInt64() <
            mesh.GetProperty("requestSequence").GetUInt64());
        Assert.True(
            mesh.GetProperty("requestSequence").GetUInt64() <
            empty.GetProperty("requestSequence").GetUInt64());
        Assert.True(
            empty.GetProperty("requestSequence").GetUInt64() <
            root.GetProperty("supersededRequestSequence").GetUInt64());
        Assert.True(
            root.GetProperty("supersededRequestSequence").GetUInt64() <
            transformed.GetProperty("requestSequence").GetUInt64());
        Assert.True(root.GetProperty("supersededFrameIndex").GetUInt64() > 0);
        Assert.Equal(
            root.GetProperty("supersededRequestSequence").GetUInt64() + 1U,
            transformed.GetProperty("minimumPresentableSequence").GetUInt64());
        Assert.Equal(
            1UL,
            root.GetProperty("presentedFramesAcrossSupersede").GetUInt64());
        Assert.Equal(
            transformedRevision,
            root.GetProperty("finalPresentedRevision").GetUInt64());

        var initialReceipt = initial.GetProperty("receipt");
        Assert.Equal(0U, initialReceipt.GetProperty("inputCount").GetUInt32());
        Assert.Equal(0U, initialReceipt.GetProperty("resolvedCount").GetUInt32());
        Assert.Equal(0U, initialReceipt.GetProperty("rejectedCount").GetUInt32());
        Assert.Equal(0U, initialReceipt.GetProperty("indexedDrawCount").GetUInt32());
        Assert.Equal(
            JsonValueKind.Null,
            initialReceipt.GetProperty("representativeSourceEntityId").ValueKind);
        Assert.Equal(
            JsonValueKind.Null,
            initialReceipt.GetProperty("representativeObjectId").ValueKind);
        Assert.Equal(
            JsonValueKind.Null,
            initialReceipt.GetProperty("representativeAssetId").ValueKind);
        Assert.Equal(0UL, initialReceipt.GetProperty("meshResourceKey").GetUInt64());
        Assert.Equal(0UL, initialReceipt.GetProperty("materialResourceKey").GetUInt64());
        Assert.Equal(0UL, initialReceipt.GetProperty("productHash").GetUInt64());

        var meshReceipt = mesh.GetProperty("receipt");
        var emptyReceipt = empty.GetProperty("receipt");
        var transformedReceipt = transformed.GetProperty("receipt");
        var supersededReceipt = root.GetProperty("supersededReceipt");
        var sessionId = initial.GetProperty("sessionId").GetGuid();
        var targetId = initial.GetProperty("targetId").GetGuid();
        Assert.NotEqual(Guid.Empty, sessionId);
        Assert.NotEqual(Guid.Empty, targetId);
        Assert.Equal(sessionId, mesh.GetProperty("sessionId").GetGuid());
        Assert.Equal(sessionId, empty.GetProperty("sessionId").GetGuid());
        Assert.Equal(sessionId, transformed.GetProperty("sessionId").GetGuid());
        Assert.Equal(targetId, mesh.GetProperty("targetId").GetGuid());
        Assert.Equal(targetId, empty.GetProperty("targetId").GetGuid());
        Assert.Equal(targetId, transformed.GetProperty("targetId").GetGuid());
        Assert.Equal(JsonValueKind.Null, initial.GetProperty("authoredMesh").ValueKind);

        var meshRequest = mesh.GetProperty("authoredMesh");
        var emptyRequest = empty.GetProperty("authoredMesh");
        var transformedRequest = transformed.GetProperty("authoredMesh");
        AssertAuthoredMesh(meshRequest, meshObjectId, assetId, TransformValue.Identity);
        AssertAuthoredMesh(emptyRequest, meshObjectId, assetId, TransformValue.Identity);
        AssertAuthoredMesh(
            transformedRequest,
            meshObjectId,
            assetId,
            StudioSceneMeshSmoke.ValidationLocalTransform);
        Assert.Equal(
            meshRequest.GetProperty("runtimeEntityId").GetProperty("index").GetUInt32(),
            transformedRequest.GetProperty("runtimeEntityId").GetProperty("index").GetUInt32());
        Assert.Equal(
            meshRequest.GetProperty("runtimeEntityId").GetProperty("generation").GetUInt32(),
            transformedRequest.GetProperty("runtimeEntityId").GetProperty("generation").GetUInt32());

        var transformedRotation = transformedRequest.GetProperty("transform")
            .GetProperty("rotation");
        var rotationLengthSquared =
            MathF.Pow(transformedRotation.GetProperty("x").GetSingle(), 2.0F) +
            MathF.Pow(transformedRotation.GetProperty("y").GetSingle(), 2.0F) +
            MathF.Pow(transformedRotation.GetProperty("z").GetSingle(), 2.0F) +
            MathF.Pow(transformedRotation.GetProperty("w").GetSingle(), 2.0F);
        Assert.InRange(rotationLengthSquared, 0.999999F, 1.000001F);
        Assert.NotEqual(0.0F, transformedRotation.GetProperty("y").GetSingle());
        var transformedScale = transformedRequest.GetProperty("transform").GetProperty("scale");
        Assert.NotEqual(
            transformedScale.GetProperty("x").GetSingle(),
            transformedScale.GetProperty("y").GetSingle());
        Assert.NotEqual(
            transformedScale.GetProperty("y").GetSingle(),
            transformedScale.GetProperty("z").GetSingle());

        AssertMeshReceipt(meshReceipt, meshObjectId, assetId);
        AssertMeshReceipt(emptyReceipt, meshObjectId, assetId);
        AssertMeshReceipt(transformedReceipt, meshObjectId, assetId);
        AssertMeshReceipt(supersededReceipt, meshObjectId, assetId);
        AssertRequestMatchesReceipt(meshRequest, meshReceipt);
        AssertRequestMatchesReceipt(emptyRequest, emptyReceipt);
        AssertRequestMatchesReceipt(transformedRequest, transformedReceipt);
        Assert.True(supersededReceipt.GetProperty("evidenceAvailable").GetBoolean());
        Assert.Equal(
            "wireframe",
            supersededReceipt.GetProperty("rasterMode").GetString());
        Assert.Equal(
            supersededRevision,
            supersededReceipt.GetProperty("sceneRevision").GetUInt64());
        Assert.Equal(
            meshReceipt.GetProperty("representativeSourceEntityId")
                .GetProperty("index").GetUInt32(),
            emptyReceipt.GetProperty("representativeSourceEntityId")
                .GetProperty("index").GetUInt32());
        Assert.Equal(
            meshReceipt.GetProperty("representativeSourceEntityId")
                .GetProperty("generation").GetUInt32(),
            emptyReceipt.GetProperty("representativeSourceEntityId")
                .GetProperty("generation").GetUInt32());
        Assert.Equal(
            meshReceipt.GetProperty("representativeSourceEntityId")
                .GetProperty("index").GetUInt32(),
            transformedReceipt.GetProperty("representativeSourceEntityId")
                .GetProperty("index").GetUInt32());
        Assert.Equal(
            meshReceipt.GetProperty("representativeSourceEntityId")
                .GetProperty("generation").GetUInt32(),
            transformedReceipt.GetProperty("representativeSourceEntityId")
                .GetProperty("generation").GetUInt32());
        Assert.Equal(
            meshReceipt.GetProperty("representativeSourceEntityId")
                .GetProperty("index").GetUInt32(),
            supersededReceipt.GetProperty("representativeSourceEntityId")
                .GetProperty("index").GetUInt32());
        Assert.Equal(
            meshReceipt.GetProperty("representativeSourceEntityId")
                .GetProperty("generation").GetUInt32(),
            supersededReceipt.GetProperty("representativeSourceEntityId")
                .GetProperty("generation").GetUInt32());

        static void AssertStage(
            JsonElement stage,
            string expectedStage,
            int expectedEntityCount)
        {
            Assert.Equal(expectedStage, stage.GetProperty("stage").GetString());
            Assert.Equal(
                expectedEntityCount,
                stage.GetProperty("documentEntityCount").GetInt32());
            Assert.True(stage.GetProperty("requestSequence").GetUInt64() > 0);
            Assert.Equal(
                stage.GetProperty("requestSequence").GetUInt64(),
                stage.GetProperty("minimumPresentableSequence").GetUInt64());
            Assert.True(stage.GetProperty("frameIndex").GetUInt64() > 0);
            Assert.True(stage.GetProperty("currentSurfaceIsExact").GetBoolean());
            Assert.True(stage.GetProperty("lastPresentationIsExact").GetBoolean());
            var revision = stage.GetProperty("targetRevision").GetUInt64();
            Assert.True(StudioSceneMeshSmoke.IsPresentedRevision(
                stage.GetProperty("presentationStatus").GetString()!,
                revision));
            var receipt = stage.GetProperty("receipt");
            Assert.True(receipt.GetProperty("evidenceAvailable").GetBoolean());
            Assert.Equal("wireframe", receipt.GetProperty("rasterMode").GetString());
            Assert.Equal(revision, receipt.GetProperty("sceneRevision").GetUInt64());
        }

        static void AssertAuthoredMesh(
            JsonElement authoredMesh,
            Guid expectedObjectId,
            Guid expectedAssetId,
            TransformValue expectedTransform)
        {
            Assert.Equal(expectedObjectId, authoredMesh.GetProperty("objectId").GetGuid());
            var runtimeEntityId = authoredMesh.GetProperty("runtimeEntityId");
            Assert.True(runtimeEntityId.GetProperty("index").GetUInt32() > 0);
            Assert.True(runtimeEntityId.GetProperty("generation").GetUInt32() > 0);
            Assert.Equal(expectedAssetId, authoredMesh.GetProperty("assetId").GetGuid());
            Assert.Equal(
                ViewportAuthoredMeshSnapshot.ExpectedMeshType,
                authoredMesh.GetProperty("expectedType").GetUInt64());
            AssertTransform(authoredMesh.GetProperty("transform"), expectedTransform);
        }

        static void AssertTransform(JsonElement transform, TransformValue expected)
        {
            AssertFloat3(transform.GetProperty("position"), expected.Position);
            var rotation = transform.GetProperty("rotation");
            Assert.Equal(expected.Rotation.X, rotation.GetProperty("x").GetSingle());
            Assert.Equal(expected.Rotation.Y, rotation.GetProperty("y").GetSingle());
            Assert.Equal(expected.Rotation.Z, rotation.GetProperty("z").GetSingle());
            Assert.Equal(expected.Rotation.W, rotation.GetProperty("w").GetSingle());
            AssertFloat3(transform.GetProperty("scale"), expected.Scale);
        }

        static void AssertFloat3(JsonElement value, Float3 expected)
        {
            Assert.Equal(expected.X, value.GetProperty("x").GetSingle());
            Assert.Equal(expected.Y, value.GetProperty("y").GetSingle());
            Assert.Equal(expected.Z, value.GetProperty("z").GetSingle());
        }

        static void AssertMeshReceipt(
            JsonElement receipt,
            Guid expectedObjectId,
            Guid expectedAssetId)
        {
            Assert.Equal(1U, receipt.GetProperty("inputCount").GetUInt32());
            Assert.Equal(1U, receipt.GetProperty("resolvedCount").GetUInt32());
            Assert.Equal(0U, receipt.GetProperty("rejectedCount").GetUInt32());
            Assert.Equal(1U, receipt.GetProperty("indexedDrawCount").GetUInt32());
            var source = receipt.GetProperty("representativeSourceEntityId");
            Assert.True(source.GetProperty("index").GetUInt32() > 0);
            Assert.True(source.GetProperty("generation").GetUInt32() > 0);
            Assert.Equal(
                expectedObjectId,
                receipt.GetProperty("representativeObjectId").GetGuid());
            Assert.Equal(
                expectedAssetId,
                receipt.GetProperty("representativeAssetId").GetGuid());
            Assert.Equal(
                StudioSceneMeshSmoke.ValidationMeshResourceKey,
                receipt.GetProperty("meshResourceKey").GetUInt64());
            Assert.Equal(
                StudioSceneMeshSmoke.DefaultUnlitMaterialResourceKey,
                receipt.GetProperty("materialResourceKey").GetUInt64());
            Assert.Equal(
                StudioSceneMeshSmoke.ValidationProductHash,
                receipt.GetProperty("productHash").GetUInt64());
        }

        static void AssertRequestMatchesReceipt(
            JsonElement authoredMesh,
            JsonElement receipt)
        {
            Assert.Equal(
                authoredMesh.GetProperty("objectId").GetGuid(),
                receipt.GetProperty("representativeObjectId").GetGuid());
            Assert.Equal(
                authoredMesh.GetProperty("assetId").GetGuid(),
                receipt.GetProperty("representativeAssetId").GetGuid());
            var requestRuntimeEntityId = authoredMesh.GetProperty("runtimeEntityId");
            var receiptRuntimeEntityId = receipt.GetProperty("representativeSourceEntityId");
            Assert.Equal(
                requestRuntimeEntityId.GetProperty("index").GetUInt32(),
                receiptRuntimeEntityId.GetProperty("index").GetUInt32());
            Assert.Equal(
                requestRuntimeEntityId.GetProperty("generation").GetUInt32(),
                receiptRuntimeEntityId.GetProperty("generation").GetUInt32());
        }
    }

    private static void AssertStructuredResizeEvidence(
        string scenario,
        string output,
        IReadOnlyList<string> arguments)
    {
        const string prefix = "viewport-transaction-resize-evidence ";
        var line = Assert.Single(
            output.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries),
            static candidate => candidate.StartsWith(prefix, StringComparison.Ordinal));
        using var document = JsonDocument.Parse(line[prefix.Length..]);
        var root = document.RootElement;
        var pattern = arguments.Single(argument =>
            argument.StartsWith("--viewport-resize-pattern=", StringComparison.Ordinal))
            ["--viewport-resize-pattern=".Length..];
        Assert.Equal("resize", root.GetProperty("scenario").GetString());
        Assert.Equal(pattern, root.GetProperty("pattern").GetString());

        var input = root.GetProperty("input");
        Assert.True(input.GetProperty("requested").GetInt32() >= 2, scenario);
        Assert.True(input.GetProperty("targetHz").GetDouble() >= 1, scenario);
        Assert.True(input.GetProperty("schedulerAccepted").GetUInt64() > 0, scenario);
        Assert.Equal(0UL, input.GetProperty("activeCancelled").GetUInt64());
        Assert.InRange(input.GetProperty("maximumPending").GetInt32(), 1, 2);

        var rendered = root.GetProperty("rendered");
        Assert.True(rendered.GetProperty("uniqueExact").GetUInt64() > 0, scenario);
        Assert.True(rendered.GetProperty("finalExact").GetBoolean(), scenario);
        Assert.True(rendered.GetProperty("finalRendered").GetBoolean(), scenario);

        var exact = root.GetProperty("exactPhysical");
        Assert.True(exact.GetProperty("noCropOrStretch").GetBoolean(), scenario);
        Assert.Equal(
            exact.GetProperty("panel").GetRawText(),
            exact.GetProperty("visual").GetRawText());
        Assert.Equal(
            exact.GetProperty("panel").GetRawText(),
            exact.GetProperty("surface").GetRawText());

        var zeroExtent = root.GetProperty("zeroExtentRecovery");
        Assert.True(zeroExtent.GetProperty("evidenceAvailable").GetBoolean(), scenario);
        Assert.True(zeroExtent.GetProperty("zeroWidthAndHeightObserved").GetBoolean(), scenario);
        Assert.True(zeroExtent.GetProperty("visualHiddenWhileCollapsed").GetBoolean(), scenario);
        Assert.Equal(1, zeroExtent.GetProperty("visibleConfirmationBatches").GetInt32());
        Assert.Equal(
            zeroExtent.GetProperty("before").GetRawText(),
            zeroExtent.GetProperty("recovered").GetRawText());

        var onePixel = root.GetProperty("onePixelBoundary");
        Assert.True(onePixel.GetProperty("purePolicyEvidenceAvailable").GetBoolean(), scenario);
        Assert.Equal(
            pattern == "pixel",
            onePixel.GetProperty("runtimeInputEvidenceAvailable").GetBoolean());
        Assert.Equal(
            pattern == "pixel",
            onePixel.GetProperty("adjacentPhysicalWidthsDifferByOne").GetBoolean());

        var dpi = root.GetProperty("dpiMatrix");
        Assert.False(
            dpi.GetProperty("realHostScaleInjectionEvidenceAvailable").GetBoolean());
        Assert.True(dpi.GetProperty("purePolicyEvidenceAvailable").GetBoolean(), scenario);
        Assert.Equal(4, dpi.GetProperty("samples").GetArrayLength());
    }

    private static void AssertStructuredFlashEvidence(string output)
    {
        const string summaryPrefix = "viewport-transaction-flash ";
        const string batchPrefix = "viewport-transaction-flash-batch ";
        var lines = output.Split(
            ['\r', '\n'],
            StringSplitOptions.RemoveEmptyEntries);
        var summaryLine = Assert.Single(
            lines,
            static line => line.StartsWith(summaryPrefix + "{", StringComparison.Ordinal));
        using var summary = JsonDocument.Parse(summaryLine[summaryPrefix.Length..]);
        var root = summary.RootElement;
        Assert.Equal("flash-structural", root.GetProperty("scenario").GetString());
        Assert.Equal(
            "transaction-batch-structural",
            root.GetProperty("evidenceKind").GetString());
        Assert.True(root.GetProperty("sentinel").GetProperty("enabled").GetBoolean());
        Assert.Equal(
            "scene-viewport-native-surface",
            root.GetProperty("sentinel").GetProperty("owner").GetString());
        Assert.False(root.GetProperty("pixelEvidenceAvailable").GetBoolean());
        Assert.False(root.GetProperty("physicalDisplayedEvidenceAvailable").GetBoolean());
        Assert.Equal(0, root.GetProperty("structuralEvidence").GetProperty("outOfBounds").GetInt32());
        Assert.Equal(0, root.GetProperty("structuralEvidence").GetProperty("blank").GetInt32());
        Assert.Equal(0, root.GetProperty("structuralEvidence").GetProperty("stretch").GetInt32());
        Assert.Equal(0, root.GetProperty("structuralEvidence").GetProperty("crop").GetInt32());

        var batchLines = lines.Where(static line =>
            line.StartsWith(batchPrefix, StringComparison.Ordinal)).ToArray();
        Assert.NotEmpty(batchLines);
        foreach (var batchLine in batchLines)
        {
            using var batch = JsonDocument.Parse(batchLine[batchPrefix.Length..]);
            var batchRoot = batch.RootElement;
            Assert.True(batchRoot.GetProperty("structurallyExact").GetBoolean());
            Assert.True(batchRoot.TryGetProperty("bounds", out _));
            Assert.True(batchRoot.TryGetProperty("frontExtent", out _));
            Assert.True(batchRoot.TryGetProperty("candidateExtent", out _));
            Assert.True(batchRoot.TryGetProperty("visualSize", out _));
            Assert.True(batchRoot.TryGetProperty("surfaceExtent", out _));
            Assert.True(batchRoot.TryGetProperty("opacity", out _));
            Assert.True(batchRoot.TryGetProperty("endpoint", out _));
            Assert.True(batchRoot.TryGetProperty("session", out _));
            Assert.True(batchRoot.TryGetProperty("epoch", out _));
            Assert.True(batchRoot.TryGetProperty("transaction", out _));
            Assert.True(batchRoot.TryGetProperty("generation", out _));
        }
    }

    private static void AssertWindowResizeEvidence(
        string scenario,
        string output,
        IReadOnlyList<string> arguments)
    {
        const string prefix = "viewport-transaction-window-resize-evidence ";
        const string batchPrefix = "viewport-transaction-window-resize-batch ";
        var lines = output.Split(
            ['\r', '\n'],
            StringSplitOptions.RemoveEmptyEntries);
        var evidenceLine = Assert.Single(
            lines,
            static line => line.StartsWith(prefix, StringComparison.Ordinal));
        using var evidence = JsonDocument.Parse(evidenceLine[prefix.Length..]);
        var root = evidence.RootElement;
        var pattern = ReadRequiredArgument(arguments, "--viewport-window-pattern=");
        var evidenceLane = ReadRequiredArgument(
            arguments,
            StudioViewportTransactionWindowResizeSmoke.EvidenceOptionPrefix);
        var measuresPerformance = evidenceLane == "performance";
        var capturesProjection = pattern == "height-aba";
        var releasePolicy = StudioViewportTransactionWindowResizeSmoke.ParseReleasePolicy(
            arguments);

        Assert.Equal(
            capturesProjection
                ? "window-resize-projection"
                : measuresPerformance
                    ? "window-resize-performance"
                    : "window-resize-structural",
            root.GetProperty("scenario").GetString());
        Assert.Equal("main", root.GetProperty("hostKind").GetString());
        Assert.Equal(pattern, root.GetProperty("pattern").GetString());
        Assert.Equal(
            StudioViewportTransactionWindowResizeSmoke.ReleasePolicyName(releasePolicy),
            root.GetProperty("releasePolicy").GetString());
        Assert.Equal(
            capturesProjection
                ? "scene-horizontal-fov-height-resize"
                : measuresPerformance
                ? "transaction-rendered-performance"
                : "continuous-composition-batch-structural",
            root.GetProperty("evidenceKind").GetString());
        Assert.False(root.GetProperty("pixelEvidenceAvailable").GetBoolean());
        Assert.False(root.GetProperty("physicalDisplayedEvidenceAvailable").GetBoolean());

        var win32 = root.GetProperty("win32");
        Assert.Equal(1, win32.GetProperty("enterSizeMove").GetInt32());
        Assert.Equal(
            win32.GetProperty("sizingRequested").GetInt32(),
            win32.GetProperty("sizingHandled").GetInt32());
        Assert.Equal(1, win32.GetProperty("exitSizeMove").GetInt32());
        Assert.True(win32.GetProperty("finalWindowRectMatches").GetBoolean(), scenario);
        if (releasePolicy == StudioViewportTransactionWindowResizeSmoke
                .WindowResizeReleasePolicy.WaitFinal)
        {
            Assert.True(win32.GetProperty("rawFinalProposalAccepted").GetBoolean());
            Assert.False(win32.GetProperty("rawFinalProposalDropped").GetBoolean());
            Assert.False(win32.GetProperty("pendingRawFinalBeforeExit").GetBoolean());
            Assert.Equal(
                "Committed",
                win32.GetProperty("finalRetirementResult").GetString());
        }
        else
        {
            Assert.False(win32.GetProperty("rawFinalProposalAccepted").GetBoolean());
            Assert.True(win32.GetProperty("rawFinalProposalDropped").GetBoolean());
            Assert.True(win32.GetProperty("pendingRawFinalBeforeExit").GetBoolean());
            Assert.Equal(
                "Cancelled",
                win32.GetProperty("finalRetirementResult").GetString());
        }
        Assert.Equal(JsonValueKind.Object, win32.GetProperty("rawFinalProposal").ValueKind);
        Assert.Equal(JsonValueKind.Object, win32.GetProperty("acceptedFinal").ValueKind);
        Assert.Equal(JsonValueKind.Object, win32.GetProperty("rawProposalLagPx").ValueKind);

        var performanceWindow = root.GetProperty("performanceWindow");
        var batches = root.GetProperty("compositionBatches");
        if (measuresPerformance)
        {
            Assert.Equal(JsonValueKind.Null, batches.ValueKind);
            Assert.Equal(JsonValueKind.Object, performanceWindow.ValueKind);
            Assert.True(
                performanceWindow.GetProperty("firstProposedQpc").GetInt64() <
                performanceWindow.GetProperty("finalExactRenderedQpc").GetInt64(),
                scenario);
            Assert.True(performanceWindow.GetProperty("durationMs").GetDouble() > 0);
            Assert.True(performanceWindow.GetProperty("uniqueExactRendered").GetInt32() > 0);
            Assert.True(
                performanceWindow.GetProperty("rate").GetDouble() >=
                performanceWindow.GetProperty("minimumRate").GetDouble(),
                scenario);
        }
        else
        {
            Assert.Equal(JsonValueKind.Null, performanceWindow.ValueKind);
            Assert.Equal(JsonValueKind.Object, batches.ValueKind);
            Assert.True(batches.GetProperty("observed").GetInt32() > 0, scenario);
            Assert.Equal(0, batches.GetProperty("invalid").GetInt32());
            Assert.Equal(0, batches.GetProperty("blank").GetInt32());
            Assert.Equal(0, batches.GetProperty("stretch").GetInt32());
            Assert.Equal(0, batches.GetProperty("crop").GetInt32());
            Assert.Equal(0, batches.GetProperty("gap").GetInt32());
            Assert.Equal(0, batches.GetProperty("extentMismatch").GetInt32());
            Assert.Equal(0, batches.GetProperty("clientMismatch").GetInt32());
        }

        var projection = root.GetProperty("projection");
        if (!capturesProjection)
        {
            Assert.Equal(JsonValueKind.Null, projection.ValueKind);
        }
        else
        {
            Assert.Equal(JsonValueKind.Object, projection.ValueKind);
            Assert.True(projection.GetProperty("evidenceAvailable").GetBoolean());
            Assert.Equal(
                "MaintainHorizontal",
                projection.GetProperty("axis").GetString());
            Assert.InRange(
                Math.Abs(
                    projection.GetProperty("fieldOfViewRadians").GetDouble() -
                    Math.PI / 2d),
                0,
                1.0e-5);
            var targetRevision = projection.GetProperty("targetRevision").GetUInt64();
            Assert.True(targetRevision > 0);

            var fixedWidth = projection.GetProperty("fixedWidth");
            Assert.True(fixedWidth.GetProperty("proposed").GetBoolean());
            Assert.True(fixedWidth.GetProperty("rendered").GetBoolean());
            Assert.True(fixedWidth.GetProperty("windowPixels").GetInt32() > 0);
            Assert.True(fixedWidth.GetProperty("clientPixels").GetInt32() > 0);
            Assert.True(fixedWidth.GetProperty("sceneLogical").GetDouble() > 0);
            var fixedSurfaceWidth = fixedWidth.GetProperty("surfacePixels").GetUInt32();
            Assert.True(fixedSurfaceWidth > 0);

            var requests = projection.GetProperty("requests")
                .EnumerateArray()
                .ToArray();
            Assert.True(requests.Length >= 3, scenario);
            var baselineCamera = requests[0].GetProperty("camera").GetRawText();
            var requestSequences = new HashSet<ulong>();
            var previousSequence = 0UL;
            foreach (var request in requests)
            {
                var sequence = request.GetProperty("sequence").GetUInt64();
                Assert.True(sequence > previousSequence, scenario);
                previousSequence = sequence;
                Assert.True(requestSequences.Add(sequence));
                Assert.Equal(targetRevision, request.GetProperty("targetRevision").GetUInt64());
                Assert.Equal("Scene", request.GetProperty("kind").GetString());
                Assert.NotEqual(Guid.Empty, request.GetProperty("session").GetGuid());
                var logicalExtent = request.GetProperty("logicalExtent");
                var allocationExtent = request.GetProperty("allocationExtent");
                Assert.Equal(
                    allocationExtent.GetProperty("width").GetUInt32(),
                    logicalExtent.GetProperty("width").GetUInt32());
                Assert.Equal(
                    allocationExtent.GetProperty("height").GetUInt32(),
                    logicalExtent.GetProperty("height").GetUInt32());
                Assert.Equal(
                    fixedSurfaceWidth,
                    allocationExtent.GetProperty("width").GetUInt32());
                var camera = request.GetProperty("camera");
                Assert.Equal(baselineCamera, camera.GetRawText());
                Assert.Equal(
                    "MaintainHorizontal",
                    camera.GetProperty("fieldOfViewAxis").GetString());
                Assert.InRange(
                    Math.Abs(camera.GetProperty("fieldOfViewRadians").GetDouble() -
                             Math.PI / 2d),
                    0,
                    1.0e-5);
            }

            var leases = projection.GetProperty("leases")
                .EnumerateArray()
                .ToArray();
            var rendered = projection.GetProperty("rendered");
            Assert.True(rendered.GetProperty("distinctExactHeights").GetInt32() >= 2);
            var scaleTolerance = rendered.GetProperty("tolerancePixels").GetDouble();
            Assert.True(scaleTolerance > 0);
            Assert.InRange(
                rendered.GetProperty("maximumPixelScaleDelta").GetDouble(),
                0,
                scaleTolerance);
            var samples = rendered.GetProperty("samples")
                .EnumerateArray()
                .ToArray();
            Assert.True(samples.Length >= 2, scenario);
            Assert.True(
                samples.Select(sample => sample.GetProperty("extent")
                        .GetProperty("height").GetUInt32())
                    .Distinct()
                    .Count() >= 2,
                scenario);
            var baselinePixelScale = samples[0].GetProperty("xPixelScale").GetDouble();
            foreach (var sample in samples)
            {
                var sequence = sample.GetProperty("requestSequence").GetUInt64();
                Assert.Contains(sequence, requestSequences);
                Assert.Equal(targetRevision, sample.GetProperty("targetRevision").GetUInt64());
                Assert.True(sample.GetProperty("geometryGeneration").GetUInt64() > 0);
                var extent = sample.GetProperty("extent");
                Assert.Equal(fixedSurfaceWidth, extent.GetProperty("width").GetUInt32());
                Assert.InRange(
                    sample.GetProperty("derivedVerticalFovRadians").GetDouble(),
                    0,
                    Math.PI);
                var xPixelScale = sample.GetProperty("xPixelScale").GetDouble();
                var yPixelScale = sample.GetProperty("yPixelScale").GetDouble();
                Assert.InRange(Math.Abs(xPixelScale - baselinePixelScale), 0, scaleTolerance);
                Assert.InRange(Math.Abs(yPixelScale - baselinePixelScale), 0, scaleTolerance);

                var matchingLease = Assert.Single(
                    leases,
                    lease => lease.GetProperty("requestSequence").GetUInt64() == sequence);
                Assert.Equal(
                    targetRevision,
                    matchingLease.GetProperty("targetRevision").GetUInt64());
                Assert.Equal(
                    extent.GetProperty("width").GetUInt32(),
                    matchingLease.GetProperty("allocationExtent")
                        .GetProperty("width").GetUInt32());
                Assert.Equal(
                    extent.GetProperty("height").GetUInt32(),
                    matchingLease.GetProperty("allocationExtent")
                        .GetProperty("height").GetUInt32());
            }

            var release = projection.GetProperty("release");
            var inputStartedQpc = release.GetProperty("inputStartedQpc").GetInt64();
            var finalRequestSentQpc = release.GetProperty("finalRequestSentQpc").GetInt64();
            var exitSizeMoveQpc = release.GetProperty("exitSizeMoveQpc").GetInt64();
            var endedQpc = release.GetProperty("endedQpc").GetInt64();
            Assert.True(inputStartedQpc < finalRequestSentQpc);
            Assert.True(finalRequestSentQpc <= exitSizeMoveQpc);
            Assert.True(exitSizeMoveQpc < endedQpc);
            var finalProjectionRequestSequence = release
                .GetProperty("finalProjectionRequestSequence")
                .GetUInt64();
            Assert.True(
                release.GetProperty("initialPresentedSequence").GetUInt64() <
                finalProjectionRequestSequence);
            var finalProjectionRequest = Assert.Single(
                requests,
                request => request.GetProperty("sequence").GetUInt64() ==
                    finalProjectionRequestSequence);
            Assert.True(
                finalProjectionRequest.GetProperty("timestampQpc").GetInt64() <
                exitSizeMoveQpc);
            Assert.All(
                requests,
                request => Assert.True(
                    request.GetProperty("timestampQpc").GetInt64() < exitSizeMoveQpc));
            Assert.Equal(1, release.GetProperty("finalExactRequestCount").GetInt32());
            Assert.Equal(0, release.GetProperty("postReleaseRequestCount").GetInt32());
            Assert.Equal(
                0,
                release.GetProperty("postReleaseProjectionMutationCount").GetInt32());
            Assert.True(release.GetProperty("cameraIsDefaultScene").GetBoolean());
            Assert.True(release.GetProperty("finalPresentedSequenceMatches").GetBoolean());
        }

        var final = root.GetProperty("final");
        Assert.True(final.GetProperty("exact").GetBoolean(), scenario);
        Assert.True(final.GetProperty("rendered").GetBoolean(), scenario);
        Assert.True(final.GetProperty("structurallyExact").GetBoolean(), scenario);
        Assert.Equal(JsonValueKind.Object, final.GetProperty("accepted").ValueKind);
        Assert.Equal(JsonValueKind.Object, final.GetProperty("rawProposalLag").ValueKind);
        Assert.Equal(JsonValueKind.Object, final.GetProperty("candidateLag").ValueKind);
        Assert.InRange(
            final.GetProperty("catchUpBatches").GetInt32(),
            final.GetProperty("minimumCatchUpBatches").GetInt32(),
            final.GetProperty("maximumCatchUpBatches").GetInt32());
        if (measuresPerformance)
        {
            Assert.Equal(
                JsonValueKind.Null,
                final.GetProperty("continuousCompositionBatches").ValueKind);
        }
        else
        {
            Assert.True(
                final.GetProperty("continuousCompositionBatches").GetInt32() >=
                final.GetProperty("catchUpBatches").GetInt32(),
                scenario);
        }
        Assert.True(final.GetProperty("catchUpElapsedMs").GetDouble() >= 0, scenario);
        Assert.True(
            final.GetProperty("catchUpElapsedMs").GetDouble() <=
                final.GetProperty("maximumCatchUp60HzBudgetMs").GetDouble(),
            scenario);

        if (pattern is "aba" or "height-aba")
        {
            Assert.True(root.GetProperty("renderedNonOrigin").GetBoolean(), scenario);
        }
        var visibility = root.GetProperty("visibility");
        Assert.Equal(0, visibility.GetProperty("hiddenDurationMs").GetDouble());
        Assert.Equal(0, visibility.GetProperty("hiddenDuty").GetDouble());

        var batchLines = lines.Where(static line =>
            line.StartsWith(batchPrefix, StringComparison.Ordinal)).ToArray();
        if (measuresPerformance)
        {
            Assert.Empty(batchLines);
        }
        else
        {
            Assert.NotEmpty(batchLines);
        }
        foreach (var batchLine in batchLines)
        {
            using var batch = JsonDocument.Parse(batchLine[batchPrefix.Length..]);
            var batchRoot = batch.RootElement;
            Assert.True(batchRoot.GetProperty("structurallyExact").GetBoolean(), scenario);
            Assert.False(batchRoot.GetProperty("blank").GetBoolean(), scenario);
            Assert.False(batchRoot.GetProperty("stretch").GetBoolean(), scenario);
            Assert.False(batchRoot.GetProperty("crop").GetBoolean(), scenario);
            Assert.False(batchRoot.GetProperty("gap").GetBoolean(), scenario);
            Assert.False(batchRoot.GetProperty("extentMismatch").GetBoolean(), scenario);
            Assert.False(batchRoot.GetProperty("clientMismatch").GetBoolean(), scenario);
            if (capturesProjection)
            {
                Assert.True(
                    batchRoot.GetProperty("scene")
                        .GetProperty("lastPresentedSequence")
                        .GetUInt64() > 0,
                    scenario);
            }
        }
    }

    private static void AssertStructuredTransactionSummary(
        StudioTransactionSmokeContract contract,
        string output)
    {
        const string prefix = "viewport-transaction-metrics ";
        var summaries = output.Split(
                ['\r', '\n'],
                StringSplitOptions.RemoveEmptyEntries)
            .Where(static line => line.StartsWith(prefix, StringComparison.Ordinal))
            .ToArray();
        var summary = Assert.Single(summaries);
        using var document = JsonDocument.Parse(summary[prefix.Length..]);
        var root = document.RootElement;
        Assert.Equal(JsonValueKind.Object, root.ValueKind);
        Assert.Equal(
            contract.StructuredScenario,
            root.GetProperty("scenario").GetString());
        Assert.True(root.TryGetProperty("uniquePublished", out _), contract.CaseScenario);
        Assert.True(root.TryGetProperty("uniqueRendered", out _), contract.CaseScenario);
        Assert.True(root.TryGetProperty("proposedToRendered", out _), contract.CaseScenario);
        Assert.True(root.TryGetProperty("requestedHiddenDuty", out _), contract.CaseScenario);
        Assert.True(root.TryGetProperty("participantOutcomeEvents", out _), contract.CaseScenario);
        Assert.True(root.TryGetProperty("candidates", out _), contract.CaseScenario);
        Assert.True(root.TryGetProperty("resources", out _), contract.CaseScenario);

        var stages = root.GetProperty("stages");
        var physicalDisplay = root.GetProperty("physicalDisplay");
        if (contract.CaseFamily == "flash-structural")
        {
            Assert.Equal(0UL, stages.GetProperty("physicalDisplayed").GetUInt64());
            Assert.False(physicalDisplay.GetProperty("evidenceAvailable").GetBoolean());
            Assert.Equal(0, physicalDisplay.GetProperty("observed").GetInt32());
        }
    }

    internal sealed record StudioTransactionSmokeContract(
        string CaseScenario,
        string CaseFamily,
        string StructuredScenario,
        string PassMarker);

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
