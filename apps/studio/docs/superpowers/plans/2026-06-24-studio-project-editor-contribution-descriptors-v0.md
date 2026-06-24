# Studio Project Editor Contribution Descriptors v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the first PR-sized slice of project `Editor/` extension support by introducing data-only contribution descriptors and a Core validator.

**Architecture:** Descriptor models live in `Editor.Core.Models` because they are the framework ABI shared by built-in features, future project `Editor/` adapters, packaged plugins, and native adapters. Validation lives in `Editor.Core.Services` and returns structured errors instead of throwing for ordinary invalid descriptor input; Shell registry commit, source loading, runtime XAML, and external assemblies stay outside this slice.

**Tech Stack:** C# 13 / .NET 10, xUnit, existing `Editor.csproj` SDK globs, existing `Editor.Core.Models` enums for dock areas, panel kinds, cache policy, action scope, diagnostics channel, and panel frame update mode.

---

## Source Spec

Implement only Slice 1 from `docs/superpowers/specs/2026-06-24-studio-project-editor-contributions-v0-design.md`.

This plan intentionally does not change:

- `Shell/Composition/EditorExtensionHost.cs`
- `Shell/Composition/EditorContributionBuilder.cs`
- `Shell/Docking/PanelRegistry.cs`
- `Shell/Commands/WorkbenchActionRegistry.cs`
- project `Editor/` scanning
- C# compilation, `AssemblyLoadContext`, runtime XAML loading, script VM, or native renderer integration

## File Map

- Create `Core/Models/EditorContributionSourceId.cs`
  - Stable source id wrapper. It stays data-only so imported invalid source ids can be validated as data.
- Create `Core/Models/EditorContributionSourceKind.cs`
  - Source policy/diagnostic kind enum.
- Create `Core/Models/EditorPanelContentModelKind.cs`
  - Supported descriptor-level content reference kinds.
- Create `Core/Models/EditorPanelContentModelReference.cs`
  - Data-only content model reference used by panel descriptors.
- Create `Core/Models/EditorPanelLifecycleMode.cs`
  - Descriptor-level declaration for panel lifecycle behavior.
- Create `Core/Models/EditorPanelLifecycleDescriptor.cs`
  - Data-only lifecycle descriptor.
- Create `Core/Models/EditorPanelFrameUpdateDescriptor.cs`
  - Data-only frame update request for descriptors.
- Create `Core/Models/EditorPanelContributionDescriptor.cs`
  - Data-only panel contribution metadata.
- Create `Core/Models/EditorActionContributionDescriptor.cs`
  - Data-only action contribution metadata with `CommandId`.
- Create `Core/Models/EditorDiagnosticSourceDescriptor.cs`
  - Data-only diagnostic source metadata.
- Create `Core/Models/EditorContributionDescriptorSet.cs`
  - One source adapter output set.
- Create `Core/Models/EditorContributionValidationError.cs`
  - Structured validation error with source id, contribution id, field, and message.
- Create `Core/Models/EditorContributionValidationResult.cs`
  - Structured validation result with `IsValid`.
- Create `Core/Models/EditorContributionValidationContext.cs`
  - Existing registry id snapshot for pre-commit collision validation without depending on Shell registries.
- Create `Core/Services/EditorContributionDescriptorValidator.cs`
  - Core validator for descriptor shape and validation rules.
- Create `Tests/Editor.Tests/Core/Models/EditorContributionDescriptorTests.cs`
  - Data model preservation tests.
- Create `Tests/Editor.Tests/Core/Services/EditorContributionDescriptorValidatorTests.cs`
  - Validation rule tests.

## Task 1: Source Descriptor Primitives

**Files:**
- Create: `Core/Models/EditorContributionSourceId.cs`
- Create: `Core/Models/EditorContributionSourceKind.cs`
- Create: `Tests/Editor.Tests/Core/Models/EditorContributionDescriptorTests.cs`

- [ ] **Step 1: Write the failing data-model test**

