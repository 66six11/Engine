using System;
using System.IO;
using System.Linq;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewCompositionPresenterSourceTests
{
    [Fact]
    public void Presenter_imports_vulkan_nt_image_and_uses_semaphore_update()
    {
        var source = LoadSource("Features", "SceneView", "Interop", "SceneViewCompositionPresenter.cs");

        Assert.Contains("KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle", source, StringComparison.Ordinal);
        Assert.Contains("KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle", source, StringComparison.Ordinal);
        Assert.Contains("new PlatformHandle(", source, StringComparison.Ordinal);
        Assert.Contains("ImportImage(", source, StringComparison.Ordinal);
        Assert.Contains("ImportSemaphore(", source, StringComparison.Ordinal);
        Assert.Contains("UpdateWithSemaphoresAsync(", source, StringComparison.Ordinal);
        Assert.Contains("ReleasePresentPacket", source, StringComparison.Ordinal);
        Assert.DoesNotContain("WaitForSingleObject", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Thread.Sleep", source, StringComparison.Ordinal);
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
