using System;
using System.Collections.Generic;
using Asharia.Editor.Panels;
using Asharia.Studio.Application.Panels;
using Editor.Core.Abstractions;
using Editor.Core.Models.Panels;
using Editor.Shell.Docking.Panels;
using Xunit;

namespace Editor.Tests.Shell.Docking.Panels;

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
    public void CreateTab_notifies_lifecycle_sink_that_panel_is_attached()
    {
        var events = new List<string>();
        var content = new RecordingPanelLifecycleSink("content", events);
        var descriptor = CreateDescriptor(
            "panel",
            DockContentCachePolicy.KeepAlive,
            () => content);
        var manager = new PanelInstanceManager();

        manager.CreateTab(descriptor);

        Assert.Equal(["content:Attached:panel:Left:Main"], events);
    }

    [Fact]
    public void ReleaseTab_detaches_lifecycle_sink_before_disposing_content()
    {
        var events = new List<string>();
        var content = new RecordingPanelLifecycleSink("content", events);
        var descriptor = CreateDescriptor(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => content);
        var manager = new PanelInstanceManager();
        var tab = manager.CreateTab(descriptor);
        events.Clear();

        tab.ReleasePanelInstance();

        Assert.Equal(["content:Detached:panel:Left:Main", "content:Disposed"], events);
    }

    [Fact]
    public void ReleaseTab_deactivates_and_hides_before_detaching()
    {
        var events = new List<string>();
        var content = new RecordingPanelLifecycleSink("content", events);
        var descriptor = CreateDescriptor(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => content);
        var manager = new PanelInstanceManager();
        var tab = manager.CreateTab(descriptor);
        tab.ShowPanelInstance();
        tab.ActivatePanelInstance();
        events.Clear();

        tab.ReleasePanelInstance();

        Assert.Equal(
            [
                "content:Deactivated:panel:Left:Main",
                "content:Hidden:panel:Left:Main",
                "content:Detached:panel:Left:Main",
                "content:Disposed",
            ],
            events);
    }

    [Fact]
    public void Show_callback_failure_does_not_prevent_scheduler_visibility()
    {
        var scheduler = new EditorPanelFrameScheduler();
        var content = new ThrowingPanelLifecycleSink(
            EditorPanelFrameUpdateRequest.Visible())
        {
            ThrowOnShown = true,
        };
        var manager = new PanelInstanceManager(scheduler);
        var tab = manager.CreateTab(CreateDescriptor(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => content));

        var exception = Assert.Throws<InvalidOperationException>(tab.ShowPanelInstance);

        Assert.Same(content.ShownFailure, exception);
        Assert.Single(scheduler.Tick(DateTimeOffset.UnixEpoch));
        tab.ReleasePanelInstance();
    }

    [Fact]
    public void Hide_callback_failure_does_not_prevent_scheduler_visibility_cleanup()
    {
        var scheduler = new EditorPanelFrameScheduler();
        var content = new ThrowingPanelLifecycleSink(
            EditorPanelFrameUpdateRequest.Visible());
        var manager = new PanelInstanceManager(scheduler);
        var tab = manager.CreateTab(CreateDescriptor(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => content));
        tab.ShowPanelInstance();
        content.ThrowOnHidden = true;

        var exception = Assert.Throws<InvalidOperationException>(tab.HidePanelInstance);

        Assert.Same(content.HiddenFailure, exception);
        Assert.Empty(scheduler.Tick(DateTimeOffset.UnixEpoch));
        tab.ReleasePanelInstance();
    }

    [Fact]
    public void Deactivate_callback_failure_does_not_prevent_scheduler_activation_cleanup()
    {
        var scheduler = new EditorPanelFrameScheduler();
        var content = new ThrowingPanelLifecycleSink(
            EditorPanelFrameUpdateRequest.Active());
        var manager = new PanelInstanceManager(scheduler);
        var tab = manager.CreateTab(CreateDescriptor(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => content));
        tab.ShowPanelInstance();
        tab.ActivatePanelInstance();
        content.ThrowOnDeactivated = true;

        var exception = Assert.Throws<InvalidOperationException>(tab.DeactivatePanelInstance);

        Assert.Same(content.DeactivatedFailure, exception);
        Assert.Empty(scheduler.Tick(DateTimeOffset.UnixEpoch));
        tab.ReleasePanelInstance();
    }

    [Fact]
    public void Release_aggregates_detach_and_dispose_failures_after_scheduler_cleanup()
    {
        var scheduler = new EditorPanelFrameScheduler();
        var content = new ThrowingPanelLifecycleSink(
            EditorPanelFrameUpdateRequest.Visible())
        {
            ThrowOnDetached = true,
            ThrowOnDispose = true,
        };
        var manager = new PanelInstanceManager(scheduler);
        var tab = manager.CreateTab(CreateDescriptor(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => content));
        tab.ShowPanelInstance();

        var exception = Assert.Throws<AggregateException>(tab.ReleasePanelInstance);

        Assert.Collection(
            exception.InnerExceptions,
            item => Assert.Same(content.DetachedFailure, item),
            item => Assert.Same(content.DisposeFailure, item));
        Assert.Equal(1, content.DisposeCount);
        Assert.Empty(scheduler.Tick(DateTimeOffset.UnixEpoch));

        tab.ReleasePanelInstance();
        Assert.Equal(1, content.DisposeCount);
    }

    [Fact]
    public void CreateTab_attach_failure_releases_content_and_scheduler_registration()
    {
        var scheduler = new EditorPanelFrameScheduler();
        var content = new ThrowingPanelLifecycleSink(
            EditorPanelFrameUpdateRequest.Visible())
        {
            ThrowOnAttached = true,
        };
        var manager = new PanelInstanceManager(scheduler);

        var exception = Assert.Throws<InvalidOperationException>(
            () => manager.CreateTab(CreateDescriptor(
                "panel",
                DockContentCachePolicy.RecreateOnOpen,
                () => content)));

        Assert.Same(content.AttachedFailure, exception);
        Assert.Equal(1, content.DisposeCount);
        var context = new EditorPanelLifecycleContext(
            "panel",
            "panel",
            EditorDockArea.Left,
            IsFloatingWorkspace: false);
        scheduler.ShowPanel(context);
        Assert.Empty(scheduler.Tick(DateTimeOffset.UnixEpoch));
    }

    [Fact]
    public void CreateTab_aggregates_attach_and_cleanup_failures_in_order()
    {
        var scheduler = new EditorPanelFrameScheduler();
        var content = new ThrowingPanelLifecycleSink(
            EditorPanelFrameUpdateRequest.Visible())
        {
            ThrowOnAttached = true,
            ThrowOnDetached = true,
            ThrowOnDispose = true,
        };
        var manager = new PanelInstanceManager(scheduler);

        var exception = Assert.Throws<AggregateException>(
            () => manager.CreateTab(CreateDescriptor(
                "panel",
                DockContentCachePolicy.RecreateOnOpen,
                () => content)));

        Assert.Collection(
            exception.InnerExceptions,
            item => Assert.Same(content.AttachedFailure, item),
            item => Assert.Same(content.DetachedFailure, item),
            item => Assert.Same(content.DisposeFailure, item));
        Assert.Equal(1, content.DisposeCount);
        var context = new EditorPanelLifecycleContext(
            "panel",
            "panel",
            EditorDockArea.Left,
            IsFloatingWorkspace: false);
        scheduler.ShowPanel(context);
        Assert.Empty(scheduler.Tick(DateTimeOffset.UnixEpoch));
    }

    [Fact]
    public void Dispose_attempts_all_keep_alive_contents_and_clears_cache_after_failures()
    {
        var first = new ThrowingPanelLifecycleSink(EditorPanelFrameUpdateRequest.Manual)
        {
            ThrowOnDispose = true,
        };
        var second = new ThrowingPanelLifecycleSink(EditorPanelFrameUpdateRequest.Manual)
        {
            ThrowOnDispose = true,
        };
        var manager = new PanelInstanceManager();
        var firstTab = manager.CreateTab(CreateDescriptor(
            "first",
            DockContentCachePolicy.KeepAlive,
            () => first));
        var secondTab = manager.CreateTab(CreateDescriptor(
            "second",
            DockContentCachePolicy.KeepAlive,
            () => second));
        firstTab.ReleasePanelInstance();
        secondTab.ReleasePanelInstance();

        var exception = Assert.Throws<AggregateException>(manager.Dispose);

        Assert.Collection(
            exception.InnerExceptions,
            item => Assert.Same(first.DisposeFailure, item),
            item => Assert.Same(second.DisposeFailure, item));
        Assert.Equal(1, first.DisposeCount);
        Assert.Equal(1, second.DisposeCount);

        manager.Dispose();
        Assert.Equal(1, first.DisposeCount);
        Assert.Equal(1, second.DisposeCount);
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
        Assert.Equal(EditorDockArea.Left, tab.Area);
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
            EditorDockArea.Left,
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

    private sealed class RecordingPanelLifecycleSink(
        string name,
        List<string> events) :
        IEditorPanelLifecycleSink,
        IEditorPanelVisibilitySink,
        IDisposable
    {
        public void OnPanelAttached(EditorPanelLifecycleContext context)
        {
            events.Add($"{name}:Attached:{context.PanelId}:{context.DockArea}:{GetHostKind(context)}");
        }

        public void OnPanelActivated(EditorPanelLifecycleContext context)
        {
            events.Add($"{name}:Activated:{context.PanelId}:{context.DockArea}:{GetHostKind(context)}");
        }

        public void OnPanelDeactivated(EditorPanelLifecycleContext context)
        {
            events.Add($"{name}:Deactivated:{context.PanelId}:{context.DockArea}:{GetHostKind(context)}");
        }

        public void OnPanelDetached(EditorPanelLifecycleContext context)
        {
            events.Add($"{name}:Detached:{context.PanelId}:{context.DockArea}:{GetHostKind(context)}");
        }

        public void OnPanelShown(EditorPanelLifecycleContext context)
        {
            events.Add($"{name}:Shown:{context.PanelId}:{context.DockArea}:{GetHostKind(context)}");
        }

        public void OnPanelHidden(EditorPanelLifecycleContext context)
        {
            events.Add($"{name}:Hidden:{context.PanelId}:{context.DockArea}:{GetHostKind(context)}");
        }

        public void Dispose()
        {
            events.Add($"{name}:Disposed");
        }

        private static string GetHostKind(EditorPanelLifecycleContext context)
        {
            return context.IsFloatingWorkspace ? "Floating" : "Main";
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

    private sealed class ThrowingPanelLifecycleSink(
        EditorPanelFrameUpdateRequest frameUpdateRequest) :
        IEditorPanelLifecycleSink,
        IEditorPanelVisibilitySink,
        IEditorPanelFrameUpdateSink,
        IDisposable
    {
        public InvalidOperationException AttachedFailure { get; } =
            new("attached failure");

        public InvalidOperationException ShownFailure { get; } =
            new("shown failure");

        public InvalidOperationException HiddenFailure { get; } =
            new("hidden failure");

        public InvalidOperationException DeactivatedFailure { get; } =
            new("deactivated failure");

        public InvalidOperationException DetachedFailure { get; } =
            new("detached failure");

        public InvalidOperationException DisposeFailure { get; } =
            new("dispose failure");

        public bool ThrowOnShown { get; init; }

        public bool ThrowOnAttached { get; init; }

        public bool ThrowOnHidden { get; set; }

        public bool ThrowOnDeactivated { get; set; }

        public bool ThrowOnDetached { get; init; }

        public bool ThrowOnDispose { get; init; }

        public int DisposeCount { get; private set; }

        public EditorPanelFrameUpdateRequest FrameUpdateRequest { get; } = frameUpdateRequest;

        public void OnPanelAttached(EditorPanelLifecycleContext context)
        {
            if (ThrowOnAttached)
            {
                throw AttachedFailure;
            }
        }

        public void OnPanelActivated(EditorPanelLifecycleContext context)
        {
        }

        public void OnPanelDeactivated(EditorPanelLifecycleContext context)
        {
            if (ThrowOnDeactivated)
            {
                throw DeactivatedFailure;
            }
        }

        public void OnPanelDetached(EditorPanelLifecycleContext context)
        {
            if (ThrowOnDetached)
            {
                throw DetachedFailure;
            }
        }

        public void OnPanelShown(EditorPanelLifecycleContext context)
        {
            if (ThrowOnShown)
            {
                throw ShownFailure;
            }
        }

        public void OnPanelHidden(EditorPanelLifecycleContext context)
        {
            if (ThrowOnHidden)
            {
                throw HiddenFailure;
            }
        }

        public void OnEditorPanelFrame(EditorPanelFrameContext context)
        {
        }

        public void Dispose()
        {
            DisposeCount++;
            if (ThrowOnDispose)
            {
                throw DisposeFailure;
            }
        }
    }
}
