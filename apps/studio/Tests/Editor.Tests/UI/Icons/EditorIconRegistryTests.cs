using Editor.UI.Icons;
using Lucide.Avalonia;
using Xunit;

namespace Editor.Tests.UI.Icons;

public sealed class EditorIconRegistryTests
{
    [Fact]
    public void RegisterLucide_records_icon_kind_for_key()
    {
        var registry = new EditorIconRegistry();
        registry.RegisterLucide("plugin.example.tool", LucideIconKind.Check);

        var found = registry.TryGetLucideKind("plugin.example.tool", out var kind);

        Assert.True(found);
        Assert.Equal(LucideIconKind.Check, kind);
    }

    [Fact]
    public void CreateIcon_returns_null_for_unknown_key()
    {
        var registry = new EditorIconRegistry();

        Assert.Null(registry.CreateIcon("missing.icon"));
        Assert.Null(registry.CreateIcon(null));
    }

    [Fact]
    public void Default_contains_builtin_editor_icon_keys()
    {
        Assert.True(EditorIconRegistry.Default.ContainsIcon(EditorIconKey.UiCheck));
        Assert.True(EditorIconRegistry.Default.ContainsIcon(EditorIconKey.UiChevronDown));
        Assert.True(EditorIconRegistry.Default.ContainsIcon(EditorIconKey.UiChevronRight));
        Assert.True(EditorIconRegistry.Default.ContainsIcon(EditorIconKey.UiClose));
        Assert.True(EditorIconRegistry.Default.ContainsIcon(EditorIconKey.UiSearch));
        Assert.True(EditorIconRegistry.Default.ContainsIcon(EditorIconKey.PanelSceneView));
        Assert.True(EditorIconRegistry.Default.ContainsIcon(EditorIconKey.PanelHierarchy));
        Assert.True(EditorIconRegistry.Default.ContainsIcon(EditorIconKey.PanelInspector));
        Assert.True(EditorIconRegistry.Default.ContainsIcon(EditorIconKey.PanelConsole));
        Assert.True(EditorIconRegistry.Default.ContainsIcon(EditorIconKey.PanelProblems));
        Assert.True(EditorIconRegistry.Default.ContainsIcon(EditorIconKey.PanelFrameDebugger));
        Assert.True(EditorIconRegistry.Default.ContainsIcon(EditorIconKey.ObjectDefault));
    }

    [Fact]
    public void Default_uses_lucide_chevron_icons_for_hierarchy_expanders()
    {
        Assert.True(EditorIconRegistry.Default.TryGetLucideKind(EditorIconKey.UiChevronDown, out var downKind));
        Assert.True(EditorIconRegistry.Default.TryGetLucideKind(EditorIconKey.UiChevronRight, out var rightKind));
        Assert.Equal(LucideIconKind.ChevronDown, downKind);
        Assert.Equal(LucideIconKind.ChevronRight, rightKind);
    }
}
