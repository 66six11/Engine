using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;

namespace Editor.Shell.Composition;

internal sealed class StudioViewportSmokeHost : IAsyncDisposable
{
    private static readonly TimeSpan kWarmUpTimeout = TimeSpan.FromSeconds(20);
    private readonly ViewportRuntimeBridge runtime_ = new();
    private readonly ViewportPresentationLifetime lifetime_ = new();
    private readonly List<ViewportSession> sessions_ = [];
    private readonly List<ViewportCompositionControl> controls_ = [];
    private Window? window_;
    private bool runtimeStarted_;

    public ViewportPresentationLifetime Lifetime => lifetime_;

    public async Task WarmUpRuntimeAsync()
    {
        var failure = await runtime_.WarmUpAsync();
        if (failure is not null)
        {
            throw new InvalidOperationException(failure.Message);
        }
        runtimeStarted_ = true;
    }

    public ViewportSession CreateSceneSession(string fileName)
    {
        var document = CreateSceneDocument(fileName);
        return CreateSession(document, ViewportRenderKind.Scene);
    }

    public ViewportSession CreateSceneSession(SceneDocumentSnapshot document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return CreateSession(document, ViewportRenderKind.Scene);
    }

    public (ViewportSession First, ViewportSession Second) CreateSceneSessionPair(
        string fileName)
    {
        var document = CreateSceneDocument(fileName);
        return (
            CreateSession(document, ViewportRenderKind.Scene),
            CreateSession(document, ViewportRenderKind.Scene));
    }

    public (ViewportSession Scene, ViewportSession Game) CreateSceneGameSessionPair(
        string fileName)
    {
        var document = CreateSceneDocument(fileName);
        return (
            CreateSession(document, ViewportRenderKind.Scene),
            CreateSession(document, ViewportRenderKind.Game));
    }

    public ViewportCompositionControl CreateControl(
        ViewportSession session,
        bool isRealtime = true,
        ViewportCompositionControlTestHooks? testHooks = null)
    {
        ArgumentNullException.ThrowIfNull(session);
        var control = new ViewportCompositionControl(Task.CompletedTask, testHooks)
        {
            Session = session,
            Lifetime = lifetime_,
            IsRealtime = isRealtime,
        };
        controls_.Add(control);
        return control;
    }

    public void Show(
        IClassicDesktopStyleApplicationLifetime desktop,
        Control content,
        string title,
        double width = 1280,
        double height = 720)
    {
        ArgumentNullException.ThrowIfNull(desktop);
        ArgumentNullException.ThrowIfNull(content);
        if (window_ is not null)
        {
            throw new InvalidOperationException("The viewport smoke window is already visible.");
        }

        window_ = new Window
        {
            Width = width,
            Height = height,
            Title = title,
            Content = content,
        };
        desktop.MainWindow = window_;
        window_.Show();
    }

    public static async Task WaitForWarmUpAsync(
        IReadOnlyList<ViewportCompositionControl> controls,
        ulong minimumFrames = 30)
    {
        ArgumentNullException.ThrowIfNull(controls);
        using var deadline = new CancellationTokenSource(kWarmUpTimeout);
        while (true)
        {
            var allReady = true;
            foreach (var control in controls)
            {
                if (control.IsDegraded)
                {
                    throw new InvalidOperationException(control.StatusMessage);
                }
                allReady &= control.PresentationMetrics.TotalPresentedFrames >= minimumFrames &&
                            control.PresentationGeometryMetrics.CurrentSurfaceIsExact;
            }
            if (allReady)
            {
                return;
            }
            await Task.Delay(TimeSpan.FromMilliseconds(10), deadline.Token);
        }
    }

    public async ValueTask DisposeAsync()
    {
        foreach (var control in controls_)
        {
            control.Session = null;
        }
        if (window_ is not null)
        {
            window_.Content = null;
            window_.Close();
            window_ = null;
        }
        foreach (var session in sessions_)
        {
            session.Close();
        }
        sessions_.Clear();
        await lifetime_.StopAndDrainAsync();
        if (runtimeStarted_)
        {
            runtime_.Shutdown();
            runtimeStarted_ = false;
        }
    }

    private static SceneDocumentSnapshot CreateSceneDocument(string fileName) =>
        new(
            Guid.NewGuid(),
            fileName,
            revision: 1,
            savedRevision: 1,
            entities: []);

    private ViewportSession CreateSession(
        SceneDocumentSnapshot document,
        ViewportRenderKind kind)
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            kind,
            document,
            ViewportCameraSnapshot.DefaultScene);
        sessions_.Add(session);
        return session;
    }
}
