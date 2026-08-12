using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Project;
using Asharia.Studio.EngineBridge.Scene;
using Asharia.Studio.EngineBridge.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Rendering.Composition;

namespace Editor.Shell.Composition;

internal static class StudioSceneMeshSmoke
{
    internal const string CommandLineSwitch = "--smoke-studio-scene-mesh";
    internal const string EvidencePrefix = "studio-scene-mesh-evidence ";
    internal const string PassMarker = "studio-scene-mesh PASS:";
    internal const ulong ValidationMeshResourceKey = 0x0EB29D6DE539D278UL;
    internal const ulong DefaultUnlitMaterialResourceKey = 0x4153484D41544C01UL;
    internal const ulong ValidationProductHash = 0x0EB29D6DE539D278UL;
    internal static TransformValue ValidationLocalTransform { get; } = new(
        new Float3(0.75F, 0.5F, 0.0F),
        new Quaternion(0.0F, 0.38268343F, 0.0F, 0.9238795F),
        new Float3(1.25F, 0.8F, 1.5F));

    private static readonly TimeSpan kOperationTimeout = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan kPresentationTimeout = TimeSpan.FromSeconds(20);
    private static readonly JsonSerializerOptions kJsonOptions = CreateJsonOptions();

    public static async Task RunAsync(IClassicDesktopStyleApplicationLifetime desktop)
    {
        ArgumentNullException.ThrowIfNull(desktop);
        var exitCode = 1;
        var temporaryParent = Path.Combine(
            Path.GetTempPath(),
            $"asharia-studio-scene-mesh-{Guid.NewGuid():N}");
        try
        {
            Directory.CreateDirectory(temporaryParent);
            var evidence = await RunCoreAsync(desktop, temporaryParent);
            DeleteTemporaryParent(temporaryParent);
            Console.Out.WriteLine(SerializeEvidence(evidence));
            Console.Out.WriteLine(
                $"{PassMarker} finalRevision={evidence.FinalPresentedRevision}, " +
                $"object={evidence.MeshObjectId:D}, wireframe=true, indexedDraws=1.");
            exitCode = 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"studio-scene-mesh FAIL: {exception.Message}");
        }
        finally
        {
            if (Directory.Exists(temporaryParent))
            {
                try
                {
                    DeleteTemporaryParent(temporaryParent);
                }
                catch (Exception exception)
                {
                    Console.Error.WriteLine(
                        $"studio-scene-mesh FAIL: temporary project cleanup failed: " +
                        exception.Message);
                    exitCode = 1;
                }
            }
            desktop.Shutdown(exitCode);
        }
    }

    internal static string SerializeEvidence(StudioSceneMeshSmokeEvidence evidence)
    {
        ArgumentNullException.ThrowIfNull(evidence);
        return EvidencePrefix + JsonSerializer.Serialize(evidence, kJsonOptions);
    }

    internal static bool IsPresentedRevision(string statusMessage, ulong revision)
    {
        ArgumentNullException.ThrowIfNull(statusMessage);
        var formattedRevision = revision.ToString(CultureInfo.InvariantCulture);
        return string.Equals(
                   statusMessage,
                   $"Presented scene revision {formattedRevision}.",
                   StringComparison.Ordinal) ||
               string.Equals(
                   statusMessage,
                   $"Rendered scene revision {formattedRevision}.",
                   StringComparison.Ordinal);
    }

    internal static void ValidateReceipt(
        ViewportSceneMeshReceipt receipt,
        ulong expectedRevision,
        SceneEntitySnapshot? expectedMeshEntity)
    {
        ArgumentNullException.ThrowIfNull(receipt);
        Require(
            receipt.SceneRevision == expectedRevision,
            $"Scene mesh evidence revision {receipt.SceneRevision} did not match " +
            $"the authored revision {expectedRevision}.");
        Require(
            receipt.RasterMode == ViewportSceneRasterMode.Wireframe,
            $"Scene mesh evidence used {receipt.RasterMode} instead of Wireframe.");
        Require(receipt.EvidenceAvailable, "Scene mesh GPU draw evidence was not captured.");
        Require(receipt.RejectedCount == 0, "The authored scene mesh was rejected.");

        if (expectedMeshEntity is null)
        {
            Require(
                receipt.InputCount == 0 &&
                receipt.ResolvedCount == 0 &&
                receipt.IndexedDrawCount == 0,
                "The empty scene produced an authored mesh draw.");
            Require(
                receipt.RepresentativeSourceEntityId is null &&
                receipt.RepresentativeObjectId is null &&
                receipt.RepresentativeAssetId is null &&
                receipt.MeshResourceKey == 0 &&
                receipt.MaterialResourceKey == 0 &&
                receipt.ProductHash == 0,
                "The empty scene retained non-empty mesh resource evidence.");
            return;
        }

        var expectedMesh = expectedMeshEntity.Mesh ?? throw new InvalidOperationException(
            "The expected entity has no mesh reference.");
        Require(
            receipt.InputCount == 1 &&
            receipt.ResolvedCount == 1 &&
            receipt.IndexedDrawCount == 1,
            "The authored mesh did not close as exactly one indexed GPU draw.");
        Require(
            receipt.RepresentativeSourceEntityId == expectedMeshEntity.RuntimeEntityId,
            "The GPU draw source runtime EntityId did not match the authored entity.");
        Require(
            receipt.RepresentativeObjectId == expectedMeshEntity.ObjectId,
            "The GPU draw source object id did not match the authored entity.");
        Require(
            receipt.RepresentativeAssetId == expectedMesh.AssetId,
            "The resolved GPU asset id did not match the authored mesh reference.");
        Require(
            receipt.MeshResourceKey == ValidationMeshResourceKey,
            "The validation mesh resolved to an unexpected GPU resource key.");
        Require(
            receipt.MaterialResourceKey == DefaultUnlitMaterialResourceKey,
            "The validation mesh resolved to an unexpected material resource key.");
        Require(
            receipt.ProductHash == ValidationProductHash,
            "The validation mesh resolved to an unexpected product hash.");
    }

    private static async Task<StudioSceneMeshSmokeEvidence> RunCoreAsync(
        IClassicDesktopStyleApplicationLifetime desktop,
        string temporaryParent)
    {
        await using var projectSession = new ProjectSession(
            new ProjectDescriptorBridge(),
            new SceneDocumentBridge());
        await using var host = new StudioViewportSmokeHost();

        var createdProject = await AwaitOperationAsync(
            projectSession.CreateProjectAsync(
                temporaryParent,
                "SceneMeshSmoke",
                ProjectDocumentTransitionExpectation.Capture(projectSession.Current)));
        var initialDocument = RequireSucceeded(createdProject, "create project");
        Require(initialDocument.Entities.Count == 0, "The new project scene was not empty.");
        var schemaVersion = await ReadSchemaVersionAsync(initialDocument.Path);
        Require(schemaVersion == 2, $"The created scene used schema v{schemaVersion}, not v2.");

        await host.WarmUpRuntimeAsync();
        var observer = new SceneMeshFrameObserver();
        var supersedeGate = new SceneMeshSupersedeGate();
        var session = host.CreateSceneSession(initialDocument);
        session.SetSceneRasterMode(ViewportSceneRasterMode.Wireframe);
        var control = host.CreateControl(
            session,
            isRealtime: false,
            testHooks: new ViewportCompositionControlTestHooks
            {
                EnableSceneMeshEvidence = true,
                LeaseAcquired = lease =>
                {
                    var frame = observer.Observe(lease);
                    supersedeGate.BlockIfArmed(frame);
                },
                RequestPublished = request => observer.Observe(request, session.Current),
            });
        var initialFrameCount = control.PresentationMetrics.TotalPresentedFrames;
        host.Show(desktop, control, "Studio Scene Mesh Smoke", width: 960, height: 640);

        var initialFrame = await WaitForPresentedRevisionAsync(
            control,
            observer,
            initialDocument,
            initialFrameCount);
        ValidatePresentedFrame(session, initialFrame, initialDocument, expectedMeshEntity: null);
        var initialEvidence = CaptureStageEvidence(
            "initial-empty",
            initialDocument,
            initialFrame,
            control);

        var meshResult = await AwaitOperationAsync(projectSession.CreateMeshEntityAsync(
            "Directional Wedge",
            SceneMeshReference.DirectionalWedgeValidation));
        var meshDocument = RequireSucceeded(meshResult, "create mesh entity");
        var meshObjectId = meshResult.CreatedObjectId ?? throw new InvalidOperationException(
            "The mesh create result omitted its stable object id.");
        Require(meshDocument.Entities.Count == 1, "Mesh creation did not produce one entity.");
        var meshEntity = RequireEntity(meshDocument, meshObjectId);
        Require(
            meshEntity.Mesh == SceneMeshReference.DirectionalWedgeValidation,
            "The created scene entity did not retain the directional wedge asset reference.");
        var meshFrameCount = control.PresentationMetrics.TotalPresentedFrames;
        session.SynchronizeDocument(meshDocument);
        var meshFrame = await WaitForPresentedRevisionAsync(
            control,
            observer,
            meshDocument,
            meshFrameCount);
        ValidatePresentedFrame(session, meshFrame, meshDocument, meshEntity);
        var meshEvidence = CaptureStageEvidence(
            "mesh-created",
            meshDocument,
            meshFrame,
            control);

        var emptyResult = await AwaitOperationAsync(
            projectSession.CreateEntityAsync("Empty Entity"));
        var emptyDocument = RequireSucceeded(emptyResult, "create empty entity");
        var emptyObjectId = emptyResult.CreatedObjectId ?? throw new InvalidOperationException(
            "The empty entity create result omitted its stable object id.");
        Require(
            emptyDocument.Entities.Count == 2,
            "Empty entity creation did not preserve exactly one authored mesh entity.");
        var emptyEntity = RequireEntity(emptyDocument, emptyObjectId);
        Require(emptyEntity.Mesh is null, "The empty entity unexpectedly acquired a mesh.");
        var meshAfterEmpty = RequireEntity(emptyDocument, meshObjectId);
        Require(
            meshAfterEmpty.RuntimeEntityId == meshEntity.RuntimeEntityId,
            "Creating an empty entity changed the mesh entity's transient runtime identity.");
        var emptyFrameCount = control.PresentationMetrics.TotalPresentedFrames;
        session.SynchronizeDocument(emptyDocument);
        var emptyFrame = await WaitForPresentedRevisionAsync(
            control,
            observer,
            emptyDocument,
            emptyFrameCount);
        ValidatePresentedFrame(session, emptyFrame, emptyDocument, meshAfterEmpty);
        var emptyEvidence = CaptureStageEvidence(
            "empty-entity-created",
            emptyDocument,
            emptyFrame,
            control);

        var supersededTransform = new TransformValue(
            new Float3(0.25F, 0.25F, 0.0F),
            Quaternion.Identity,
            Float3.One);
        var supersededResult = await AwaitOperationAsync(
            projectSession.SetEntityTransformAsync(
                meshObjectId,
                supersededTransform,
                new ProjectSessionEditContext(
                    ProjectEditId.CreateNew(),
                    emptyDocument.Revision)));
        var supersededDocument = RequireSucceeded(
            supersededResult,
            "stage superseded mesh Transform");
        Require(
            supersededDocument.Entities.Count == 2,
            "The superseded Transform revision changed the authoritative entity count.");
        var supersededMesh = RequireEntity(supersededDocument, meshObjectId);
        Require(
            supersededMesh.Transform == supersededTransform,
            "The superseded scene revision omitted its requested Transform.");
        Require(
            supersededMesh.RuntimeEntityId == meshEntity.RuntimeEntityId,
            "The superseded Transform changed the mesh entity's transient runtime identity.");

        var transform = ValidationLocalTransform;
        var transformResult = await AwaitOperationAsync(
            projectSession.SetEntityTransformAsync(
                meshObjectId,
                transform,
                new ProjectSessionEditContext(
                    ProjectEditId.CreateNew(),
                    supersededDocument.Revision)));
        var transformedDocument = RequireSucceeded(transformResult, "update mesh Transform");
        Require(
            transformedDocument.Entities.Count == 2,
            "The Transform edit changed the authoritative entity count.");
        var transformedMesh = RequireEntity(transformedDocument, meshObjectId);
        Require(
            transformedMesh.Transform == transform,
            "The authoritative scene snapshot did not retain the requested Transform.");
        Require(
            transformedMesh.RuntimeEntityId == meshEntity.RuntimeEntityId,
            "The Transform edit changed the mesh entity's transient runtime identity.");
        var transformFrameCount = control.PresentationMetrics.TotalPresentedFrames;
        supersedeGate.Arm(supersededDocument.Revision);
        var advanceFence = Task.Run(async () =>
        {
            try
            {
                var acquired = await supersedeGate.WaitForAcquiredAsync()
                    .WaitAsync(kPresentationTimeout);
                session.SynchronizeDocument(transformedDocument);
                return acquired;
            }
            finally
            {
                supersedeGate.Release();
            }
        });
        session.SynchronizeDocument(supersededDocument);
        var supersededFrame = await advanceFence.WaitAsync(kPresentationTimeout);
        ValidateAcquiredFrame(supersededFrame, supersededDocument, supersededMesh);
        var finalSessionSnapshot = session.Current;
        Require(
            finalSessionSnapshot.TargetId == transformedDocument.SceneId &&
            finalSessionSnapshot.TargetRevision == transformedDocument.Revision,
            "The final presentation fence did not target the transformed authoritative snapshot.");
        Require(
            finalSessionSnapshot.MinimumPresentableSequence ==
                checked(supersededFrame.RequestSequence + 1U),
            "The final presentation fence did not advance exactly past the superseded request.");
        Require(
            supersededFrame.RequestSequence < session.Current.MinimumPresentableSequence,
            "The superseded Transform lease remained inside the final presentation fence.");
        var transformFrame = await WaitForPresentedRevisionAsync(
            control,
            observer,
            transformedDocument,
            transformFrameCount);
        ValidatePresentedFrame(session, transformFrame, transformedDocument, transformedMesh);
        var transformEvidence = CaptureStageEvidence(
            "transform-updated",
            transformedDocument,
            transformFrame,
            control);

        var visual = ElementComposition.GetElementVisual(control) ??
            throw new InvalidOperationException("The Scene viewport composition visual is unavailable.");
        await visual.Compositor.RequestCompositionBatchCommitAsync()
            .Rendered.WaitAsync(kPresentationTimeout);
        await Task.Delay(TimeSpan.FromMilliseconds(25));
        var geometry = control.PresentationGeometryMetrics;
        var presentedFramesAcrossSupersede = checked(
            control.PresentationMetrics.TotalPresentedFrames - transformFrameCount);
        var stalePresentationExcluded =
            presentedFramesAcrossSupersede == 1 &&
            IsPresentedRevision(control.StatusMessage, transformedDocument.Revision) &&
            transformFrame.RequestSequence ==
                transformFrame.RequestSession.MinimumPresentableSequence &&
            session.CanPresentPublishedFrame(
                transformFrame.RequestSequence,
                transformFrame.TargetRevision);
        Require(
            stalePresentationExcluded,
            "The Scene viewport did not remain fenced to the latest Transform revision.");
        Require(
            geometry.CurrentSurfaceIsExact && geometry.LastPresentationIsExact,
            "The latest mesh revision was not retained on an exact composition surface.");

        var revisionOrderStrict =
            meshDocument.Revision == checked(initialDocument.Revision + 1U) &&
            emptyDocument.Revision == checked(meshDocument.Revision + 1U) &&
            supersededDocument.Revision == checked(emptyDocument.Revision + 1U) &&
            transformedDocument.Revision == checked(supersededDocument.Revision + 1U);
        Require(revisionOrderStrict, "Scene document revisions did not advance exactly once per mutation.");

        return new StudioSceneMeshSmokeEvidence(
            "scene-mesh-closure",
            "studio-scene-mesh-vulkan",
            PixelEvidenceAvailable: false,
            PhysicalDisplayedEvidenceAvailable: false,
            schemaVersion,
            meshObjectId,
            emptyObjectId,
            SceneMeshReference.DirectionalWedgeValidation.AssetId,
            initialEvidence,
            meshEvidence,
            emptyEvidence,
            transformEvidence,
            revisionOrderStrict,
            geometry.CurrentSurfaceIsExact && geometry.LastPresentationIsExact,
            stalePresentationExcluded,
            supersededDocument.Revision,
            supersededFrame.RequestSequence,
            supersededFrame.FrameIndex,
            supersededFrame.Receipt,
            presentedFramesAcrossSupersede,
            transformedDocument.Revision);
    }

    private static async Task<ObservedSceneMeshFrame> WaitForPresentedRevisionAsync(
        ViewportCompositionControl control,
        SceneMeshFrameObserver observer,
        SceneDocumentSnapshot document,
        ulong presentedFramesBefore)
    {
        using var deadline = new CancellationTokenSource(kPresentationTimeout);
        try
        {
            while (true)
            {
                if (control.IsDegraded)
                {
                    throw new InvalidOperationException(control.StatusMessage);
                }
                var geometry = control.PresentationGeometryMetrics;
                if (observer.TryGetLatest(document.Revision, out var frame) &&
                    control.PresentationMetrics.TotalPresentedFrames > presentedFramesBefore &&
                    IsPresentedRevision(control.StatusMessage, document.Revision) &&
                    geometry.CurrentSurfaceIsExact &&
                    geometry.LastPresentationIsExact)
                {
                    return frame;
                }
                await Task.Delay(TimeSpan.FromMilliseconds(5), deadline.Token);
            }
        }
        catch (OperationCanceledException) when (deadline.IsCancellationRequested)
        {
            throw new TimeoutException(
                $"The Scene viewport did not present authored revision {document.Revision} " +
                $"within {kPresentationTimeout.TotalSeconds:F0} seconds.");
        }
    }

    private static void ValidatePresentedFrame(
        ViewportSession session,
        ObservedSceneMeshFrame frame,
        SceneDocumentSnapshot document,
        SceneEntitySnapshot? expectedMeshEntity)
    {
        ValidateAuthoredRequest(frame, document, expectedMeshEntity);
        Require(frame.Kind == ViewportRenderKind.Scene, "The frame was not rendered by Scene View.");
        Require(frame.TargetId == document.SceneId, "The frame targeted a different scene document.");
        Require(
            frame.TargetRevision == document.Revision,
            "The presented frame did not target the authoritative scene revision.");
        Require(
            frame.RequestSequence == session.Current.MinimumPresentableSequence,
            "The presented frame did not match the current session presentation fence exactly.");
        ValidateReceipt(frame.Receipt, document.Revision, expectedMeshEntity);
    }

    private static void ValidateAcquiredFrame(
        ObservedSceneMeshFrame frame,
        SceneDocumentSnapshot document,
        SceneEntitySnapshot expectedMeshEntity)
    {
        ValidateAuthoredRequest(frame, document, expectedMeshEntity);
        Require(frame.Kind == ViewportRenderKind.Scene, "The acquired frame was not Scene View.");
        Require(frame.TargetId == document.SceneId, "The acquired frame targeted another scene.");
        Require(
            frame.TargetRevision == document.Revision,
            "The acquired frame did not target the superseded scene revision.");
        ValidateReceipt(frame.Receipt, document.Revision, expectedMeshEntity);
    }

    private static void ValidateAuthoredRequest(
        ObservedSceneMeshFrame frame,
        SceneDocumentSnapshot document,
        SceneEntitySnapshot? expectedMeshEntity)
    {
        var request = frame.Request;
        var requestSession = frame.RequestSession;
        Require(
            request.SessionId == requestSession.SessionId &&
            request.Kind == ViewportRenderKind.Scene &&
            request.Kind == requestSession.Kind &&
            request.TargetKind == ViewportTargetKind.DocumentScene &&
            request.TargetKind == requestSession.TargetKind,
            "The Scene mesh request did not retain its published session identity.");
        Require(
            request.Sequence == frame.RequestSequence &&
            request.Sequence == requestSession.LastSequence &&
            request.Sequence == requestSession.MinimumPresentableSequence,
            "The Scene mesh request did not match its exact publish-time presentation fence.");
        Require(
            request.TargetId == document.SceneId &&
            request.TargetId == requestSession.TargetId &&
            request.TargetId == frame.TargetId &&
            request.TargetRevision == document.Revision &&
            request.TargetRevision == requestSession.TargetRevision &&
            request.TargetRevision == frame.TargetRevision,
            "The Scene mesh request did not retain the authoritative snapshot identity and revision.");
        Require(
            request.SceneRasterMode == ViewportSceneRasterMode.Wireframe,
            "The Scene mesh request did not retain the authored wireframe view policy.");

        if (expectedMeshEntity is null)
        {
            Require(
                request.AuthoredMeshes.Count == 0,
                "The empty authoritative snapshot published an authored mesh request.");
            return;
        }

        var expectedMesh = expectedMeshEntity.Mesh ?? throw new InvalidOperationException(
            "The expected request entity has no mesh reference.");
        Require(
            request.AuthoredMeshes.Count == 1,
            "The authoritative snapshot did not publish exactly one authored mesh request.");
        var authoredMesh = request.AuthoredMeshes[0];
        Require(
            authoredMesh.ObjectId == expectedMeshEntity.ObjectId &&
            authoredMesh.RuntimeEntityId == expectedMeshEntity.RuntimeEntityId &&
            authoredMesh.AssetId == expectedMesh.AssetId &&
            authoredMesh.ExpectedType == ViewportAuthoredMeshSnapshot.ExpectedMeshType &&
            authoredMesh.Transform == expectedMeshEntity.Transform,
            "The authored mesh request changed snapshot identity, type, or local Transform.");
    }

    private static StudioSceneMeshStageEvidence CaptureStageEvidence(
        string stage,
        SceneDocumentSnapshot document,
        ObservedSceneMeshFrame frame,
        ViewportCompositionControl control)
    {
        var geometry = control.PresentationGeometryMetrics;
        return new StudioSceneMeshStageEvidence(
            stage,
            document.Entities.Count,
            frame.Request.SessionId.Value,
            frame.Request.TargetId,
            frame.TargetRevision,
            frame.RequestSequence,
            frame.RequestSession.MinimumPresentableSequence,
            frame.Request.AuthoredMeshes.SingleOrDefault(),
            frame.FrameIndex,
            frame.Receipt,
            control.StatusMessage,
            geometry.CurrentSurfaceIsExact,
            geometry.LastPresentationIsExact);
    }

    private static async Task<ProjectSessionOperationResult> AwaitOperationAsync(
        ValueTask<ProjectSessionOperationResult> operation) =>
        await operation.AsTask().WaitAsync(kOperationTimeout);

    private static SceneDocumentSnapshot RequireSucceeded(
        ProjectSessionOperationResult result,
        string operation)
    {
        ArgumentNullException.ThrowIfNull(result);
        if (!result.Succeeded)
        {
            throw new InvalidOperationException(
                $"Studio could not {operation}: {result.FailureKind}: {result.Message}");
        }
        return result.Current.Document ?? throw new InvalidOperationException(
            $"Studio {operation} succeeded without an authoritative scene snapshot.");
    }

    private static SceneEntitySnapshot RequireEntity(
        SceneDocumentSnapshot document,
        Guid objectId)
    {
        var entity = document.Entities.SingleOrDefault(candidate => candidate.ObjectId == objectId);
        return entity ?? throw new InvalidOperationException(
            $"The authoritative scene snapshot omitted object {objectId:D}.");
    }

    private static async Task<int> ReadSchemaVersionAsync(string scenePath)
    {
        var json = await File.ReadAllTextAsync(scenePath);
        using var document = JsonDocument.Parse(json);
        return document.RootElement.GetProperty("schemaVersion").GetInt32();
    }

    private static JsonSerializerOptions CreateJsonOptions()
    {
        var options = new JsonSerializerOptions(JsonSerializerDefaults.Web);
        options.Converters.Add(new JsonStringEnumConverter(JsonNamingPolicy.CamelCase));
        return options;
    }

    private static void DeleteTemporaryParent(string temporaryParent)
    {
        var fullPath = Path.GetFullPath(temporaryParent);
        var relativeToTemp = Path.GetRelativePath(Path.GetFullPath(Path.GetTempPath()), fullPath);
        if (Path.IsPathRooted(relativeToTemp) ||
            relativeToTemp.StartsWith("..", StringComparison.Ordinal) ||
            !Path.GetFileName(fullPath).StartsWith(
                "asharia-studio-scene-mesh-",
                StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                $"Refusing to remove unexpected smoke directory '{fullPath}'.");
        }
        Directory.Delete(fullPath, recursive: true);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private sealed class SceneMeshFrameObserver
    {
        private readonly object gate_ = new();
        private readonly List<ObservedSceneMeshFrame> frames_ = [];
        private readonly List<ObservedSceneMeshRequest> requests_ = [];

        public void Observe(
            ViewportRenderRequest request,
            ViewportSessionSnapshot sessionSnapshot)
        {
            ArgumentNullException.ThrowIfNull(request);
            lock (gate_)
            {
                requests_.Add(new ObservedSceneMeshRequest(request, sessionSnapshot));
            }
        }

        public ObservedSceneMeshFrame Observe(ViewportFrameLease lease)
        {
            ArgumentNullException.ThrowIfNull(lease);
            lock (gate_)
            {
                var request = requests_.SingleOrDefault(
                    candidate => candidate.Request.Sequence == lease.RequestSequence);
                if (request.Request is null)
                {
                    throw new InvalidOperationException(
                        $"Scene mesh lease {lease.RequestSequence} has no published request.");
                }
                var frame = new ObservedSceneMeshFrame(
                    lease.TargetId,
                    lease.TargetRevision,
                    lease.RequestSequence,
                    lease.FrameIndex,
                    lease.Kind,
                    lease.SceneMeshReceipt,
                    request.Request,
                    request.SessionSnapshot);
                frames_.Add(frame);
                return frame;
            }
        }

        public bool TryGetLatest(ulong targetRevision, out ObservedSceneMeshFrame frame)
        {
            lock (gate_)
            {
                for (var index = frames_.Count - 1; index >= 0; index--)
                {
                    if (frames_[index].TargetRevision == targetRevision)
                    {
                        frame = frames_[index];
                        return true;
                    }
                }
            }
            frame = default;
            return false;
        }
    }

    private sealed class SceneMeshSupersedeGate
    {
        private readonly object gate_ = new();
        private TaskCompletionSource<ObservedSceneMeshFrame>? acquired_;
        private TaskCompletionSource? release_;
        private ulong expectedRevision_;
        private bool triggered_;

        public void Arm(ulong expectedRevision)
        {
            if (expectedRevision == 0)
            {
                throw new ArgumentOutOfRangeException(nameof(expectedRevision));
            }
            lock (gate_)
            {
                if (acquired_ is not null)
                {
                    throw new InvalidOperationException(
                        "The scene mesh supersede gate was already armed.");
                }
                expectedRevision_ = expectedRevision;
                acquired_ = new TaskCompletionSource<ObservedSceneMeshFrame>(
                    TaskCreationOptions.RunContinuationsAsynchronously);
                release_ = new TaskCompletionSource(
                    TaskCreationOptions.RunContinuationsAsynchronously);
            }
        }

        public Task<ObservedSceneMeshFrame> WaitForAcquiredAsync()
        {
            lock (gate_)
            {
                return acquired_?.Task ?? throw new InvalidOperationException(
                    "The scene mesh supersede gate is not armed.");
            }
        }

        public void BlockIfArmed(ObservedSceneMeshFrame frame)
        {
            TaskCompletionSource<ObservedSceneMeshFrame>? acquired;
            Task release;
            lock (gate_)
            {
                if (acquired_ is null || release_ is null || triggered_ ||
                    frame.TargetRevision != expectedRevision_)
                {
                    return;
                }
                triggered_ = true;
                acquired = acquired_;
                release = release_.Task;
            }
            acquired.TrySetResult(frame);
            // This deliberate smoke seam holds the old lease before composition submission
            // while a background continuation advances the session revision fence.
            if (!release.Wait(kPresentationTimeout))
            {
                throw new TimeoutException(
                    "The superseded scene mesh lease was not released by the revision fence.");
            }
        }

        public void Release()
        {
            lock (gate_)
            {
                release_?.TrySetResult();
            }
        }
    }

    private readonly record struct ObservedSceneMeshRequest(
        ViewportRenderRequest Request,
        ViewportSessionSnapshot SessionSnapshot);

    private readonly record struct ObservedSceneMeshFrame(
        Guid TargetId,
        ulong TargetRevision,
        ulong RequestSequence,
        ulong FrameIndex,
        ViewportRenderKind Kind,
        ViewportSceneMeshReceipt Receipt,
        ViewportRenderRequest Request,
        ViewportSessionSnapshot RequestSession);
}