Create `Tests/Editor.Tests/Core/Models/EditorContributionDescriptorTests.cs` with this content:

```csharp
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
}
```

- [ ] **Step 2: Run the focused test and verify it fails at compile time**

Run from `D:\TechArt\VkEngine-studio-frontend\apps\studio`:

```powershell
dotnet test .\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorContributionDescriptorTests"
```

Expected: FAIL with `CS0246` for `EditorContributionSourceId` and `EditorContributionSourceKind`.

- [ ] **Step 3: Add the source id model**

Create `Core/Models/EditorContributionSourceId.cs`:

```csharp
namespace Editor.Core.Models;

public sealed record EditorContributionSourceId(string Value)
{
    public override string ToString()
    {
        return Value;
    }
}
```

- [ ] **Step 4: Add the source kind enum**

Create `Core/Models/EditorContributionSourceKind.cs`:

```csharp
namespace Editor.Core.Models;

public enum EditorContributionSourceKind
{
    BuiltIn,
    ProjectEditor,
    PackagedPlugin,
    NativeAdapter,
}
```

- [ ] **Step 5: Run the focused test and verify it passes**

Run:

```powershell
dotnet test .\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorContributionDescriptorTests"
```

Expected: PASS, 3 tests from `EditorContributionDescriptorTests`.

- [ ] **Step 6: Commit**

```powershell
git add Core\Models\EditorContributionSourceId.cs Core\Models\EditorContributionSourceKind.cs Tests\Editor.Tests\Core\Models\EditorContributionDescriptorTests.cs
git commit -m "feat: add editor contribution source descriptors"
```

## Task 2: Panel Content, Lifecycle, And Frame Descriptor Primitives

**Files:**
- Modify: `Tests/Editor.Tests/Core/Models/EditorContributionDescriptorTests.cs`
- Create: `Core/Models/EditorPanelContentModelKind.cs`
- Create: `Core/Models/EditorPanelContentModelReference.cs`
- Create: `Core/Models/EditorPanelLifecycleMode.cs`
- Create: `Core/Models/EditorPanelLifecycleDescriptor.cs`
- Create: `Core/Models/EditorPanelFrameUpdateDescriptor.cs`

- [ ] **Step 1: Add failing tests for panel descriptor primitives**

Append these tests inside `EditorContributionDescriptorTests`:

```csharp
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
```

- [ ] **Step 2: Run the focused test and verify it fails at compile time**

Run:

```powershell
dotnet test .\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorContributionDescriptorTests"
```

Expected: FAIL with `CS0246` for the new panel descriptor primitive types.

- [ ] **Step 3: Add content model kind**

Create `Core/Models/EditorPanelContentModelKind.cs`:

```csharp
namespace Editor.Core.Models;

public enum EditorPanelContentModelKind
{
    ViewModelTypeReference,
    DeclarativePanelModelReference,
}
```

- [ ] **Step 4: Add content model reference**

Create `Core/Models/EditorPanelContentModelReference.cs`:

```csharp
namespace Editor.Core.Models;

public sealed record EditorPanelContentModelReference(
    EditorPanelContentModelKind Kind,
    string ModelId);
```

- [ ] **Step 5: Add lifecycle mode**

Create `Core/Models/EditorPanelLifecycleMode.cs`:

```csharp
namespace Editor.Core.Models;

public enum EditorPanelLifecycleMode
{
    None,
    ContentObject,
}
```

- [ ] **Step 6: Add lifecycle descriptor**

Create `Core/Models/EditorPanelLifecycleDescriptor.cs`:

```csharp
namespace Editor.Core.Models;

public sealed record EditorPanelLifecycleDescriptor(EditorPanelLifecycleMode Mode)
{
    public static EditorPanelLifecycleDescriptor None { get; } =
        new(EditorPanelLifecycleMode.None);

    public static EditorPanelLifecycleDescriptor ContentObject { get; } =
        new(EditorPanelLifecycleMode.ContentObject);
}
```

