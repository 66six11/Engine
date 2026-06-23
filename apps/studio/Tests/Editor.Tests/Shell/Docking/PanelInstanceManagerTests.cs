using System;
using Editor.Core.Models;
using Editor.Shell.Docking;
using Xunit;

namespace Editor.Tests.Shell.Docking;

public sealed class PanelInstanceManagerTests
{
    [Fact]
    public void ReleaseTab_keeps_keep_alive_content_until_manager_disposal()
    {
        var manager = new PanelInstanceManager();
        var disposable = new RecordingDisposable();
        var descriptor = CreateDescriptor(
            "panel",
            DockContentCachePolicy.KeepAlive,
            () => disposable);

        var tab = manager.CreateTab(descriptor);
        tab.ReleasePanelInstance();

        Assert.False(disposable.IsDisposed);

        manager.Dispose();

        Assert.True(disposable.IsDisposed);
    }

    [Fact]
    public void ReleaseTab_disposes_recreate_on_open_content_on_close()
    {
        var manager = new PanelInstanceManager();
        var disposable = new RecordingDisposable();
        var descriptor = CreateDescriptor(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => disposable);

        var tab = manager.CreateTab(descriptor);
        tab.ReleasePanelInstance();

        Assert.True(disposable.IsDisposed);
    }

    [Fact]
    public void CreateTab_reuses_keep_alive_content_after_release()
    {
        var manager = new PanelInstanceManager();
        var contentFactory = new CountingContentFactory();
        var descriptor = CreateDescriptor(
            "panel",
            DockContentCachePolicy.KeepAlive,
            contentFactory.Create);

        var first = manager.CreateTab(descriptor);
        first.ReleasePanelInstance();
        var second = manager.CreateTab(descriptor);

        Assert.Same(first.Content, second.Content);
        Assert.Equal(1, contentFactory.CreateCount);
    }

    [Fact]
    public void ReleaseTab_is_idempotent()
    {
        var manager = new PanelInstanceManager();
        var disposable = new RecordingDisposable();
        var descriptor = CreateDescriptor(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => disposable);

        var tab = manager.CreateTab(descriptor);
        tab.ReleasePanelInstance();
        tab.ReleasePanelInstance();

        Assert.Equal(1, disposable.DisposeCount);
    }

    [Fact]
    public void CreateTab_preserves_workspace_descriptor_metadata_defaults()
    {
        var manager = new PanelInstanceManager();
        var descriptor = CreateDescriptor(
            "panel",
            DockContentCachePolicy.KeepAlive,
            () => new object());

        var tab = manager.CreateTab(descriptor);

        Assert.Equal("panel", tab.Id);
        Assert.Equal("panel", tab.Title);
        Assert.Equal("LEFT", tab.Tag);
        Assert.Equal("Window/Panels/panel", tab.TitleDetail);
        Assert.Equal("tool", tab.StatusText);
        Assert.Equal(PanelKind.Tool, tab.Kind);
        Assert.Equal(DockArea.Left, tab.Area);
    }

    private static PanelDescriptor CreateDescriptor(
        string id,
        DockContentCachePolicy cachePolicy,
        Func<object> createContent)
    {
        return new PanelDescriptor(
            id,
            id,
            PanelKind.Tool,
            DockArea.Left,
            $"Window/Panels/{id}",
            cachePolicy,
            createContent);
    }

    private sealed class CountingContentFactory
    {
        public int CreateCount { get; private set; }

        public object Create()
        {
            CreateCount++;
            return new object();
        }
    }

    private sealed class RecordingDisposable : IDisposable
    {
        public int DisposeCount { get; private set; }

        public bool IsDisposed => DisposeCount > 0;

        public void Dispose()
        {
            DisposeCount++;
        }
    }
}
