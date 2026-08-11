using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class StudioViewportTransactionSmokeTests
{
    [Fact]
    public void Router_recognizes_scene_mesh_smoke()
    {
        Assert.True(Editor.Shell.Composition.StudioViewportTransactionSmoke.IsRequested(
            [Editor.Shell.Composition.StudioSceneMeshSmoke.CommandLineSwitch]));
    }

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
    public void Router_recognizes_window_resize_smoke()
    {
        Assert.True(Editor.Shell.Composition.StudioViewportTransactionSmoke.IsRequested(
            [
                Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                    .CommandLineSwitch,
                Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                    .ObserverReadyEventOptionPrefix + "Local\\Asharia.Studio.Wgc.test",
                Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                    .ReleasePolicyOptionPrefix + "immediate-exit",
            ]));
    }

    [Fact]
    public void Window_resize_observer_ready_event_is_optional_and_unique()
    {
        const string eventName = "Local\\Asharia.Studio.Wgc.unique";
        var prefix = Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .ObserverReadyEventOptionPrefix;

        Assert.Null(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .ParseObserverReadyEventName([]));
        Assert.Equal(
            eventName,
            Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                .ParseObserverReadyEventName([$"{prefix}{eventName}"]));
        Assert.Throws<ArgumentException>(() =>
            Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                .ParseObserverReadyEventName([$"{prefix}{eventName}", $"{prefix}{eventName}"]));
        Assert.Throws<ArgumentException>(() =>
            Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                .ParseObserverReadyEventName([prefix]));
    }

    [Fact]
    public void Window_resize_release_policy_defaults_to_wait_final_and_accepts_immediate_exit()
    {
        var prefix = Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .ReleasePolicyOptionPrefix;

        Assert.Equal(
            Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                .WindowResizeReleasePolicy.WaitFinal,
            Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                .ParseReleasePolicy([]));
        Assert.Equal(
            Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                .WindowResizeReleasePolicy.ImmediateExit,
            Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                .ParseReleasePolicy([$"{prefix}immediate-exit"]));
        Assert.Throws<ArgumentException>(() =>
            Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                .ParseReleasePolicy([$"{prefix}unknown"]));
        Assert.Throws<ArgumentException>(() =>
            Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                .ParseReleasePolicy(
                    [$"{prefix}wait-final", $"{prefix}immediate-exit"]));
    }

    [Fact]
    public void Immediate_exit_uses_the_accepted_commit_and_allows_zero_final_catch_up()
    {
        var rawRequested = new Size(1200, 800);
        var acceptedCommitted = new Size(1170, 793);
        var immediate = Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .WindowResizeReleasePolicy.ImmediateExit;
        var waitFinal = Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .WindowResizeReleasePolicy.WaitFinal;

        Assert.Equal(
            acceptedCommitted,
            Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                .ResolveFinalTruthSize(immediate, rawRequested, acceptedCommitted));
        Assert.Equal(
            rawRequested,
            Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
                .ResolveFinalTruthSize(waitFinal, rawRequested, acceptedCommitted));
        Assert.True(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsFinalCatchUpAccepted(immediate, 0));
        Assert.False(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsFinalCatchUpAccepted(waitFinal, 0));
        Assert.True(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsFinalCatchUpAccepted(waitFinal, 1));
        Assert.False(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsFinalCatchUpAccepted(immediate, 3));
        Assert.True(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsFinalRetirementAccepted(
                immediate,
                ViewportPresentationTransactionResult.Cancelled));
        Assert.False(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsFinalRetirementAccepted(
                immediate,
                ViewportPresentationTransactionResult.Committed));
        Assert.True(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsHostFailureCountAccepted(
                immediate,
                ViewportPresentationTransactionResult.Cancelled,
                failedRequests: 1));
        Assert.False(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsHostFailureCountAccepted(
                immediate,
                ViewportPresentationTransactionResult.Cancelled,
                failedRequests: 0));
        Assert.False(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsFinalRetirementAccepted(
                waitFinal,
                ViewportPresentationTransactionResult.Cancelled));
        Assert.True(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsFinalRetirementAccepted(
                waitFinal,
                ViewportPresentationTransactionResult.Committed));
        Assert.True(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsHostFailureCountAccepted(
                waitFinal,
                ViewportPresentationTransactionResult.Committed,
                failedRequests: 0));
        Assert.True(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsRawFinalProposalDropAccepted(
                immediate,
                pendingRawFinalBeforeExit: true,
                rawFinalProposalAccepted: false,
                ViewportPresentationTransactionResult.Cancelled));
        Assert.False(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsRawFinalProposalDropAccepted(
                immediate,
                pendingRawFinalBeforeExit: false,
                rawFinalProposalAccepted: false,
                ViewportPresentationTransactionResult.Cancelled));
        Assert.False(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsRawFinalProposalDropAccepted(
                immediate,
                pendingRawFinalBeforeExit: true,
                rawFinalProposalAccepted: true,
                ViewportPresentationTransactionResult.Cancelled));
        Assert.False(Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .IsRawFinalProposalDropAccepted(
                immediate,
                pendingRawFinalBeforeExit: true,
                rawFinalProposalAccepted: false,
                ViewportPresentationTransactionResult.Committed));
    }

    [Fact]
    public void Raw_final_proposal_lag_reports_signed_physical_extent_delta()
    {
        var raw = new Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .NativeRect
        {
            Left = 100,
            Top = 80,
            Right = 1740,
            Bottom = 890,
        };
        var accepted = new Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .NativeRect
        {
            Left = 100,
            Top = 80,
            Right = 1710,
            Bottom = 883,
        };

        var lag = Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .CalculateRawFinalProposalLag(raw, accepted);

        Assert.Equal(30, lag.Width);
        Assert.Equal(7, lag.Height);
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
        var renderSize = new ViewportRenderSize(
            new ViewportExtent(1280, 720),
            new ViewportExtent(1280, 720));
        Assert.True(scene.TryPublishLatest(renderSize, out var sceneRequest));
        Assert.True(game.TryPublishLatest(renderSize, out var gameRequest));
        Assert.Equal(
            ViewportFieldOfViewAxis.MaintainHorizontal,
            sceneRequest.Camera.FieldOfViewAxis);
        Assert.Equal(MathF.PI / 2, sceneRequest.Camera.FieldOfViewRadians);
        Assert.Equal(
            ViewportFieldOfViewAxis.MaintainVertical,
            gameRequest.Camera.FieldOfViewAxis);
        Assert.Equal(MathF.PI / 3, gameRequest.Camera.FieldOfViewRadians);
        Assert.NotEqual(scene.Current.SessionId, game.Current.SessionId);
        Assert.Equal(scene.Current.TargetId, game.Current.TargetId);
    }

    [Fact]
    public async Task Smoke_host_can_create_a_scene_session_for_an_authoritative_document()
    {
        await using var host = new Editor.Shell.Composition.StudioViewportSmokeHost();
        var document = new SceneDocumentSnapshot(
            Guid.NewGuid(),
            "authoritative.scene.json",
            revision: 7,
            savedRevision: 5,
            entities: []);

        var session = host.CreateSceneSession(document);

        Assert.Equal(document.SceneId, session.Current.TargetId);
        Assert.Equal(document.Revision, session.Current.TargetRevision);
        Assert.Equal(ViewportRenderKind.Scene, session.Current.Kind);
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

    [Theory]
    [InlineData(30, false)]
    [InlineData(60, false)]
    [InlineData(120, true)]
    [InlineData(240, true)]
    public void Resize_completion_interval_capacity_gate_requires_overdrive_input(
        double inputHz,
        bool expected)
    {
        Assert.Equal(
            expected,
            Editor.Shell.Composition.StudioViewportTransactionResizeSmoke
                .ShouldGateUniqueCompletionP95(inputHz));
    }

    [Fact]
    public void Window_resize_rect_patterns_cover_grow_shrink_and_a_b_a()
    {
        var initial = new Editor.Shell.Composition
            .StudioViewportTransactionWindowResizeSmoke.NativeRect
        {
            Left = 100,
            Top = 80,
            Right = 1380,
            Bottom = 800,
        };

        var grow = Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .BuildProposedRects("grow", 90, initial, renderScaling: 1.25);
        var shrink = Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .BuildProposedRects("shrink", 90, initial, renderScaling: 1.25);
        var aba = Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .BuildProposedRects("aba", 91, initial, renderScaling: 1.25);
        var heightAba = Editor.Shell.Composition.StudioViewportTransactionWindowResizeSmoke
            .BuildProposedRects("height-aba", 13, initial, renderScaling: 1.25);

        Assert.All(grow, rectangle =>
        {
            Assert.Equal(initial.Left, rectangle.Left);
            Assert.Equal(initial.Top, rectangle.Top);
        });
        Assert.True(grow[0].Width > initial.Width);
        Assert.All(
            grow.Zip(grow.Skip(1)),
            pair => Assert.True(pair.First.Width < pair.Second.Width));
        Assert.True(shrink[0].Width < initial.Width);
        Assert.All(
            shrink.Zip(shrink.Skip(1)),
            pair => Assert.True(pair.First.Width > pair.Second.Width));
        Assert.Equal(initial, aba[0]);
        Assert.Equal(initial, aba[^1]);
        Assert.Contains(aba, rectangle => rectangle.Width > initial.Width);
        Assert.Equal(initial, heightAba[0]);
        Assert.Equal(initial, heightAba[^1]);
        Assert.Contains(heightAba, rectangle => rectangle.Height > initial.Height);
        Assert.All(heightAba, rectangle =>
        {
            Assert.Equal(initial.Left, rectangle.Left);
            Assert.Equal(initial.Top, rectangle.Top);
            Assert.Equal(initial.Right, rectangle.Right);
            Assert.Equal(initial.Width, rectangle.Width);
        });
    }

    [Fact]
    public void Window_resize_catch_up_counts_post_request_published_rendered_identities()
    {
        var sessionId = ViewportSessionId.Create();
        var endpointId = new ViewportPresentationEndpointId("scene");
        var extent = new ViewportExtent(1280, 720);
        var otherExtent = new ViewportExtent(1281, 720);
        ViewportPresentationTelemetryEvent Event(
            ViewportPresentationTelemetryEventKind kind,
            long timestamp,
            ulong transaction,
            ulong generation,
            ViewportExtent? eventExtent = null) =>
            new(
                kind,
                timestamp,
                new ViewportPresentationTelemetryIdentity(
                    endpointId,
                    sessionId,
                    Epoch: 1,
                    new ViewportPresentationTransactionId(transaction),
                    generation,
                    eventExtent ?? extent));
        var events = new[]
        {
            Event(ViewportPresentationTelemetryEventKind.Published, 99, 1, 1),
            Event(ViewportPresentationTelemetryEventKind.Rendered, 105, 1, 1),
            Event(ViewportPresentationTelemetryEventKind.Published, 100, 2, 2),
            Event(ViewportPresentationTelemetryEventKind.Rendered, 101, 2, 2),
            Event(ViewportPresentationTelemetryEventKind.Rendered, 102, 2, 2),
            Event(ViewportPresentationTelemetryEventKind.Published, 110, 3, 3),
            Event(ViewportPresentationTelemetryEventKind.Rendered, 120, 3, 3),
            Event(ViewportPresentationTelemetryEventKind.Published, 105, 4, 4),
            Event(ViewportPresentationTelemetryEventKind.Rendered, 106, 4, 4, otherExtent),
            Event(ViewportPresentationTelemetryEventKind.Published, 121, 5, 5),
            Event(ViewportPresentationTelemetryEventKind.Rendered, 122, 5, 5),
        };

        var count = Editor.Shell.Composition
            .StudioViewportTransactionWindowResizeSmoke
            .CountDistinctPublishedRenderedTransactions(
                events,
                startedAt: 100,
                completedAt: 120);

        Assert.Equal(2, count);
    }

    [Fact]
    public void Window_resize_performance_uses_first_proposed_to_final_exact_rendered()
    {
        var sessionId = ViewportSessionId.Create();
        var endpointId = new ViewportPresentationEndpointId("scene");
        var extent = new ViewportExtent(1280, 720);
        ViewportPresentationTelemetryEvent Event(
            ViewportPresentationTelemetryEventKind kind,
            long timestamp,
            ulong transaction,
            ulong generation) =>
            new(
                kind,
                timestamp,
                new ViewportPresentationTelemetryIdentity(
                    endpointId,
                    sessionId,
                    Epoch: 1,
                    new ViewportPresentationTransactionId(transaction),
                    generation,
                    extent));
        var start = Stopwatch.GetTimestamp();
        var firstProposed = start + Stopwatch.Frequency;
        var finalRenderedAt = firstProposed + Stopwatch.Frequency;
        var finalRendered = Event(
            ViewportPresentationTelemetryEventKind.Rendered,
            finalRenderedAt,
            transaction: 3,
            generation: 3);
        var events = new[]
        {
            Event(ViewportPresentationTelemetryEventKind.Proposed, start - 1, 1, 1),
            Event(ViewportPresentationTelemetryEventKind.Rendered, start, 1, 1),
            Event(ViewportPresentationTelemetryEventKind.Proposed, firstProposed, 2, 2),
            Event(ViewportPresentationTelemetryEventKind.Rendered, firstProposed + 1, 2, 2),
            Event(ViewportPresentationTelemetryEventKind.Rendered, firstProposed + 2, 2, 2),
            Event(ViewportPresentationTelemetryEventKind.Proposed, firstProposed + 3, 3, 3),
            finalRendered,
        };

        var performance = Editor.Shell.Composition
            .StudioViewportTransactionWindowResizeSmoke
            .MeasureUniqueRenderedPerformance(events, start, finalRendered);

        Assert.True(performance.IsValid);
        Assert.Equal(firstProposed, performance.FirstProposedTimestamp);
        Assert.Equal(finalRenderedAt, performance.FinalRenderedTimestamp);
        Assert.Equal(2, performance.UniqueGenerationCount);
        Assert.Equal(2, performance.Rate, precision: 6);
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
        foreach (var pattern in new[] { "grow", "shrink", "aba" })
        {
            Assert.Contains($"window-resize-main-{pattern}-120hz-performance", scenarios);
        }
        Assert.Contains("window-resize-main-aba-structural", scenarios);
        Assert.Contains("window-resize-main-height-aba-projection", scenarios);
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
        var sceneMesh = testType.GetMethod(
            nameof(StudioProcessAcceptanceTests
                .Scene_mesh_closes_from_v2_document_to_presented_wireframe_draw));

        Assert.NotNull(steady);
        Assert.NotNull(family);
        Assert.NotNull(sceneMesh);
        Assert.IsType<StudioGpuFactAttribute>(
            Assert.Single(steady.GetCustomAttributes(typeof(StudioGpuFactAttribute), true)));
        Assert.IsType<StudioGpuTheoryAttribute>(
            Assert.Single(family.GetCustomAttributes(typeof(StudioGpuTheoryAttribute), true)));
        Assert.IsType<StudioGpuFactAttribute>(
            Assert.Single(sceneMesh.GetCustomAttributes(typeof(StudioGpuFactAttribute), true)));
    }

    [Fact]
    public void Scene_mesh_receipt_validator_requires_the_exact_wireframe_draw_identity()
    {
        var objectId = Guid.NewGuid();
        var runtimeEntityId = new EntityId(11, 3);
        var mesh = SceneMeshReference.DirectionalWedgeValidation;
        var entity = new SceneEntitySnapshot(
            objectId,
            runtimeEntityId,
            "Directional Wedge",
            TransformValue.Identity,
            mesh);
        var receipt = new ViewportSceneMeshReceipt(
            InputCount: 1,
            ResolvedCount: 1,
            RejectedCount: 0,
            IndexedDrawCount: 1,
            ViewportSceneRasterMode.Wireframe,
            EvidenceAvailable: true,
            runtimeEntityId,
            objectId,
            mesh.AssetId,
            Editor.Shell.Composition.StudioSceneMeshSmoke.ValidationMeshResourceKey,
            Editor.Shell.Composition.StudioSceneMeshSmoke.DefaultUnlitMaterialResourceKey,
            Editor.Shell.Composition.StudioSceneMeshSmoke.ValidationProductHash,
            SceneRevision: 4);

        Editor.Shell.Composition.StudioSceneMeshSmoke.ValidateReceipt(
            receipt,
            expectedRevision: 4,
            entity);

        Assert.Throws<InvalidOperationException>(() =>
            Editor.Shell.Composition.StudioSceneMeshSmoke.ValidateReceipt(
                receipt with { RasterMode = ViewportSceneRasterMode.Solid },
                expectedRevision: 4,
                entity));
    }

    [Fact]
    public void Scene_mesh_structured_evidence_round_trips_through_process_parser()
    {
        var meshObjectId = Guid.NewGuid();
        var emptyObjectId = Guid.NewGuid();
        var assetId = SceneMeshReference.DirectionalWedgeValidation.AssetId;
        var runtimeEntityId = new EntityId(8, 2);
        var initialReceipt = new ViewportSceneMeshReceipt(
            0,
            0,
            0,
            0,
            ViewportSceneRasterMode.Wireframe,
            true,
            null,
            null,
            null,
            0,
            0,
            0,
            1);
        var meshReceipt = new ViewportSceneMeshReceipt(
            1,
            1,
            0,
            1,
            ViewportSceneRasterMode.Wireframe,
            true,
            runtimeEntityId,
            meshObjectId,
            assetId,
            Editor.Shell.Composition.StudioSceneMeshSmoke.ValidationMeshResourceKey,
            Editor.Shell.Composition.StudioSceneMeshSmoke.DefaultUnlitMaterialResourceKey,
            Editor.Shell.Composition.StudioSceneMeshSmoke.ValidationProductHash,
            2);
        var evidence = new Editor.Shell.Composition.StudioSceneMeshSmokeEvidence(
            "scene-mesh-closure",
            "studio-scene-mesh-vulkan",
            PixelEvidenceAvailable: false,
            PhysicalDisplayedEvidenceAvailable: false,
            SceneSchemaVersion: 2,
            meshObjectId,
            emptyObjectId,
            assetId,
            Stage("initial-empty", 0, 1, initialReceipt),
            Stage("mesh-created", 1, 2, meshReceipt),
            Stage("empty-entity-created", 2, 3, meshReceipt with { SceneRevision = 3 }),
            Stage("transform-updated", 2, 5, meshReceipt with { SceneRevision = 5 }),
            RevisionOrderStrict: true,
            FinalExactSurface: true,
            StalePresentationExcluded: true,
            SupersededRevision: 4,
            SupersededRequestSequence: 4,
            SupersededFrameIndex: 4,
            SupersededReceipt: meshReceipt with { SceneRevision = 4 },
            PresentedFramesAcrossSupersede: 1,
            FinalPresentedRevision: 5);
        var output = Editor.Shell.Composition.StudioSceneMeshSmoke.SerializeEvidence(evidence);

        StudioProcessAcceptanceTests.AssertStructuredSceneMeshEvidence(output);

        static Editor.Shell.Composition.StudioSceneMeshStageEvidence Stage(
            string stage,
            int entityCount,
            ulong revision,
            ViewportSceneMeshReceipt receipt) =>
            new(
                stage,
                entityCount,
                revision,
                revision,
                revision,
                receipt,
                $"Presented scene revision {revision}.",
                CurrentSurfaceIsExact: true,
                LastPresentationIsExact: true);
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
            "window-resize-performance",
            "window-resize-structural",
            "window-resize-projection",
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