- [ ] **Step 7: Add frame update descriptor**

Create `Core/Models/EditorPanelFrameUpdateDescriptor.cs`:

```csharp
namespace Editor.Core.Models;

public sealed record EditorPanelFrameUpdateDescriptor(
    EditorPanelFrameUpdateMode Mode,
    double? TargetFramesPerSecond = null)
{
    public static EditorPanelFrameUpdateDescriptor Manual { get; } =
        new(EditorPanelFrameUpdateMode.Manual);
}
```

- [ ] **Step 8: Run the focused test and verify it passes**

Run:

```powershell
dotnet test .\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorContributionDescriptorTests"
```

Expected: PASS, 6 tests from `EditorContributionDescriptorTests`.

- [ ] **Step 9: Commit**

```powershell
git add Core\Models\EditorPanelContentModelKind.cs Core\Models\EditorPanelContentModelReference.cs Core\Models\EditorPanelLifecycleMode.cs Core\Models\EditorPanelLifecycleDescriptor.cs Core\Models\EditorPanelFrameUpdateDescriptor.cs Tests\Editor.Tests\Core\Models\EditorContributionDescriptorTests.cs
git commit -m "feat: add editor panel descriptor primitives"
```

## Task 3: Contribution Descriptor Records

**Files:**
- Modify: `Tests/Editor.Tests/Core/Models/EditorContributionDescriptorTests.cs`
- Create: `Core/Models/EditorPanelContributionDescriptor.cs`
- Create: `Core/Models/EditorActionContributionDescriptor.cs`
- Create: `Core/Models/EditorDiagnosticSourceDescriptor.cs`
- Create: `Core/Models/EditorContributionDescriptorSet.cs`

- [ ] **Step 1: Add failing tests for full descriptor records**

Append these tests and helpers inside `EditorContributionDescriptorTests`:

```csharp
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
```

- [ ] **Step 2: Run the focused test and verify it fails at compile time**

Run:

```powershell
dotnet test .\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorContributionDescriptorTests"
```

Expected: FAIL with `CS0246` for `EditorPanelContributionDescriptor`, `EditorActionContributionDescriptor`, `EditorDiagnosticSourceDescriptor`, and `EditorContributionDescriptorSet`.

- [ ] **Step 3: Add the panel contribution descriptor**

Create `Core/Models/EditorPanelContributionDescriptor.cs`:

```csharp
namespace Editor.Core.Models;

public sealed record EditorPanelContributionDescriptor(
    string Id,
    string Title,
    PanelKind Kind,
    DockArea DefaultDockArea,
    string MenuPath,
    DockContentCachePolicy CachePolicy,
    EditorPanelContentModelReference ContentModel,
    EditorPanelLifecycleDescriptor Lifecycle,
    EditorPanelFrameUpdateDescriptor FrameUpdate);
```

- [ ] **Step 4: Add the action contribution descriptor**

Create `Core/Models/EditorActionContributionDescriptor.cs`:

```csharp
namespace Editor.Core.Models;

public sealed record EditorActionContributionDescriptor(
    string Id,
    string Title,
    string Category,
    WorkbenchActionScope Scope,
    string? DefaultShortcut,
    string MenuPath,
    string CommandId);
```

- [ ] **Step 5: Add the diagnostic source descriptor**

Create `Core/Models/EditorDiagnosticSourceDescriptor.cs`:

```csharp
namespace Editor.Core.Models;

public sealed record EditorDiagnosticSourceDescriptor(
    string Id,
    string Title,
    EditorDiagnosticChannel DefaultChannel,
    EditorContributionSourceKind SourceKind);
```

- [ ] **Step 6: Add the descriptor set**

Create `Core/Models/EditorContributionDescriptorSet.cs`:

