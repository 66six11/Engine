using System;
using System.Collections.Generic;
using Asharia.Editor.Panels;
using Asharia.Studio.Application.Panels;
using Editor.Core.Models.Panels;
using Editor.Shell.ViewModels.Docking;

namespace Editor.Shell.Docking.Panels;

internal sealed class PanelInstanceManager : IDisposable
{
    private readonly Dictionary<string, object> keptAliveContentByPanelId_ =
        new(StringComparer.Ordinal);
    private readonly EditorPanelFrameScheduler? frameScheduler_;

    public PanelInstanceManager(EditorPanelFrameScheduler? frameScheduler = null)
    {
        frameScheduler_ = frameScheduler;
    }

    public EditorDockTabViewModel CreateTab(
        PanelDescriptor descriptor,
        bool isFloatingWorkspace = false,
        EditorDockArea? initialArea = null)
    {
        ArgumentNullException.ThrowIfNull(descriptor);

        var content = GetOrCreateContent(descriptor);
        var tab = new EditorDockTabViewModel(
            descriptor.Id,
            descriptor.Title,
            GetTag(descriptor),
            GetTitleDetail(descriptor),
            GetStatusText(descriptor),
            descriptor.Kind,
            initialArea ?? descriptor.DefaultArea,
            content,
            descriptor.IconKey,
            new PanelInstanceRelease(this, descriptor, content),
            frameScheduler_);
        tab.AttachPanelInstance(isFloatingWorkspace);
        return tab;
    }

    public void Dispose()
    {
        foreach (var content in keptAliveContentByPanelId_.Values)
        {
            DisposeContent(content);
        }

        keptAliveContentByPanelId_.Clear();
    }

    private object GetOrCreateContent(PanelDescriptor descriptor)
    {
        if (descriptor.CachePolicy == DockContentCachePolicy.RecreateOnOpen)
        {
            return descriptor.CreateContent();
        }

        if (!keptAliveContentByPanelId_.TryGetValue(descriptor.Id, out var content))
        {
            content = descriptor.CreateContent();
            keptAliveContentByPanelId_[descriptor.Id] = content;
        }

        return content;
    }

    private void ReleaseTabContent(PanelDescriptor descriptor, object content)
    {
        if (descriptor.CachePolicy == DockContentCachePolicy.RecreateOnOpen)
        {
            DisposeContent(content);
        }
    }

    private static void DisposeContent(object content)
    {
        if (content is IDisposable disposable)
        {
            disposable.Dispose();
        }
    }

    private static string GetTag(PanelDescriptor descriptor)
    {
        return descriptor.Tag
            ?? (descriptor.Kind == PanelKind.Document ? "DOC" : descriptor.DefaultArea.ToString().ToUpperInvariant());
    }

    private static string GetTitleDetail(PanelDescriptor descriptor)
    {
        return descriptor.TitleDetail ?? descriptor.MenuPath;
    }

    private static string GetStatusText(PanelDescriptor descriptor)
    {
        return descriptor.StatusText ?? descriptor.Kind.ToString().ToLowerInvariant();
    }

    private sealed class PanelInstanceRelease(
        PanelInstanceManager manager,
        PanelDescriptor descriptor,
        object content) : IDisposable
    {
        private PanelInstanceManager? manager_ = manager;

        public void Dispose()
        {
            manager_?.ReleaseTabContent(descriptor, content);
            manager_ = null;
        }
    }
}
