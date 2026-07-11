using System;
using System.Collections.Generic;
using Asharia.Editor.Contributions;
using Asharia.Editor.Extensions;
using Asharia.Editor.Panels;
using Xunit;

namespace Asharia.Editor.Tests.Panels;

public sealed class EditorPanelContributionBuilderTests
{
    [Fact]
    public void Panels_are_ordered_and_frozen_with_the_module_declaration()
    {
        var builder = CreateBuilder("terrain.editor", EditorModuleScopeKind.Project);
        var main = CreatePanel("terrain.main-panel", "terrain.factory.main");
        var details = CreatePanel("terrain.details-panel", "terrain.factory.details");

        builder.Panels.Add(main);
        builder.Panels.Add(details);
        var declaration = builder.Build();

        Assert.Same(declaration, builder.Build());
        Assert.Equal([main, details], declaration.Panels);
        var collection = Assert.IsAssignableFrom<ICollection<EditorPanelDescriptor>>(declaration.Panels);
        Assert.True(collection.IsReadOnly);
        Assert.Throws<NotSupportedException>(() => collection.Add(main));
        Assert.Throws<InvalidOperationException>(() => builder.Panels.Add(
            CreatePanel("terrain.other-panel", "terrain.factory.other")));
    }

    [Fact]
    public void Panels_reject_duplicate_contribution_and_factory_ids()
    {
        var duplicateContribution = CreateBuilder("terrain.editor", EditorModuleScopeKind.Project);
        duplicateContribution.Panels.Add(
            CreatePanel("terrain.main-panel", "terrain.factory.main"));
        Assert.Throws<InvalidOperationException>(() => duplicateContribution.Panels.Add(
            CreatePanel("terrain.main-panel", "terrain.factory.other")));

        var duplicateFactory = CreateBuilder("terrain.editor", EditorModuleScopeKind.Project);
        duplicateFactory.Panels.Add(
            CreatePanel("terrain.main-panel", "terrain.factory.main"));
        Assert.Throws<InvalidOperationException>(() => duplicateFactory.Panels.Add(
            CreatePanel("terrain.other-panel", "terrain.factory.main")));
    }

    [Theory]
    [InlineData(EditorModuleScopeKind.Application)]
    [InlineData(EditorModuleScopeKind.Project)]
    public void Panel_scope_comes_only_from_module_definition(EditorModuleScopeKind scope)
    {
        var builder = CreateBuilder("terrain.editor", scope);
        builder.Panels.Add(CreatePanel("terrain.main-panel", "terrain.factory.main"));

        var declaration = builder.Build();

        Assert.Equal(scope, declaration.DefinitionContext.DefinitionId.Scope);
        Assert.DoesNotContain(
            typeof(EditorPanelDescriptor).GetProperties(),
            property => property.Name.Contains("Scope", StringComparison.Ordinal));
    }

    private static EditorModuleBuilder CreateBuilder(
        string moduleId,
        EditorModuleScopeKind scope)
    {
        return new EditorModuleBuilder(
            new EditorModuleDefinitionContext(CreateDefinition(moduleId, scope)));
    }

    private static EditorModuleDefinitionId CreateDefinition(
        string moduleId,
        EditorModuleScopeKind scope)
    {
        return EditorModuleDefinitionId.Create(
            EditorAssemblyId.Create(
                PackageName.Create("com.asharia.tests"),
                EditorAssemblyName.Create("Asharia.Tests.Editor")),
            ModuleLocalId.Create(moduleId),
            scope);
    }

    private static EditorPanelDescriptor CreatePanel(
        string contributionId,
        string factoryId)
    {
        return new EditorPanelDescriptor(
            EditorContributionId.Create(contributionId),
            "Test Panel",
            EditorPanelKind.Tool,
            EditorDockPreference.Right,
            EditorPanelCachePolicy.KeepAlive,
            UiBackendId.CodeFirst,
            EditorFactoryLocalId.Create(factoryId));
    }
}