```csharp
using System.Collections.Generic;

namespace Editor.Core.Models;

public sealed record EditorContributionDescriptorSet(
    EditorContributionSourceId SourceId,
    EditorContributionSourceKind SourceKind,
    IReadOnlyList<EditorPanelContributionDescriptor> Panels,
    IReadOnlyList<EditorActionContributionDescriptor> Actions,
    IReadOnlyList<EditorDiagnosticSourceDescriptor> DiagnosticSources);
```

- [ ] **Step 7: Run the focused test and verify it passes**

Run:

```powershell
dotnet test .\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorContributionDescriptorTests"
```

Expected: PASS, 7 tests from `EditorContributionDescriptorTests`.

- [ ] **Step 8: Commit**

```powershell
git add Core\Models\EditorPanelContributionDescriptor.cs Core\Models\EditorActionContributionDescriptor.cs Core\Models\EditorDiagnosticSourceDescriptor.cs Core\Models\EditorContributionDescriptorSet.cs Tests\Editor.Tests\Core\Models\EditorContributionDescriptorTests.cs
git commit -m "feat: add editor contribution descriptors"
```

## Task 4: Validation Result, Context, And Source/Id Rules

**Files:**
- Create: `Core/Models/EditorContributionValidationError.cs`
- Create: `Core/Models/EditorContributionValidationResult.cs`
- Create: `Core/Models/EditorContributionValidationContext.cs`
- Create: `Core/Services/EditorContributionDescriptorValidator.cs`
- Create: `Tests/Editor.Tests/Core/Services/EditorContributionDescriptorValidatorTests.cs`

- [ ] **Step 1: Write failing validator tests for source and id rules**

Create `Tests/Editor.Tests/Core/Services/EditorContributionDescriptorValidatorTests.cs`:

```csharp
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
```

- [ ] **Step 2: Run the validator tests and verify they fail at compile time**

Run:

```powershell
dotnet test .\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorContributionDescriptorValidatorTests"
```

Expected: FAIL with `CS0246` for validation result, context, and validator types.

- [ ] **Step 3: Add validation error model**

Create `Core/Models/EditorContributionValidationError.cs`:

```csharp
namespace Editor.Core.Models;

public sealed record EditorContributionValidationError(
    string SourceId,
    string ContributionId,
    string Field,
    string Message);
```

- [ ] **Step 4: Add validation result model**

Create `Core/Models/EditorContributionValidationResult.cs`:

```csharp
using System.Collections.Generic;

namespace Editor.Core.Models;

public sealed record EditorContributionValidationResult(
    IReadOnlyList<EditorContributionValidationError> Errors)
{
    public bool IsValid => Errors.Count == 0;

    public static EditorContributionValidationResult Success { get; } = new([]);
}
```

- [ ] **Step 5: Add validation context model**

Create `Core/Models/EditorContributionValidationContext.cs`:

```csharp
using System.Collections.Generic;

namespace Editor.Core.Models;

public sealed record EditorContributionValidationContext(
    IReadOnlyCollection<string> RegisteredPanelIds,
    IReadOnlyCollection<string> RegisteredActionIds,
    IReadOnlyCollection<string> RegisteredDiagnosticSourceIds)
{
    public static EditorContributionValidationContext Empty { get; } = new(
        RegisteredPanelIds: [],
        RegisteredActionIds: [],
        RegisteredDiagnosticSourceIds: []);
}
```

- [ ] **Step 6: Add validator with source and id rules**

Create `Core/Services/EditorContributionDescriptorValidator.cs`:

