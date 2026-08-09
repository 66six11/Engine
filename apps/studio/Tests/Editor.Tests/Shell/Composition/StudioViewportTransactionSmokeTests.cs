using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class StudioViewportTransactionSmokeTests
{
    [Fact]
    public void Router_recognizes_flash_smoke()
    {
        Assert.True(Editor.Shell.Composition.StudioViewportTransactionSmoke.IsRequested(
            [Editor.Shell.Composition.StudioViewportTransactionFlashSmoke.CommandLineSwitch]));
    }

    [Fact]
    public void Router_recognizes_fault_smoke()
    {
        Assert.True(Editor.Shell.Composition.StudioViewportTransactionSmoke.IsRequested(
            [Editor.Shell.Composition.StudioViewportTransactionFaultSmoke.CommandLineSwitch]));
    }

    [Fact]
    public void Router_recognizes_multi_endpoint_smoke()
    {
        Assert.True(Editor.Shell.Composition.StudioViewportTransactionSmoke.IsRequested(
            [Editor.Shell.Composition.StudioViewportMultiEndpointSmoke.CommandLineSwitch]));
    }

    [Fact]
    public async Task Multi_endpoint_host_creates_distinct_sessions_for_one_document()
    {
        await using var host = new Editor.Shell.Composition.StudioViewportSmokeHost();

        var (first, second) = host.CreateSceneSessionPair("shared.scene.json");

        Assert.NotEqual(first.Current.SessionId, second.Current.SessionId);
        Assert.Equal(first.Current.TargetId, second.Current.TargetId);
        Assert.Equal(first.Current.TargetRevision, second.Current.TargetRevision);
    }

    [Fact]
    public async Task Multi_endpoint_host_keeps_scene_and_game_session_owners_distinct()
    {
        await using var host = new Editor.Shell.Composition.StudioViewportSmokeHost();

        var (scene, game) = host.CreateSceneGameSessionPair("shared.scene.json");

        Assert.Equal(
            Asharia.Studio.Application.Viewports.ViewportRenderKind.Scene,
            scene.Current.Kind);
        Assert.Equal(
            Asharia.Studio.Application.Viewports.ViewportRenderKind.Game,
            game.Current.Kind);
        Assert.NotEqual(scene.Current.SessionId, game.Current.SessionId);
        Assert.Equal(scene.Current.TargetId, game.Current.TargetId);
    }

    [Fact]
    public void Resize_directional_patterns_are_monotonic_from_the_committed_width()
    {
        const double origin = 640;

        var grow = Editor.Shell.Composition.StudioViewportResizeStimulus.Build(
            "grow",
            90,
            origin);
        var shrink = Editor.Shell.Composition.StudioViewportResizeStimulus.Build(
            "shrink",
            90,
            origin);

        Assert.True(grow[0] > origin);
        Assert.All(grow.Zip(grow.Skip(1)), pair => Assert.True(pair.First < pair.Second));
        Assert.True(shrink[0] < origin);
        Assert.All(shrink.Zip(shrink.Skip(1)), pair => Assert.True(pair.First > pair.Second));
    }

    [Fact]
    public void Resize_aba_sawtooth_and_jitter_patterns_preserve_their_distinct_shape()
    {
        const double origin = 640;
        var aba = Editor.Shell.Composition.StudioViewportResizeStimulus.Build(
            "aba",
            91,
            origin);
        var sawtooth = Editor.Shell.Composition.StudioViewportResizeStimulus.Build(
            "sawtooth",
            90,
            origin);
        var jitter = Editor.Shell.Composition.StudioViewportResizeStimulus.Build(
            "jitter",
            90,
            origin);
        var repeatedJitter = Editor.Shell.Composition.StudioViewportResizeStimulus.Build(
            "jitter",
            90,
            origin);

        Assert.Equal(origin, aba[0]);
        Assert.Equal(origin, aba[^1]);
        Assert.True(aba.Max() > origin);
        Assert.Contains(sawtooth.Zip(sawtooth.Skip(1)), pair => pair.First < pair.Second);
        Assert.Contains(sawtooth.Zip(sawtooth.Skip(1)), pair => pair.First > pair.Second);
        Assert.Equal(jitter, repeatedJitter);
        Assert.True(jitter.Min() < origin);
        Assert.True(jitter.Max() > origin);
    }

    [Fact]
    public void Resize_pixel_pattern_crosses_one_physical_pixel_boundary()
    {
        const double scaling = 1.25;
        var widths = Editor.Shell.Composition.StudioViewportResizeStimulus.Build(
            "pixel",
            90,
            originWidth: 640,
            scaling);
        var physicalWidths = widths.Select(width =>
        {
            Assert.True(ViewportPhysicalExtentPolicy.TryCalculate(
                width,
                logicalHeight: 1 / scaling,
                scaling,
                out var extent));
            return extent.Width;
        }).ToArray();

        Assert.Equal(physicalWidths.Length, physicalWidths.Distinct().Count());
        Assert.All(
            physicalWidths.Zip(physicalWidths.Skip(1)),
            pair => Assert.Equal(1, Math.Abs((long)pair.First - pair.Second)));
    }

    [Fact]
    public void Gpu_acceptance_data_contains_the_full_resize_pattern_and_rate_matrix()
    {
        var scenarios = Editor.Tests.Shell.Composition.StudioProcessAcceptanceTests
            .ViewportTransactionSmokeCases()
            .Select(testCase => Assert.IsType<string>(testCase[0]))
            .ToHashSet(StringComparer.Ordinal);

        foreach (var pattern in new[] { "grow", "shrink", "aba", "sawtooth", "jitter" })
        {
            foreach (var rate in new[] { 30, 60, 120, 240 })
            {
                Assert.Contains($"resize-{pattern}-{rate}hz", scenarios);
            }
        }
        Assert.Contains("resize-pixel-boundary-120hz", scenarios);
    }

    [Fact]
    public void Gpu_acceptance_gate_requires_the_explicit_environment_opt_in()
    {
        var missing = StudioGpuAcceptanceGate.ResolveSkipReason(
            isWindows: true,
            optInValue: null);
        var disabled = StudioGpuAcceptanceGate.ResolveSkipReason(
            isWindows: true,
            optInValue: "0");
        var enabled = StudioGpuAcceptanceGate.ResolveSkipReason(
            isWindows: true,
            optInValue: "1");
        var unsupported = StudioGpuAcceptanceGate.ResolveSkipReason(
            isWindows: false,
            optInValue: "1");

        Assert.Contains(StudioGpuAcceptanceGate.EnvironmentVariable, missing);
        Assert.Contains(StudioGpuAcceptanceGate.EnvironmentVariable, disabled);
        Assert.Null(enabled);
        Assert.Contains("Windows", unsupported);
    }

    [Fact]
    public void Gpu_acceptance_methods_use_xunit_discovery_time_skip_attributes()
    {
        var testType = typeof(StudioProcessAcceptanceTests);
        var steady = testType.GetMethod(
            nameof(StudioProcessAcceptanceTests
                .Realtime_scene_viewport_sustains_at_least_60_surface_updates_per_second));
        var family = testType.GetMethod(
            nameof(StudioProcessAcceptanceTests
                .Viewport_transaction_smoke_family_runs_at_the_real_Studio_boundary));

        Assert.NotNull(steady);
        Assert.NotNull(family);
        Assert.IsType<StudioGpuFactAttribute>(
            Assert.Single(steady.GetCustomAttributes(typeof(StudioGpuFactAttribute), true)));
        Assert.IsType<StudioGpuTheoryAttribute>(
            Assert.Single(family.GetCustomAttributes(typeof(StudioGpuTheoryAttribute), true)));
    }

    [Fact]
    public void Gpu_acceptance_case_names_and_structured_scenarios_map_to_one_family()
    {
        var families = new HashSet<string>(StringComparer.Ordinal)
        {
            "resize",
            "overload",
            "faults",
            "supersede",
            "multi-endpoint",
            "flash-structural",
        };

        foreach (var testCase in StudioProcessAcceptanceTests.ViewportTransactionSmokeCases())
        {
            var caseScenario = Assert.IsType<string>(testCase[0]);
            var arguments = Assert.IsType<string[]>(testCase[2]);
            var contract = StudioProcessAcceptanceTests
                .ResolveTransactionSmokeContract(arguments);

            Assert.Equal(caseScenario, contract.CaseScenario);
            Assert.Contains(contract.CaseFamily, families);
            Assert.False(string.IsNullOrWhiteSpace(contract.StructuredScenario));
            Assert.False(string.IsNullOrWhiteSpace(contract.PassMarker));
        }

        var flash = StudioProcessAcceptanceTests.ViewportTransactionSmokeCases()
            .Single(testCase => Assert.IsType<string>(testCase[0]) == "flash-structural");
        var flashContract = StudioProcessAcceptanceTests.ResolveTransactionSmokeContract(
            Assert.IsType<string[]>(flash[2]));
        Assert.Equal("flash-structural", flashContract.CaseFamily);
        Assert.Equal("flash-structural", flashContract.StructuredScenario);
    }

    [Fact]
    public void Gpu_acceptance_contract_rejects_ambiguous_smoke_routing()
    {
        Assert.Throws<ArgumentException>(() =>
            StudioProcessAcceptanceTests.ResolveTransactionSmokeContract(
            [
                Editor.Shell.Composition.StudioViewportTransactionResizeSmoke
                    .CommandLineSwitch,
                Editor.Shell.Composition.StudioViewportTransactionOverloadSmoke
                    .CommandLineSwitch,
            ]));
    }
}
