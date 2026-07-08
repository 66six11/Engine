using System;
using System.IO;
using System.Linq;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewCompositionCapabilityReaderTests
{
    [Fact]
    public void Capability_reader_uses_attached_element_compositor_and_gpu_interop()
    {
        var source = LoadSource("Features", "SceneView", "Interop", "SceneViewCompositionCapabilityReader.cs");

        Assert.Contains("ElementComposition.GetElementVisual", source, StringComparison.Ordinal);
        Assert.Contains("TopLevel.GetTopLevel", source, StringComparison.Ordinal);
        Assert.Contains("TryGetCompositionGpuInterop", source, StringComparison.Ordinal);
        Assert.Contains("KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle", source, StringComparison.Ordinal);
        Assert.Contains("KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle", source, StringComparison.Ordinal);
        Assert.Contains("GetSynchronizationCapabilities", source, StringComparison.Ordinal);
        Assert.Contains("Convert.ToHexString", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Editor.Core.Models.Viewports.IntPtr", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Thread.Sleep", source, StringComparison.Ordinal);
        Assert.DoesNotContain("WaitForSingleObject", source, StringComparison.Ordinal);
    }

    private static string LoadSource(params string[] pathParts)
    {
        var root = FindRepositoryRoot();
        return File.ReadAllText(Path.Combine(new[] { root }.Concat(pathParts).ToArray()));
    }

    private static string FindRepositoryRoot()
    {
        var workspaceRoot = Environment.GetEnvironmentVariable("CODEX_WORKSPACE_ROOT");
        if (!string.IsNullOrWhiteSpace(workspaceRoot)
            && File.Exists(Path.Combine(workspaceRoot, "Editor.sln")))
        {
            return workspaceRoot;
        }

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