```csharp
using System;
using System.Collections.Generic;
using Editor.Core.Models;

namespace Editor.Core.Services;

public sealed class EditorContributionDescriptorValidator
{
    public EditorContributionValidationResult Validate(
        EditorContributionDescriptorSet descriptorSet,
        EditorContributionValidationContext? context = null)
    {
        ArgumentNullException.ThrowIfNull(descriptorSet);
        context ??= EditorContributionValidationContext.Empty;

        var errors = new List<EditorContributionValidationError>();
        var sourceId = descriptorSet.SourceId?.Value ?? string.Empty;

        if (string.IsNullOrWhiteSpace(sourceId))
        {
            AddError(errors, sourceId, string.Empty, "SourceId", "Source id must not be empty.");
        }

        if (!Enum.IsDefined(descriptorSet.SourceKind))
        {
            AddError(
                errors,
                sourceId,
                string.Empty,
                "SourceKind",
                $"Source kind '{descriptorSet.SourceKind}' is not defined.");
        }

        var contributionOwners = new Dictionary<string, string>(StringComparer.Ordinal);

        foreach (var panel in descriptorSet.Panels ?? [])
        {
            if (panel is null)
            {
                AddError(errors, sourceId, string.Empty, "Panels", "Panel descriptor must not be null.");
                continue;
            }

            ValidateContributionId(
                errors,
                sourceId,
                panel.Id,
                "Panel",
                contributionOwners,
                context.RegisteredPanelIds,
                "Panel");
        }

        foreach (var action in descriptorSet.Actions ?? [])
        {
            if (action is null)
            {
                AddError(errors, sourceId, string.Empty, "Actions", "Action descriptor must not be null.");
                continue;
            }

            ValidateContributionId(
                errors,
                sourceId,
                action.Id,
                "Action",
                contributionOwners,
                context.RegisteredActionIds,
                "Action");
        }

        foreach (var diagnosticSource in descriptorSet.DiagnosticSources ?? [])
        {
            if (diagnosticSource is null)
            {
                AddError(
                    errors,
                    sourceId,
                    string.Empty,
                    "DiagnosticSources",
                    "Diagnostic source descriptor must not be null.");
                continue;
            }

            ValidateContributionId(
                errors,
                sourceId,
                diagnosticSource.Id,
                "DiagnosticSource",
                contributionOwners,
                context.RegisteredDiagnosticSourceIds,
                "Diagnostic source");
        }

        return errors.Count == 0
            ? EditorContributionValidationResult.Success
            : new EditorContributionValidationResult(errors.ToArray());
    }

    private static void ValidateContributionId(
        List<EditorContributionValidationError> errors,
        string sourceId,
        string id,
        string contributionType,
        Dictionary<string, string> contributionOwners,
        IReadOnlyCollection<string> registeredIds,
        string registeredName)
    {
        if (string.IsNullOrWhiteSpace(id))
        {
            AddError(
                errors,
                sourceId,
                string.Empty,
                "Id",
                $"{contributionType} id must not be empty.");
            return;
        }

        if (!contributionOwners.TryAdd(id, contributionType))
        {
            AddError(
                errors,
                sourceId,
                id,
                "Id",
                $"Contribution id '{id}' is already used by {contributionOwners[id]}.");
        }

        if (registeredIds.Contains(id))
        {
            AddError(
                errors,
                sourceId,
                id,
                "Id",
                $"{registeredName} id '{id}' is already registered.");
        }
    }

    private static void AddError(
        List<EditorContributionValidationError> errors,
        string sourceId,
        string contributionId,
        string field,
        string message)
    {
        errors.Add(new EditorContributionValidationError(
            string.IsNullOrWhiteSpace(sourceId) ? string.Empty : sourceId,
            contributionId,
            field,
            message));
    }
}
```

- [ ] **Step 7: Run the validator tests and verify they pass**

Run:

```powershell
dotnet test .\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorContributionDescriptorValidatorTests"
```

Expected: PASS, 5 tests from `EditorContributionDescriptorValidatorTests`.

- [ ] **Step 8: Commit**

```powershell
git add Core\Models\EditorContributionValidationError.cs Core\Models\EditorContributionValidationResult.cs Core\Models\EditorContributionValidationContext.cs Core\Services\EditorContributionDescriptorValidator.cs Tests\Editor.Tests\Core\Services\EditorContributionDescriptorValidatorTests.cs
git commit -m "feat: validate editor contribution source ids"
```

## Task 5: Panel Validation Rules

