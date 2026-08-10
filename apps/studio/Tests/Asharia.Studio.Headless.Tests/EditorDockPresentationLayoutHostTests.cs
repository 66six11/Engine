using System.Threading.Tasks;
using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Layout;
using Avalonia.Threading;
using Asharia.Studio.Presentation.Avalonia.Windowing;
using Editor.Shell.Views.Docking;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class EditorDockPresentationLayoutHostTests
{
    [AvaloniaFact]
    public async Task Non_precommitted_outer_arrange_uses_the_exact_fallback_without_retained_crop()
    {
        var child = new Border();
        var host = new EditorDockPresentationLayoutHost { Child = child };
        var window = new Window
        {
            Width = 640,
            Height = 480,
            Content = host,
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            await host.WhenIdleAsync();
            var committed = child.Bounds.Size;
            Assert.Equal(host.Bounds.Size, committed);

            window.Width = 800;
            window.Height = 600;
            var requested = new Size(committed.Width + 160, committed.Height + 120);
            host.Measure(requested);
            host.Arrange(new Rect(requested));

            Assert.Equal(requested, child.Bounds.Size);
            Assert.False(host.CaptureMetrics().HasQueued);

            Dispatcher.UIThread.RunJobs();
            await host.WhenIdleAsync();

            Assert.Equal(host.Bounds.Size, child.Bounds.Size);
            var metrics = host.CaptureMetrics();
            Assert.Equal(host.Bounds.Size, metrics.CommittedSize);
            Assert.Equal(0UL, metrics.PublishedRequests);
            Assert.False(metrics.HasActive);
            Assert.False(metrics.HasQueued);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void Zero_sized_interval_keeps_the_last_committed_child_for_restore()
    {
        var child = new Border();
        var host = new EditorDockPresentationLayoutHost { Child = child };
        host.Measure(new Size(640, 480));
        host.Arrange(new Rect(new Size(640, 480)));

        host.Measure(default);
        host.Arrange(new Rect(default(Size)));

        Assert.Equal(new Size(640, 480), child.Bounds.Size);
        Assert.Equal(new Size(640, 480), host.CaptureMetrics().CommittedSize);
    }

    [Fact]
    public void Layout_size_equality_uses_Avalonia_layout_epsilon()
    {
        Assert.True(EditorDockPresentationLayoutHost.AreLayoutSizesEqual(
            new Size(640, 480),
            new Size(640 + (LayoutHelper.LayoutEpsilon / 2), 480)));
        Assert.False(EditorDockPresentationLayoutHost.AreLayoutSizesEqual(
            new Size(640, 480),
            new Size(641, 480)));
    }

    [AvaloniaFact]
    public void Shared_main_and_floating_workspace_view_owns_the_presentation_layout_host()
    {
        var workspace = new EditorDockWorkspaceView();

        Assert.NotNull(
            workspace.FindControl<EditorDockPresentationLayoutHost>("PresentationLayoutHost"));
    }

    [Theory]
    [InlineData(1.0)]
    [InlineData(1.25)]
    [InlineData(1.5)]
    [InlineData(2.0)]
    public void Interactive_resize_projection_preserves_exact_client_pixels_across_fixed_scaling(
        double scaling)
    {
        var result = InteractiveTopLevelResizeProjection.TryProjectWorkspaceTarget(
            new Size(1200, 800),
            new Size(1184, 761),
            new Size(1300, 900),
            new Size(0, 48),
            scaling,
            out var target);

        Assert.True(result);
        Assert.Equal(1284 / scaling, target.Width, precision: 8);
        Assert.Equal((861 / scaling) - 48, target.Height, precision: 8);
    }

    [Fact]
    public void Interactive_resize_projection_preserves_a_one_pixel_resize_at_125_percent_scaling()
    {
        var result = InteractiveTopLevelResizeProjection.TryProjectWorkspaceTarget(
            new Size(1200, 800),
            new Size(1184, 761),
            new Size(1201, 800),
            new Size(0, 0),
            1.25,
            out var target);

        Assert.True(result);
        Assert.Equal(1185 / 1.25, target.Width, precision: 8);
        Assert.Equal(761 / 1.25, target.Height, precision: 8);
    }

    [AvaloniaFact]
    public async Task Stale_outer_commit_is_dropped_before_layout_or_publication()
    {
        var child = new Border();
        var host = new EditorDockPresentationLayoutHost { Child = child };
        var window = new Window { Width = 640, Height = 480, Content = host };
        var applied = 0;
        var rolledBack = 0;
        var accepted = 0;

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            await host.WhenIdleAsync();
            var original = host.Bounds.Size;
            var target = new Size(original.Width + 80, original.Height + 60);

            Assert.True(host.TryQueuePrecommittedWindowResize(
                target,
                new TestInteractiveTopLevelResizeCommit(
                    () => applied++,
                    () => rolledBack++,
                    () => accepted++,
                    isCurrent: () => false)));

            Dispatcher.UIThread.RunJobs();
            await host.WhenIdleAsync();

            Assert.Equal(0, applied);
            Assert.Equal(0, rolledBack);
            Assert.Equal(0, accepted);
            Assert.Equal(original, host.CaptureMetrics().CommittedSize);
            Assert.Equal(original, child.Bounds.Size);
            Assert.Equal(0UL, host.CaptureMetrics().PublishedRequests);
            Assert.False(host.CaptureMetrics().HasActive);
            Assert.False(host.CaptureMetrics().HasQueued);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public async Task Precommitted_outer_layout_is_accepted_only_after_apply()
    {
        var child = new Border();
        var host = new EditorDockPresentationLayoutHost { Child = child };
        var window = new Window { Width = 640, Height = 480, Content = host };
        var accepted = 0;
        var rolledBack = 0;

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var original = host.Bounds.Size;
            var target = new Size(original.Width + 80, original.Height + 60);
            Assert.True(host.TryQueuePrecommittedWindowResize(
                target,
                new TestInteractiveTopLevelResizeCommit(
                    () =>
                    {
                        host.Measure(target);
                        host.Arrange(new Rect(target));
                    },
                    () =>
                    {
                        rolledBack++;
                        host.Measure(original);
                        host.Arrange(new Rect(original));
                    },
                    () => accepted++)));

            Dispatcher.UIThread.RunJobs();
            await host.WhenIdleAsync();

            Assert.Equal(1, accepted);
            Assert.Equal(0, rolledBack);
            Assert.Equal(target, host.Bounds.Size);
            Assert.Equal(target, child.Bounds.Size);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public async Task Prepublish_outer_failure_rolls_back_and_is_not_accepted()
    {
        var child = new Border();
        var host = new EditorDockPresentationLayoutHost { Child = child };
        var window = new Window { Width = 640, Height = 480, Content = host };
        var accepted = 0;
        var rolledBack = 0;

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var original = host.Bounds.Size;
            var target = new Size(original.Width + 80, original.Height + 60);
            Assert.True(host.TryQueuePrecommittedWindowResize(
                target,
                new TestInteractiveTopLevelResizeCommit(
                    () => throw new InvalidOperationException("injected apply failure"),
                    () => rolledBack++,
                    () => accepted++)));

            Dispatcher.UIThread.RunJobs();
            await host.WhenIdleAsync();

            Assert.Equal(0, accepted);
            Assert.Equal(1, rolledBack);
            Assert.Equal(original, host.CaptureMetrics().CommittedSize);
            Assert.Equal(original, child.Bounds.Size);
            Assert.Equal(0UL, host.CaptureMetrics().PublishedRequests);
        }
        finally
        {
            window.Close();
        }
    }

    private sealed class TestInteractiveTopLevelResizeCommit(
        Action apply,
        Action rollback,
        Action? accept = null,
        Func<bool>? isCurrent = null) : IInteractiveTopLevelResizeCommit
    {
        public void Apply() => apply();

        public void Rollback() => rollback();

        public void Accept() => accept?.Invoke();

        public bool IsCurrent() => isCurrent?.Invoke() ?? true;
    }
}