internal sealed record StudioSceneMeshStageEvidence(
    string Stage,
    int DocumentEntityCount,
    Guid SessionId,
    Guid TargetId,
    ulong TargetRevision,
    ulong RequestSequence,
    ulong MinimumPresentableSequence,
    ViewportAuthoredMeshSnapshot? AuthoredMesh,
    ulong FrameIndex,
    ViewportSceneMeshReceipt Receipt,
    string PresentationStatus,
    bool CurrentSurfaceIsExact,
    bool LastPresentationIsExact);

internal sealed record StudioSceneMeshSmokeEvidence(
    string Scenario,
    string EvidenceKind,
    bool PixelEvidenceAvailable,
    bool PhysicalDisplayedEvidenceAvailable,
    int SceneSchemaVersion,
    Guid MeshObjectId,
    Guid EmptyObjectId,
    Guid AssetId,
    StudioSceneMeshStageEvidence Initial,
    StudioSceneMeshStageEvidence MeshCreated,
    StudioSceneMeshStageEvidence EmptyEntityCreated,
    StudioSceneMeshStageEvidence TransformUpdated,
    bool RevisionOrderStrict,
    bool FinalExactSurface,
    bool StalePresentationExcluded,
    ulong SupersededRevision,
    ulong SupersededRequestSequence,
    ulong SupersededFrameIndex,
    ViewportSceneMeshReceipt SupersededReceipt,
    ulong PresentedFramesAcrossSupersede,
    ulong FinalPresentedRevision);