**Files:**
- Modify: `Core/Services/EditorContributionDescriptorValidator.cs`
- Modify: `Tests/Editor.Tests/Core/Services/EditorContributionDescriptorValidatorTests.cs`

- [ ] **Step 1: Add failing tests for panel rules**

Append these tests inside `EditorContributionDescriptorValidatorTests`:

```csharp
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
```

Replace the existing `CreatePanel` helper with this overload:

```csharp
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
```

- [ ] **Step 2: Run the validator tests and verify the new tests fail**

Run:

```powershell
dotnet test .\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorContributionDescriptorValidatorTests"
```

Expected: FAIL because the validator does not yet report panel enum, content model, menu path, or target FPS errors.

- [ ] **Step 3: Add panel validation calls**

Inside each non-null panel branch in `EditorContributionDescriptorValidator.Validate`, after `ValidateContributionId(...)`, add:

```csharp
            ValidatePanel(errors, sourceId, panel);
```

- [ ] **Step 4: Add panel validation methods**

Add these methods to `EditorContributionDescriptorValidator`:

```csharp
    private static void ValidatePanel(
        List<EditorContributionValidationError> errors,
        string sourceId,
        EditorPanelContributionDescriptor panel)
    {
        ValidateDefinedEnum(errors, sourceId, panel.Id, "Kind", panel.Kind);
        ValidateDefinedEnum(errors, sourceId, panel.Id, "DefaultDockArea", panel.DefaultDockArea);
        ValidateMenuPath(errors, sourceId, panel.Id, panel.MenuPath);
        ValidateDefinedEnum(errors, sourceId, panel.Id, "CachePolicy", panel.CachePolicy);

        if (panel.ContentModel is null)
        {
            AddError(
                errors,
                sourceId,
                panel.Id,
                "ContentModel",
                "Panel content model must not be null.");
        }
        else
        {
            ValidateDefinedEnum(
                errors,
                sourceId,
                panel.Id,
                "ContentModel.Kind",
                panel.ContentModel.Kind);

            if (string.IsNullOrWhiteSpace(panel.ContentModel.ModelId))
            {
                AddError(
                    errors,
                    sourceId,
                    panel.Id,
                    "ContentModel.ModelId",
                    "Panel content model id must not be empty.");
            }
        }

        if (panel.Lifecycle is null)
        {
            AddError(
                errors,
                sourceId,
                panel.Id,
                "Lifecycle",
                "Panel lifecycle descriptor must not be null.");
        }
        else
        {
            ValidateDefinedEnum(
                errors,
                sourceId,
                panel.Id,
                "Lifecycle.Mode",
                panel.Lifecycle.Mode);
        }

        if (panel.FrameUpdate is null)
        {
            AddError(
                errors,
                sourceId,
                panel.Id,
                "FrameUpdate",
                "Panel frame update descriptor must not be null.");
        }
        else
        {
            ValidateDefinedEnum(
                errors,
                sourceId,
                panel.Id,
                "FrameUpdate.Mode",
                panel.FrameUpdate.Mode);

            if (panel.FrameUpdate.TargetFramesPerSecond is { } targetFramesPerSecond
                && (targetFramesPerSecond <= 0 || !double.IsFinite(targetFramesPerSecond)))
            {
                AddError(
                    errors,
                    sourceId,
                    panel.Id,
                    "FrameUpdate.TargetFramesPerSecond",
                    "Panel target frames per second must be greater than zero.");
            }
        }
    }

    private static void ValidateMenuPath(
        List<EditorContributionValidationError> errors,
        string sourceId,
        string contributionId,
        string menuPath)
    {
        if (string.IsNullOrWhiteSpace(menuPath))
        {
            AddError(
                errors,
                sourceId,
                contributionId,
                "MenuPath",
                "Menu path must not be empty.");
            return;
        }

        var segments = menuPath.Split('/');
        if (menuPath.Contains('\\')
            || menuPath.StartsWith('/', StringComparison.Ordinal)
            || menuPath.EndsWith('/', StringComparison.Ordinal)
            || segments.Length < 2
            || Array.Exists(segments, string.IsNullOrWhiteSpace))
        {
            AddError(
                errors,
                sourceId,
                contributionId,
                "MenuPath",
                $"Menu path '{menuPath}' must be a slash-separated route with at least two non-empty segments.");
        }
    }

    private static void ValidateDefinedEnum<TEnum>(
        List<EditorContributionValidationError> errors,
        string sourceId,
        string contributionId,
        string field,
        TEnum value)
        where TEnum : struct, Enum
    {
        if (!Enum.IsDefined(value))
        {
            AddError(
                errors,
                sourceId,
                contributionId,
                field,
                $"{field} value '{value}' is not defined.");
        }
    }
```

