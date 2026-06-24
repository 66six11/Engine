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
    public void Existing_registered_id_collisions_are_reported_across_contribution_kinds()
    {
        var context = new EditorContributionValidationContext(
            RegisteredPanelIds: ["project.open-inspector"],
            RegisteredActionIds: ["project.inspector"],
            RegisteredDiagnosticSourceIds: []);

        var result = new EditorContributionDescriptorValidator().Validate(
            CreateDescriptorSet(),
            context);

        Assert.Equal(
            ["project.inspector", "project.open-inspector"],
            result.Errors.Select(error => error.ContributionId).ToArray());
        Assert.Equal(
            [
                "Panel id 'project.inspector' is already registered.",
                "Action id 'project.open-inspector' is already registered.",
            ],
            result.Errors.Select(error => error.Message).ToArray());
    }

    [Fact]
    public void Diagnostic_source_route_prefix_collisions_are_reported_against_panel_and_action_ids()
    {
        var result = Validate(CreateDescriptorSet(
            panels: [CreatePanel("project.debug.panel")],
            actions: [CreateAction("project.debug.open", "project.debug.open")],
            diagnosticSources: [CreateDiagnosticSource("project.debug")]));

        Assert.Equal(
            ["project.debug", "project.debug"],
            result.Errors.Select(error => error.ContributionId).ToArray());
        Assert.Equal(
            [
                "Diagnostic source id 'project.debug' conflicts with panel id 'project.debug.panel' route prefix.",
                "Diagnostic source id 'project.debug' conflicts with action id 'project.debug.open' route prefix.",
            ],
            result.Errors.Select(error => error.Message).ToArray());
    }

    [Fact]
    public void Incoming_diagnostic_source_route_prefix_collisions_are_reported_against_registered_panel_and_action_ids()
    {
        var context = new EditorContributionValidationContext(
            RegisteredPanelIds: ["project.debug.panel"],
            RegisteredActionIds: ["project.debug.open"],
            RegisteredDiagnosticSourceIds: []);

        var result = new EditorContributionDescriptorValidator().Validate(
            CreateDescriptorSet(
                panels: [],
                actions: [],
                diagnosticSources: [CreateDiagnosticSource("project.debug")]),
            context);

        Assert.Equal(
            ["project.debug", "project.debug"],
            result.Errors.Select(error => error.ContributionId).ToArray());
        Assert.Equal(
            [
                "Diagnostic source id 'project.debug' conflicts with panel id 'project.debug.panel' route prefix.",
                "Diagnostic source id 'project.debug' conflicts with action id 'project.debug.open' route prefix.",
            ],
            result.Errors.Select(error => error.Message).ToArray());
    }

    [Fact]
    public void Incoming_panel_and_action_route_prefix_collisions_are_reported_against_registered_diagnostic_sources()
    {
        var context = new EditorContributionValidationContext(
            RegisteredPanelIds: [],
            RegisteredActionIds: [],
            RegisteredDiagnosticSourceIds: ["project.debug"]);

        var result = new EditorContributionDescriptorValidator().Validate(
            CreateDescriptorSet(
                panels: [CreatePanel("project.debug.panel")],
                actions: [CreateAction("project.debug.open", "project.debug.open")],
                diagnosticSources: []),
            context);

        Assert.Equal(
            ["project.debug", "project.debug"],
            result.Errors.Select(error => error.ContributionId).ToArray());
        Assert.Equal(
            [
                "Diagnostic source id 'project.debug' conflicts with panel id 'project.debug.panel' route prefix.",
                "Diagnostic source id 'project.debug' conflicts with action id 'project.debug.open' route prefix.",
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

    [Theory]
    [InlineData(double.NaN)]
    [InlineData(double.PositiveInfinity)]
    [InlineData(-1)]
    public void Panel_validation_reports_clear_target_fps_message(double targetFramesPerSecond)
    {
        var result = Validate(CreateDescriptorSet(
            panels:
            [
                CreatePanel(
                    "project.invalid-frame-rate",
                    targetFramesPerSecond: targetFramesPerSecond),
            ],
            actions: [],
            diagnosticSources: []));

        var error = Assert.Single(result.Errors);
        Assert.Equal("FrameUpdate.TargetFramesPerSecond", error.Field);
        Assert.Equal(
            "Panel target frames per second must be finite and greater than zero.",
            error.Message);
    }

    [Fact]
    public void Panel_validation_reports_null_nested_descriptors()
    {
        var result = Validate(CreateDescriptorSet(
            panels:
            [
                new EditorPanelContributionDescriptor(
                    "project.null-nested",
                    "Inspector",
                    PanelKind.Tool,
                    DockArea.Right,
                    "Window/Panels/Inspector",
                    DockContentCachePolicy.KeepAlive,
                    ContentModel: null!,
                    Lifecycle: null!,
                    FrameUpdate: null!),
            ],
            actions: [],
            diagnosticSources: []));

        Assert.Equal(
            ["ContentModel", "Lifecycle", "FrameUpdate"],
            result.Errors.Select(error => error.Field).ToArray());
    }

    [Fact]
    public void Action_validation_reports_invalid_scope_menu_path_and_missing_command()
    {
        var result = Validate(CreateDescriptorSet(
            panels: [],
            actions:
            [
                CreateAction(
                    "project.invalid-action",
                    " ",
                    scope: (WorkbenchActionScope)42,
                    menuPath: "Tools//Invalid"),
            ],
            diagnosticSources: []));

        Assert.Equal(
            ["Scope", "MenuPath", "CommandId"],
            result.Errors.Select(error => error.Field).ToArray());
        Assert.All(result.Errors, error => Assert.Equal("project.invalid-action", error.ContributionId));
    }

    [Fact]
    public void Diagnostic_source_validation_reports_invalid_channel_and_source_kind()
    {
        var result = Validate(CreateDescriptorSet(
            panels: [],
            actions: [],
            diagnosticSources:
            [
                new EditorDiagnosticSourceDescriptor(
                    "project.invalid-diagnostics",
                    "Project Debug",
                    (EditorDiagnosticChannel)42,
                    (EditorContributionSourceKind)43),
            ]));

        Assert.Equal(
            ["DefaultChannel", "SourceKind"],
            result.Errors.Select(error => error.Field).ToArray());
        Assert.All(result.Errors, error => Assert.Equal("project.invalid-diagnostics", error.ContributionId));
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
        string commandId,
        WorkbenchActionScope scope = WorkbenchActionScope.Global,
        string menuPath = "Window/Panels/Inspector")
    {
        return new EditorActionContributionDescriptor(
            id,
            "Open Inspector",
            "Window",
            scope,
            "Ctrl+Shift+I",
            menuPath,
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
