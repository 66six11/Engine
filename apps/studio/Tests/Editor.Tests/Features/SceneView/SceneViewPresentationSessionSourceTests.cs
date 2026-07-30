using System;
using System.IO;
using System.Linq;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewPresentationSessionSourceTests
{
    [Fact]
    public void Session_keeps_native_work_off_ui_and_composition_work_on_ui_path()
    {
        var source = LoadSource("Features", "SceneView", "Interop", "SceneViewPresentationSession.cs");

        Assert.Contains("KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle", source, StringComparison.Ordinal);
        Assert.Contains("KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle", source, StringComparison.Ordinal);
        Assert.Contains("new PlatformHandle(", source, StringComparison.Ordinal);
        Assert.Contains("ImportImage(", source, StringComparison.Ordinal);
        Assert.Contains("ImportSemaphore(", source, StringComparison.Ordinal);
        Assert.Contains("UpdateWithSemaphoresAsync(", source, StringComparison.Ordinal);
        Assert.Contains("CreatePresentSlot(", source, StringComparison.Ordinal);
        Assert.Contains("RenderPresentSlot(", source, StringComparison.Ordinal);
        Assert.Contains("Task.Run(operation)", source, StringComparison.Ordinal);
        Assert.Contains("SemaphoreSlim", source, StringComparison.Ordinal);
        Assert.Contains("producerGate_", source, StringComparison.Ordinal);
        Assert.Contains("NativeStartAdmission", source, StringComparison.Ordinal);
        Assert.Contains("WaitAsync(nativeStartAdmission.CancellationToken)", source, StringComparison.Ordinal);
        Assert.Contains("nativeStartAdmission.TryBegin()", source, StringComparison.Ordinal);
        Assert.Contains("catch (OperationCanceledException)", source, StringComparison.Ordinal);
        Assert.Contains("EnsureUiThread()", source, StringComparison.Ordinal);
        Assert.Contains("Action requestRetry", source, StringComparison.Ordinal);
        Assert.Contains("configuration.RequestRetry()", source, StringComparison.Ordinal);
        Assert.Contains("return !state_.IsCurrent(work.Request)", source, StringComparison.Ordinal);
        Assert.Contains("state_.IsCurrent(work.Request)", source, StringComparison.Ordinal);
        Assert.Contains("state_.CanPresent(work.Request)", source, StringComparison.Ordinal);
        Assert.Contains(
            "() => state_.MarkPresented(work.Request),",
            source,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            "committed = committed && state_.MarkPresented",
            source,
            StringComparison.Ordinal);
        Assert.Contains("MaximumOwnedSlots = 4", source, StringComparison.Ordinal);
        Assert.Contains("activeSlots_", source, StringComparison.Ordinal);
        Assert.Contains("retiringSlots_", source, StringComparison.Ordinal);
        Assert.Contains("slotCreationReservations_", source, StringComparison.Ordinal);
        Assert.Contains("OwnedSlotCount() < MaximumOwnedSlots", source, StringComparison.Ordinal);
        Assert.Contains("activeSlots_.Remove(slotId, out var slot)", source, StringComparison.Ordinal);
        Assert.Contains("retiringSlots_.Add(slotId, slot)", source, StringComparison.Ordinal);
        Assert.Contains("retiringSlots_.Remove(slotId)", source, StringComparison.Ordinal);
        Assert.Contains("SceneViewResourceQuarantine.Count", source, StringComparison.Ordinal);
        Assert.Contains("SceneViewResourceRetirement.RunAsync(", source, StringComparison.Ordinal);
        Assert.Contains("() => DisposeImportedObjectsAsync(slot)", source, StringComparison.Ordinal);
        Assert.Contains("Task.WhenAll(releases)", source, StringComparison.Ordinal);
        Assert.Contains("DisposeAsync().AsTask()", source, StringComparison.Ordinal);
        Assert.Contains("SceneViewResourceQuarantine.Retain(", source, StringComparison.Ordinal);
        Assert.Contains("ReleasePresentPacket", source, StringComparison.Ordinal);
        Assert.Contains("RegisterShutdownParticipant(DetachAsync)", source, StringComparison.Ordinal);
        Assert.Contains("shutdownRegistration?.Dispose()", source, StringComparison.Ordinal);
        var canceledStartCatch =
            source[
                source.IndexOf(
                    "catch (OperationCanceledException)",
                    StringComparison.Ordinal)..source.IndexOf(
                    "catch (Exception ex)",
                    StringComparison.Ordinal)];
        Assert.Contains("return true;", canceledStartCatch, StringComparison.Ordinal);
        Assert.DoesNotContain("UpdatePresent", canceledStartCatch, StringComparison.Ordinal);
        Assert.DoesNotContain("WaitForSingleObject", source, StringComparison.Ordinal);
        Assert.DoesNotContain(".Wait()", source, StringComparison.Ordinal);
        Assert.DoesNotContain(".Result", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Thread.Sleep", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Task.Delay", source, StringComparison.Ordinal);
        Assert.DoesNotContain("ScheduleBackpressureRetry", source, StringComparison.Ordinal);
        Assert.DoesNotContain("warmCurrentGeneration", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Task.Run(operation, nativeStartAdmission.CancellationToken)", source, StringComparison.Ordinal);
    }

    private static string LoadSource(params string[] pathParts)
    {
        var root = FindRepositoryRoot();
        return File.ReadAllText(Path.Combine(new[] { root }.Concat(pathParts).ToArray()));
    }

    private static string FindRepositoryRoot()
    {
        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Editor.sln.");
    }
}