- [ ] **Step 5: Run the validator tests and verify they pass**

Run:

```powershell
dotnet test .\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorContributionDescriptorValidatorTests"
```

Expected: PASS, 8 tests from `EditorContributionDescriptorValidatorTests`.

- [ ] **Step 6: Commit**

```powershell
git add Core\Services\EditorContributionDescriptorValidator.cs Tests\Editor.Tests\Core\Services\EditorContributionDescriptorValidatorTests.cs
git commit -m "feat: validate editor panel descriptors"
```

## Task 6: Action And Diagnostic Source Validation Rules

**Files:**
- Modify: `Core/Services/EditorContributionDescriptorValidator.cs`
- Modify: `Tests/Editor.Tests/Core/Services/EditorContributionDescriptorValidatorTests.cs`

- [ ] **Step 1: Add failing tests for action and diagnostic source rules**

Append these tests inside `EditorContributionDescriptorValidatorTests`:

```csharp
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
```

Replace the existing `CreateAction` helper with this overload:

```csharp
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
```

- [ ] **Step 2: Run the validator tests and verify the new tests fail**

Run:

```powershell
dotnet test .\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorContributionDescriptorValidatorTests"
```

Expected: FAIL because the validator does not yet report action scope, action command id, action menu path, diagnostic channel, or diagnostic source kind errors.

- [ ] **Step 3: Add action validation call**

Inside each non-null action branch in `EditorContributionDescriptorValidator.Validate`, after `ValidateContributionId(...)`, add:

```csharp
            ValidateAction(errors, sourceId, action);
```

- [ ] **Step 4: Add diagnostic source validation call**

Inside each non-null diagnostic source branch in `EditorContributionDescriptorValidator.Validate`, after `ValidateContributionId(...)`, add:

```csharp
            ValidateDiagnosticSource(errors, sourceId, diagnosticSource);
```

- [ ] **Step 5: Add action and diagnostic validation methods**

Add these methods to `EditorContributionDescriptorValidator`:

```csharp
    private static void ValidateAction(
        List<EditorContributionValidationError> errors,
        string sourceId,
        EditorActionContributionDescriptor action)
    {
        ValidateDefinedEnum(errors, sourceId, action.Id, "Scope", action.Scope);
        ValidateMenuPath(errors, sourceId, action.Id, action.MenuPath);

        if (string.IsNullOrWhiteSpace(action.CommandId))
        {
            AddError(
                errors,
                sourceId,
                action.Id,
                "CommandId",
                "Action command id must not be empty.");
        }
    }

    private static void ValidateDiagnosticSource(
        List<EditorContributionValidationError> errors,
        string sourceId,
        EditorDiagnosticSourceDescriptor diagnosticSource)
    {
        ValidateDefinedEnum(
            errors,
            sourceId,
            diagnosticSource.Id,
            "DefaultChannel",
            diagnosticSource.DefaultChannel);
        ValidateDefinedEnum(
            errors,
            sourceId,
            diagnosticSource.Id,
            "SourceKind",
            diagnosticSource.SourceKind);
    }
```

- [ ] **Step 6: Run the validator tests and verify they pass**

