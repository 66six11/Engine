using System;
using System.Linq;
using Asharia.Editor.Contributions;
using Asharia.Editor.Panels;
using Xunit;

namespace Asharia.Editor.Tests.Panels;

public sealed class EditorPanelDescriptorTests
{
    [Fact]
    public void Panel_enums_have_stable_names_and_values()
    {
        Assert.Equal(["Document", "Tool"], Enum.GetNames<EditorPanelKind>());
        Assert.Equal([0, 1], Enum.GetValues<EditorPanelKind>().Select(value => (int)value));
        Assert.Equal(["Center", "Left", "Right", "Bottom"], Enum.GetNames<EditorDockPreference>());
        Assert.Equal([0, 1, 2, 3], Enum.GetValues<EditorDockPreference>().Select(value => (int)value));
        Assert.Equal(["RecreateOnOpen", "KeepAlive"], Enum.GetNames<EditorPanelCachePolicy>());
        Assert.Equal([0, 1], Enum.GetValues<EditorPanelCachePolicy>().Select(value => (int)value));
    }

    [Fact]
    public void Descriptor_preserves_valid_contract_values()
    {
        var descriptor = CreateDescriptor();

        Assert.Equal(EditorContributionId.Create("terrain.main-panel"), descriptor.Id);
        Assert.Equal(" Terrain ", descriptor.Title);
        Assert.Equal(EditorPanelKind.Tool, descriptor.Kind);
        Assert.Equal(EditorDockPreference.Right, descriptor.DefaultDock);
        Assert.Equal(EditorPanelCachePolicy.KeepAlive, descriptor.CachePolicy);
        Assert.Equal(UiBackendId.CodeFirst, descriptor.Backend);
        Assert.Equal(
            EditorFactoryLocalId.Create("terrain.panel.main-content"),
            descriptor.ContentFactory);
    }

    [Fact]
    public void Descriptor_rejects_invalid_fields()
    {
        var valid = CreateDescriptor();

        Assert.Throws<ArgumentException>(() => new EditorPanelDescriptor(
            default,
            valid.Title,
            valid.Kind,
            valid.DefaultDock,
            valid.CachePolicy,
            valid.Backend,
            valid.ContentFactory));
        Assert.Throws<ArgumentException>(() => new EditorPanelDescriptor(
            valid.Id,
            "   ",
            valid.Kind,
            valid.DefaultDock,
            valid.CachePolicy,
            valid.Backend,
            valid.ContentFactory));
        Assert.Throws<ArgumentException>(() => new EditorPanelDescriptor(
            valid.Id,
            null!,
            valid.Kind,
            valid.DefaultDock,
            valid.CachePolicy,
            valid.Backend,
            valid.ContentFactory));
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new EditorPanelDescriptor(
                valid.Id,
                valid.Title,
                (EditorPanelKind)42,
                valid.DefaultDock,
                valid.CachePolicy,
                valid.Backend,
                valid.ContentFactory));
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new EditorPanelDescriptor(
                valid.Id,
                valid.Title,
                valid.Kind,
                (EditorDockPreference)42,
                valid.CachePolicy,
                valid.Backend,
                valid.ContentFactory));
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new EditorPanelDescriptor(
                valid.Id,
                valid.Title,
                valid.Kind,
                valid.DefaultDock,
                (EditorPanelCachePolicy)42,
                valid.Backend,
                valid.ContentFactory));
        Assert.Throws<ArgumentException>(() => new EditorPanelDescriptor(
            valid.Id,
            valid.Title,
            valid.Kind,
            valid.DefaultDock,
            valid.CachePolicy,
            default,
            valid.ContentFactory));
        Assert.Throws<ArgumentException>(() => new EditorPanelDescriptor(
            valid.Id,
            valid.Title,
            valid.Kind,
            valid.DefaultDock,
            valid.CachePolicy,
            valid.Backend,
            default));
    }

    private static EditorPanelDescriptor CreateDescriptor()
    {
        return new EditorPanelDescriptor(
            EditorContributionId.Create("terrain.main-panel"),
            " Terrain ",
            EditorPanelKind.Tool,
            EditorDockPreference.Right,
            EditorPanelCachePolicy.KeepAlive,
            UiBackendId.CodeFirst,
            EditorFactoryLocalId.Create("terrain.panel.main-content"));
    }
}
