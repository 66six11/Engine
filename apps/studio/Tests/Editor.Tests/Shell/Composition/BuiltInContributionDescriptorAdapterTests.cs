using System;
using Asharia.Editor.Panels;
using Editor.Core.Models.Contributions;
using Editor.Core.Models.Extensions;
using Editor.Core.Models.Panels;
using Editor.Core.Models.Workbench;
using Editor.Shell.Composition;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class BuiltInContributionDescriptorAdapterTests
{
    [Fact]
    public void Adapt_preserves_runtime_panel_and_action_metadata_without_creating_panel_content()
    {
        var createContentCallCount = 0;
        var panel = new PanelDescriptor(
            "project.inspector",
            "Inspector",
            PanelKind.Tool,
            EditorDockArea.Right,
            "Window/Panels/Inspector",
            DockContentCachePolicy.KeepAlive,
            () =>
            {
                createContentCallCount++;
                return new object();
            },
            IconKey: "project.inspector.icon",
            Tag: "TOOL",
            TitleDetail: "selection target",
            StatusText: "ready");
        var action = new WorkbenchActionDescriptor(
            "project.open-inspector",
            "Open Inspector",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Inspector",
            TargetId: panel.Id,
            IconKey: "project.inspector.icon",
            Category: "Window",
            DefaultShortcut: "Ctrl+Shift+I",
            Scope: WorkbenchActionScope.Global,
            SearchText: "selection properties");
        var contributions = new EditorDeclaredContributions(
            new EditorExtensionId("project.editor"),
            [panel],
            [action],
            []);

        var descriptorSet = new BuiltInContributionDescriptorAdapter().Adapt(contributions);

        Assert.Equal("project.editor", descriptorSet.SourceId.Value);
        Assert.Equal(EditorContributionSourceKind.BuiltIn, descriptorSet.SourceKind);
        Assert.Equal(0, createContentCallCount);

        var adaptedPanel = Assert.Single(descriptorSet.Panels);
        Assert.Equal(panel.Id, adaptedPanel.Id);
        Assert.Equal(panel.Title, adaptedPanel.Title);
        Assert.Equal(panel.Kind, adaptedPanel.Kind);
        Assert.Equal(panel.DefaultArea, adaptedPanel.DefaultDockArea);
        Assert.Equal(panel.MenuPath, adaptedPanel.MenuPath);
        Assert.Equal(panel.CachePolicy, adaptedPanel.CachePolicy);
        Assert.Equal(EditorPanelContentModelKind.ViewModelTypeReference, adaptedPanel.ContentModel.Kind);
        Assert.Equal("project.editor/project.inspector", adaptedPanel.ContentModel.ModelId);
        Assert.Equal(EditorPanelLifecycleMode.ContentObject, adaptedPanel.Lifecycle.Mode);
        Assert.Equal(EditorPanelFrameUpdateMode.Manual, adaptedPanel.FrameUpdate.Mode);

        var adaptedAction = Assert.Single(descriptorSet.Actions);
        Assert.Equal(action.Id, adaptedAction.Id);
        Assert.Equal(action.Title, adaptedAction.Title);
        Assert.Equal(action.Category, adaptedAction.Category);
        Assert.Equal(action.Scope, adaptedAction.Scope);
        Assert.Equal(action.DefaultShortcut, adaptedAction.DefaultShortcut);
        Assert.Equal(action.MenuPath, adaptedAction.MenuPath);
        Assert.Equal(action.Id, adaptedAction.CommandId);
        Assert.Empty(descriptorSet.DiagnosticSources);
    }

    [Fact]
    public void Adapt_rejects_null_declared_contributions()
    {
        Assert.Throws<ArgumentNullException>(() =>
            new BuiltInContributionDescriptorAdapter().Adapt(null!));
    }
}