Run:

```powershell
dotnet test .\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorContributionDescriptorValidatorTests"
```

Expected: PASS, 10 tests from `EditorContributionDescriptorValidatorTests`.

- [ ] **Step 7: Commit**

```powershell
git add Core\Services\EditorContributionDescriptorValidator.cs Tests\Editor.Tests\Core\Services\EditorContributionDescriptorValidatorTests.cs
git commit -m "feat: validate editor action descriptors"
```

## Task 7: Full Verification And Review

**Files:**
- Verify all files created or modified by Tasks 1 through 6.

- [ ] **Step 1: Run the full Studio test suite**

Run from `D:\TechArt\VkEngine-studio-frontend\apps\studio`:

```powershell
dotnet test .\Editor.sln -c Release
```

Expected: PASS for all `Editor.Tests` tests.

- [ ] **Step 2: Run encoding check from repository root**

Run from `D:\TechArt\VkEngine-studio-frontend`:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
```

Expected: success output with no encoding violations.

- [ ] **Step 3: Run whitespace check**

Run from `D:\TechArt\VkEngine-studio-frontend\apps\studio`:

```powershell
git diff --check
```

Expected: no output and exit code `0`.

- [ ] **Step 4: Verify the slice stayed editor-framework-only**

Run from `D:\TechArt\VkEngine-studio-frontend\apps\studio`:

```powershell
rg -n "AssemblyLoadContext|NativeEditorBridge|C\\+\\+ ABI|script VM|ScriptExecutionHost|PluginLoader|LoadFromAssembly|AvaloniaRuntimeXamlLoader|RuntimeXamlLoader|VkDevice|Vulkan|Swapchain" Core Shell Features Tests
```

Expected: no matches in files changed by this plan. Existing unrelated matches outside the changed file set do not block this slice.

- [ ] **Step 5: Review staged diff**

Run:

```powershell
git diff --stat
git diff -- Core Tests
```

Expected:

- New descriptor models under `Core/Models`.
- One validator under `Core/Services`.
- Tests under `Tests/Editor.Tests/Core`.
- No changes to Shell registries, `EditorExtensionHost`, project scanning, runtime XAML loading, external assembly loading, native bridge, renderer, scene mutation, or shell command line behavior.

- [ ] **Step 6: Commit any final fixes**

If Task 7 changed files after the Task 6 commit, commit the final fixes:

```powershell
git add Core Tests
git commit -m "test: verify editor contribution descriptors"
```

If there are no changes after Task 6, do not create an empty commit.

## Self-Review

- Spec coverage:
  - Source id/kind data model: Task 1.
  - Panel content/lifecycle/frame update data model: Task 2.
  - Panel/action/diagnostic descriptor set model: Task 3.
  - Structured validation result/error: Task 4.
  - Blank source id, duplicate ids, existing id collision: Task 4.
  - Panel enum, menu path, content model, lifecycle, frame update validation: Task 5.
  - Action command id and scope validation: Task 6.
  - Diagnostic source channel/source kind validation: Task 6.
  - No registry commit, project `Editor/` scanning, runtime XAML, external assembly loading, script VM, native bridge, or renderer integration: Task 7.
- Type consistency:
  - Descriptor set uses `EditorContributionSourceId`, `EditorContributionSourceKind`, panel/action/diagnostic descriptor lists.
  - Panel descriptor uses existing `PanelKind`, `DockArea`, `DockContentCachePolicy`, `EditorPanelFrameUpdateMode`, plus new descriptor-only content and lifecycle types.
  - Action descriptor uses existing `WorkbenchActionScope` and a plain `CommandId` string.
  - Diagnostic source descriptor uses existing `EditorDiagnosticChannel` and source kind.
- Test strategy:
  - Each task starts with compile-failing or behavior-failing tests.
  - Each task has a focused test command and a commit boundary.
  - Full verification runs the solution tests, encoding check, whitespace check, and forbidden-scope search.
