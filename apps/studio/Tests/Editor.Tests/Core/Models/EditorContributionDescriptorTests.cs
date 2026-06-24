using Editor.Core.Models;
using Xunit;

namespace Editor.Tests.Core.Models;

public sealed class EditorContributionDescriptorTests
{
    [Fact]
    public void Source_id_is_data_only_and_preserves_imported_value()
    {
        var sourceId = new EditorContributionSourceId("project.editor");

        Assert.Equal("project.editor", sourceId.Value);
        Assert.Equal("project.editor", sourceId.ToString());
    }

    [Fact]
    public void Source_id_allows_invalid_imported_value_for_validator_reporting()
    {
        var sourceId = new EditorContributionSourceId(" ");

        Assert.Equal(" ", sourceId.Value);
    }

    [Fact]
    public void Source_kind_defines_supported_descriptor_origins()
    {
        Assert.Equal(0, (int)EditorContributionSourceKind.BuiltIn);
        Assert.Equal(1, (int)EditorContributionSourceKind.ProjectEditor);
        Assert.Equal(2, (int)EditorContributionSourceKind.PackagedPlugin);
        Assert.Equal(3, (int)EditorContributionSourceKind.NativeAdapter);
    }

    [Fact]
    public void Panel_content_model_reference_preserves_kind_and_model_id()
    {
        var reference = new EditorPanelContentModelReference(
            EditorPanelContentModelKind.ViewModelTypeReference,
            "Editor.Tests.MockPanelViewModel");

        Assert.Equal(EditorPanelContentModelKind.ViewModelTypeReference, reference.Kind);
        Assert.Equal("Editor.Tests.MockPanelViewModel", reference.ModelId);
    }

    [Fact]
    public void Panel_lifecycle_descriptor_declares_shell_owned_lifecycle_mode()
    {
        Assert.Equal(EditorPanelLifecycleMode.None, EditorPanelLifecycleDescriptor.None.Mode);
        Assert.Equal(EditorPanelLifecycleMode.ContentObject, EditorPanelLifecycleDescriptor.ContentObject.Mode);
    }

    [Fact]
    public void Panel_frame_update_descriptor_preserves_data_without_scheduler_coupling()
    {
        var frameUpdate = new EditorPanelFrameUpdateDescriptor(
            EditorPanelFrameUpdateMode.Visible,
            targetFramesPerSecond: 30);

        Assert.Equal(EditorPanelFrameUpdateMode.Visible, frameUpdate.Mode);
        Assert.Equal(30, frameUpdate.TargetFramesPerSecond);
    }

    [Fact]
    public void Descriptor_set_groups_panels_actions_and_diagnostic_sources_by_source()
    {
        var panel = CreatePanelDescriptor("project.inspector");
        var action = CreateActionDescriptor("project.open-inspector", "project.open-inspector");
        var diagnostics = new EditorDiagnosticSourceDescriptor(
            "project.debug",
            "Project Debug",
            EditorDiagnosticChannel.Debug,
            EditorContributionSourceKind.ProjectEditor);

        var descriptorSet = new EditorContributionDescriptorSet(
            new EditorContributionSourceId("project.editor"),
            EditorContributionSourceKind.ProjectEditor,
            [panel],
            [action],
            [diagnostics]);

        Assert.Equal("project.editor", descriptorSet.SourceId.Value);
        Assert.Equal(EditorContributionSourceKind.ProjectEditor, descriptorSet.SourceKind);
        Assert.Same(panel, Assert.Single(descriptorSet.Panels));
        Assert.Same(action, Assert.Single(descriptorSet.Actions));
        Assert.Same(diagnostics, Assert.Single(descriptorSet.DiagnosticSources));
    }

    private static EditorPanelContributionDescriptor CreatePanelDescriptor(string id)
    {
        return new EditorPanelContributionDescriptor(
            id,
            "Inspector",
            PanelKind.Tool,
            DockArea.Right,
            "Window/Panels/Inspector",
            DockContentCachePolicy.KeepAlive,
            new EditorPanelContentModelReference(
                EditorPanelContentModelKind.ViewModelTypeReference,
                "Editor.Tests.InspectorPanelViewModel"),
            EditorPanelLifecycleDescriptor.ContentObject,
            new EditorPanelFrameUpdateDescriptor(EditorPanelFrameUpdateMode.Active, 30));
    }

    private static EditorActionContributionDescriptor CreateActionDescriptor(
        string id,
        string commandId)
    {
        return new EditorActionContributionDescriptor(
            id,
            "Open Inspector",
            "Window",
            WorkbenchActionScope.Global,
            "Ctrl+Shift+I",
            "Window/Panels/Inspector",
            commandId);
    }
}
