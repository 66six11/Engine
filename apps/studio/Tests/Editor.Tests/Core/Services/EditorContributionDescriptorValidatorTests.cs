using System;
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

    [Fact]
    public void Null_descriptor_collections_are_reported_as_structured_errors()
    {
        var result = Validate(new EditorContributionDescriptorSet(
            new EditorContributionSourceId("project.editor"),
            EditorContributionSourceKind.ProjectEditor,
            Panels: null!,
            Actions: null!,
            DiagnosticSources: null!));

        Assert.Equal(
            ["Panels", "Actions", "DiagnosticSources"],
            result.Errors.Select(error => error.Field).ToArray());
        Assert.Equal(
            [
                "Panel descriptor collection must not be null.",
                "Action descriptor collection must not be null.",
                "Diagnostic source descriptor collection must not be null.",
            ],
            result.Errors.Select(error => error.Message).ToArray());
    }

    [Fact]
    public void Null_descriptor_items_are_reported_as_structured_errors()
    {
        var result = Validate(CreateDescriptorSet(
            panels: [null!],
            actions: [null!],
            diagnosticSources: [null!]));

        Assert.Equal(
            ["Panels", "Actions", "DiagnosticSources"],
            result.Errors.Select(error => error.Field).ToArray());
        Assert.Equal(
            [
                "Panel descriptor must not be null.",
                "Action descriptor must not be null.",
                "Diagnostic source descriptor must not be null.",
            ],
            result.Errors.Select(error => error.Message).ToArray());
    }

    [Fact]
    public void Existing_action_and_diagnostic_source_collisions_are_reported_from_validation_context()
    {
        var context = new EditorContributionValidationContext(
            RegisteredPanelIds: [],
            RegisteredActionIds: ["project.open-inspector"],
            RegisteredDiagnosticSourceIds: ["project.debug"]);

        var result = new EditorContributionDescriptorValidator().Validate(
            CreateDescriptorSet(),
            context);

        Assert.Equal(
            ["project.open-inspector", "project.debug"],
            result.Errors.Select(error => error.ContributionId).ToArray());
        Assert.Equal(
            [
                "Action id 'project.open-inspector' is already registered.",
                "Diagnostic source id 'project.debug' is already registered.",
            ],
            result.Errors.Select(error => error.Message).ToArray());
    }

    [Fact]
    public void Validation_context_rejects_null_registry_snapshots_clearly()
    {
        var context = new EditorContributionValidationContext(
            RegisteredPanelIds: null!,
            RegisteredActionIds: [],
            RegisteredDiagnosticSourceIds: []);

        var exception = Assert.Throws<ArgumentNullException>(() =>
            new EditorContributionDescriptorValidator().Validate(CreateDescriptorSet(), context));

        Assert.Equal("RegisteredPanelIds", exception.ParamName);
    }

    [Fact]
    public void Panel_validation_reports_invalid_enums()
    {
        var result = Validate(CreateDescriptorSet(
            panels:
            [
                CreatePanel(
                    "project.invalid-panel",
                    kind: (PanelKind)42,
                    defaultDockArea: (DockArea)43,
                    cachePolicy: (DockContentCachePolicy)44,
                    lifecycleMode: (EditorPanelLifecycleMode)45,
                    frameUpdateMode: (EditorPanelFrameUpdateMode)46),
            ],
            actions: [],
            diagnosticSources: []));

        Assert.Equal(
            ["Kind", "DefaultDockArea", "CachePolicy", "Lifecycle.Mode", "FrameUpdate.Mode"],
            result.Errors.Select(error => error.Field).ToArray());
    }

    [Fact]
    public void Panel_validation_reports_invalid_content_model()
    {
        var result = Validate(CreateDescriptorSet(
            panels:
            [
                CreatePanel(
                    "project.invalid-content",
                    contentModelKind: (EditorPanelContentModelKind)42,
                    contentModelId: " "),
            ],
            actions: [],
            diagnosticSources: []));

        Assert.Equal(
            ["ContentModel.Kind", "ContentModel.ModelId"],
            result.Errors.Select(error => error.Field).ToArray());
        Assert.All(result.Errors, error => Assert.Equal("project.invalid-content", error.ContributionId));
    }

    [Fact]
    public void Panel_validation_reports_menu_path_and_target_fps_errors()
    {
        var result = Validate(CreateDescriptorSet(
            panels:
            [
                CreatePanel(
                    "project.invalid-menu",
                    menuPath: "/Window//Panels/",
                    targetFramesPerSecond: 0),
            ],
            actions: [],
            diagnosticSources: []));

        Assert.Equal(
            ["MenuPath", "FrameUpdate.TargetFramesPerSecond"],
            result.Errors.Select(error => error.Field).ToArray());
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

    private static EditorPanelContributionDescriptor CreatePanel(
        string id,
        PanelKind kind = PanelKind.Tool,
        DockArea defaultDockArea = DockArea.Right,
        DockContentCachePolicy cachePolicy = DockContentCachePolicy.KeepAlive,
        string menuPath = "Window/Panels/Inspector",
        EditorPanelContentModelKind contentModelKind = EditorPanelContentModelKind.ViewModelTypeReference,
        string contentModelId = "Editor.Tests.InspectorPanelViewModel",
        EditorPanelLifecycleMode lifecycleMode = EditorPanelLifecycleMode.ContentObject,
        EditorPanelFrameUpdateMode frameUpdateMode = EditorPanelFrameUpdateMode.Active,
        double? targetFramesPerSecond = 30)
    {
        return new EditorPanelContributionDescriptor(
            id,
            "Inspector",
            kind,
            defaultDockArea,
            menuPath,
            cachePolicy,
            new EditorPanelContentModelReference(contentModelKind, contentModelId),
            new EditorPanelLifecycleDescriptor(lifecycleMode),
            new EditorPanelFrameUpdateDescriptor(frameUpdateMode, targetFramesPerSecond));
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
