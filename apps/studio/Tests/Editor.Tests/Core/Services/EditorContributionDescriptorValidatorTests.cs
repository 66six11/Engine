using System.Collections.Generic;
using System.Linq;
using Editor.Core.Models;
using Editor.Core.Services;
using Xunit;

namespace Editor.Tests.Core.Services;

public sealed class EditorContributionDescriptorValidatorTests
{
    [Fact]
    public void Valid_descriptor_set_returns_success()
    {
        var result = Validate(CreateDescriptorSet());

        Assert.True(result.IsValid);
        Assert.Empty(result.Errors);
    }

    [Fact]
    public void Blank_source_id_returns_structured_error()
    {
        var result = Validate(CreateDescriptorSet(sourceId: " "));

        var error = Assert.Single(result.Errors);
        Assert.False(result.IsValid);
        Assert.Equal("SourceId", error.Field);
        Assert.Equal("Source id must not be empty.", error.Message);
        Assert.Equal(string.Empty, error.ContributionId);
    }

    [Fact]
    public void Invalid_source_kind_returns_structured_error()
    {
        var result = Validate(CreateDescriptorSet(
            sourceKind: (EditorContributionSourceKind)42));

        var error = Assert.Single(result.Errors);
        Assert.Equal("SourceKind", error.Field);
        Assert.Equal("Source kind '42' is not defined.", error.Message);
    }

    [Fact]
    public void Duplicate_contribution_ids_inside_set_are_reported_before_registry_commit()
    {
        var result = Validate(CreateDescriptorSet(
            panels:
            [
                CreatePanel("project.shared"),
                CreatePanel("project.shared"),
            ]));

        var error = Assert.Single(result.Errors);
        Assert.Equal("project.editor", error.SourceId);
        Assert.Equal("project.shared", error.ContributionId);
        Assert.Equal("Id", error.Field);
        Assert.Equal("Contribution id 'project.shared' is already used by Panel.", error.Message);
    }

    [Fact]
    public void Existing_registry_id_collision_is_reported_from_validation_context()
    {
        var context = new EditorContributionValidationContext(
            RegisteredPanelIds: ["project.inspector"],
            RegisteredActionIds: [],
            RegisteredDiagnosticSourceIds: []);

        var result = new EditorContributionDescriptorValidator().Validate(
            CreateDescriptorSet(),
            context);

        var error = Assert.Single(result.Errors);
        Assert.Equal("project.inspector", error.ContributionId);
        Assert.Equal("Id", error.Field);
        Assert.Equal("Panel id 'project.inspector' is already registered.", error.Message);
    }

    private static EditorContributionValidationResult Validate(
        EditorContributionDescriptorSet descriptorSet)
    {
        return new EditorContributionDescriptorValidator().Validate(descriptorSet);
    }

    private static EditorContributionDescriptorSet CreateDescriptorSet(
        string sourceId = "project.editor",
        EditorContributionSourceKind sourceKind = EditorContributionSourceKind.ProjectEditor,
        IReadOnlyList<EditorPanelContributionDescriptor>? panels = null,
        IReadOnlyList<EditorActionContributionDescriptor>? actions = null,
        IReadOnlyList<EditorDiagnosticSourceDescriptor>? diagnosticSources = null)
    {
        return new EditorContributionDescriptorSet(
            new EditorContributionSourceId(sourceId),
            sourceKind,
            panels ?? [CreatePanel("project.inspector")],
            actions ?? [CreateAction("project.open-inspector", "project.open-inspector")],
            diagnosticSources ?? [CreateDiagnosticSource("project.debug")]);
    }

    private static EditorPanelContributionDescriptor CreatePanel(string id)
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

    private static EditorActionContributionDescriptor CreateAction(
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

    private static EditorDiagnosticSourceDescriptor CreateDiagnosticSource(string id)
    {
        return new EditorDiagnosticSourceDescriptor(
            id,
            "Project Debug",
            EditorDiagnosticChannel.Debug,
            EditorContributionSourceKind.ProjectEditor);
    }
}
