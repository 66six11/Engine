using System;
using System.IO;
using Asharia.Studio.Application.Projects;
using Xunit;

namespace Asharia.Studio.Application.Tests.Projects;

public sealed class RecentProjectStoreTests
{
    [Fact]
    public void Missing_preference_returns_no_recent_project()
    {
        using var workspace = new TemporaryWorkspace();
        var store = new RecentProjectStore(
            Path.Combine(workspace.Root, "preferences", "recent-project.txt"));

        Assert.Null(store.Read());
    }

    [Fact]
    public void Write_publishes_exact_root_and_replaces_previous_value()
    {
        using var workspace = new TemporaryWorkspace();
        var path = Path.Combine(
            workspace.Root,
            "preferences",
            "recent-project.txt");
        var store = new RecentProjectStore(path);

        store.Write(@"D:\Projects\First");
        store.Write(@"D:\项目\Second");

        Assert.Equal(@"D:\项目\Second", store.Read());
        Assert.Empty(Directory.GetFiles(
            Path.GetDirectoryName(path)!,
            "*.tmp"));
    }

    [Fact]
    public void Oversized_preference_is_rejected()
    {
        using var workspace = new TemporaryWorkspace();
        var path = Path.Combine(workspace.Root, "recent-project.txt");
        File.WriteAllText(path, new string('a', 65 * 1024));
        var store = new RecentProjectStore(path);

        _ = Assert.Throws<InvalidDataException>(() => store.Read());
    }

    private sealed class TemporaryWorkspace : IDisposable
    {
        public TemporaryWorkspace()
        {
            Root = Path.Combine(
                Path.GetTempPath(),
                "asharia-studio-recent-project-tests",
                Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Root);
        }

        public string Root { get; }

        public void Dispose()
        {
            Directory.Delete(Root, recursive: true);
        }
    }
}
