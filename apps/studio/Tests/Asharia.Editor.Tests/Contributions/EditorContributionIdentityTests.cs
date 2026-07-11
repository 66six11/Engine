using Asharia.Editor.Contributions;
using Xunit;

namespace Asharia.Editor.Tests.Contributions;

public sealed class EditorContributionIdentityTests
{
    [Theory]
    [InlineData("terrain.main-panel", true)]
    [InlineData("render.frame-debugger", true)]
    [InlineData("Terrain.MainPanel", false)]
    [InlineData("terrain", false)]
    [InlineData("terrain..panel", false)]
    [InlineData("terrain_panel.main", false)]
    public void Contribution_id_uses_lowercase_namespaced_syntax(string value, bool valid)
    {
        Assert.Equal(valid, EditorContributionId.TryCreate(value, out _));
    }

    [Theory]
    [InlineData("terrain.panel.main-content", true)]
    [InlineData("terrain.panel", true)]
    [InlineData("Terrain.Panel", false)]
    [InlineData("panel", false)]
    [InlineData("terrain.panel.", false)]
    public void Factory_local_id_uses_lowercase_namespaced_syntax(string value, bool valid)
    {
        Assert.Equal(valid, EditorFactoryLocalId.TryCreate(value, out _));
    }

    [Theory]
    [InlineData("code-first", true)]
    [InlineData("avalonia", true)]
    [InlineData("CodeFirst", false)]
    [InlineData("code_first", false)]
    [InlineData("code--first", false)]
    [InlineData("-code", false)]
    [InlineData("code-", false)]
    public void Backend_id_uses_lowercase_kebab_syntax(string value, bool valid)
    {
        Assert.Equal(valid, UiBackendId.TryCreate(value, out _));
    }

    [Fact]
    public void Default_identities_are_invalid_and_render_empty()
    {
        Assert.False(default(EditorContributionId).IsValid);
        Assert.False(default(EditorFactoryLocalId).IsValid);
        Assert.False(default(UiBackendId).IsValid);
        Assert.Equal(string.Empty, default(EditorContributionId).ToString());
        Assert.Equal(string.Empty, default(EditorFactoryLocalId).ToString());
        Assert.Equal(string.Empty, default(UiBackendId).ToString());
    }

    [Fact]
    public void Code_first_backend_has_exact_stable_value()
    {
        Assert.True(UiBackendId.CodeFirst.IsValid);
        Assert.Equal("code-first", UiBackendId.CodeFirst.Value);
        Assert.Equal(UiBackendId.Create("code-first"), UiBackendId.CodeFirst);
    }
}
