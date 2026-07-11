# Studio Declaration-only Panel Contribution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let an `EditorModule` declare validated, ordered, immutable UI-neutral Panel contributions without implementing factory binding, registry, Dock, or runtime display behavior.

**Architecture:** Add stable contribution/backend/factory-local identities first, then the immutable `EditorPanelDescriptor`, then integrate an isolated `Panels` builder into the existing `EditorModuleBuilder.Build()` freeze boundary. Factory identity remains two-stage: declarations store only `EditorFactoryLocalId`; a future Host binds generation + owner definition + local ID into a runtime handle.

**Tech Stack:** .NET 10, C# 14, xUnit, SDK-style projects, dependency-free `Asharia.Editor`; Avalonia remains only in the legacy Studio implementation.

## Global Constraints

- `Asharia.Editor` remains `net10.0` with no `ProjectReference` and no `PackageReference`.
- Public source must not reference `Editor.Core`, Avalonia, Studio implementation, P/Invoke, native libraries, Vulkan, `Type`, delegate factories, or `object` content factories.
- Do not define `PackageGenerationId` or `GenerationScopedFactoryHandle` in this Slice.
- Do not migrate legacy `PanelDescriptor`, `EditorPanelContributionDescriptor`, validator, adapter, registry, or Dock code.
- Panel scope comes only from `EditorModuleDefinitionContext.DefinitionId.Scope`; descriptor and builder must not add another scope field.
- `EditorContributionId` and `EditorFactoryLocalId` use lowercase dot-separated namespace syntax; `UiBackendId` uses lowercase kebab syntax.
- `UiBackendId.CodeFirst.Value` is exactly `code-first`.
- Same-module contribution IDs and factory local IDs are unique; cross-module/package/scope conflicts remain future Host responsibilities.
- `Build()` preserves declaration order, returns one immutable declaration instance, and freezes dependency, capability, and Panel mutation together.
- Use existing uppercase `apps/studio/Tests/`; do not create a lowercase sibling.
- All changed managed/Markdown files use strict UTF-8 without BOM.
- Preserve user-owned untracked `apps/studio/.vs/` and `qodana.yaml`.
- Every production behavior follows RED → GREEN and every task ends in a buildable commit.

---

### Task 1: Add contribution, factory-local, and UI backend identities

**Files:**

- Create: `apps/studio/src/Asharia.Editor/Contributions/EditorContributionId.cs`
- Create: `apps/studio/src/Asharia.Editor/Contributions/EditorFactoryLocalId.cs`
- Create: `apps/studio/src/Asharia.Editor/Contributions/UiBackendId.cs`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/Contributions/EditorContributionIdentityTests.cs`

**Interfaces:**

- Consumes: internal `Asharia.Editor.Extensions.EditorIdentityValidation.IsLowercaseNamespacedId` for dot-separated identities.
- Produces: `EditorContributionId`, `EditorFactoryLocalId`, and `UiBackendId` value types for Tasks 2–3.

- [x] **Step 1: Write the failing identity tests**

Create `EditorContributionIdentityTests.cs`:

```csharp
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
```

- [x] **Step 2: Run the focused test and verify compile RED**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~EditorContributionIdentityTests
```

Expected: compile FAIL because `Asharia.Editor.Contributions` and all three identities do not exist.

- [x] **Step 3: Implement `EditorContributionId`**

Create:

```csharp
using System;
using Asharia.Editor.Extensions;

namespace Asharia.Editor.Contributions;

public readonly record struct EditorContributionId
{
    private readonly string? value_;

    private EditorContributionId(string value)
    {
        value_ = value;
    }

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public static EditorContributionId Create(string value)
    {
        if (!TryCreate(value, out var result))
        {
            throw new ArgumentException(
                "Editor contribution id must be a lowercase dot-separated namespace.",
                nameof(value));
        }

        return result;
    }

    public static bool TryCreate(string? value, out EditorContributionId result)
    {
        if (EditorIdentityValidation.IsLowercaseNamespacedId(value, allowColon: false))
        {
            result = new EditorContributionId(value!);
            return true;
        }

        result = default;
        return false;
    }

    public override string ToString() => Value;
}
```

- [x] **Step 4: Implement `EditorFactoryLocalId`**

Create:

```csharp
using System;
using Asharia.Editor.Extensions;

namespace Asharia.Editor.Contributions;

public readonly record struct EditorFactoryLocalId
{
    private readonly string? value_;

    private EditorFactoryLocalId(string value)
    {
        value_ = value;
    }

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public static EditorFactoryLocalId Create(string value)
    {
        if (!TryCreate(value, out var result))
        {
            throw new ArgumentException(
                "Editor factory local id must be a lowercase dot-separated namespace.",
                nameof(value));
        }

        return result;
    }

    public static bool TryCreate(string? value, out EditorFactoryLocalId result)
    {
        if (EditorIdentityValidation.IsLowercaseNamespacedId(value, allowColon: false))
        {
            result = new EditorFactoryLocalId(value!);
            return true;
        }

        result = default;
        return false;
    }

    public override string ToString() => Value;
}
```

- [x] **Step 5: Implement `UiBackendId`**

Create:

```csharp
using System;

namespace Asharia.Editor.Contributions;

public readonly record struct UiBackendId
{
    private readonly string? value_;

    private UiBackendId(string value)
    {
        value_ = value;
    }

    public static UiBackendId CodeFirst { get; } = new("code-first");

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public static UiBackendId Create(string value)
    {
        if (!TryCreate(value, out var result))
        {
            throw new ArgumentException(
                "UI backend id must be a lowercase kebab id.",
                nameof(value));
        }

        return result;
    }

    public static bool TryCreate(string? value, out UiBackendId result)
    {
        if (IsLowercaseKebabId(value))
        {
            result = new UiBackendId(value!);
            return true;
        }

        result = default;
        return false;
    }

    public override string ToString() => Value;

    private static bool IsLowercaseKebabId(string? value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return false;
        }

        var atSegmentStart = true;
        for (var index = 0; index < value.Length; index++)
        {
            var character = value[index];
            if (character is >= 'a' and <= 'z' || char.IsAsciiDigit(character))
            {
                atSegmentStart = false;
                continue;
            }

            if (character != '-' || atSegmentStart)
            {
                return false;
            }

            atSegmentStart = true;
        }

        return !atSegmentStart;
    }
}
```

- [x] **Step 6: Verify GREEN and public assembly neutrality**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~EditorContributionIdentityTests
dotnet build apps/studio/src/Asharia.Editor/Asharia.Editor.csproj -c Release --no-restore -warnaserror
```

Expected: all identity cases PASS; public build has 0 warnings/errors.

- [x] **Step 7: Commit**

```powershell
git add apps/studio/src/Asharia.Editor/Contributions apps/studio/Tests/Asharia.Editor.Tests/Contributions
git commit -m "feat(studio): add contribution identities"
```

---

### Task 2: Add the immutable UI-neutral Panel descriptor

**Files:**

- Create: `apps/studio/src/Asharia.Editor/Panels/EditorPanelKind.cs`
- Create: `apps/studio/src/Asharia.Editor/Panels/EditorDockPreference.cs`
- Create: `apps/studio/src/Asharia.Editor/Panels/EditorPanelCachePolicy.cs`
- Create: `apps/studio/src/Asharia.Editor/Panels/EditorPanelDescriptor.cs`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/Panels/EditorPanelDescriptorTests.cs`

**Interfaces:**

- Consumes: Task 1 identities.
- Produces: validated immutable `EditorPanelDescriptor` for Task 3.

- [x] **Step 1: Write failing enum and descriptor tests**

Create `EditorPanelDescriptorTests.cs`:

```csharp
using System;
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
```

Add `using System.Linq;` because enum value assertions use `Select`.

- [x] **Step 2: Run and verify compile RED**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~EditorPanelDescriptorTests
```

Expected: compile FAIL because the public Panel enums/descriptor do not exist.

- [x] **Step 3: Implement the enums**

Create the three files with these exact declarations:

```csharp
namespace Asharia.Editor.Panels;

public enum EditorPanelKind
{
    Document,
    Tool,
}
```

```csharp
namespace Asharia.Editor.Panels;

public enum EditorDockPreference
{
    Center,
    Left,
    Right,
    Bottom,
}
```

```csharp
namespace Asharia.Editor.Panels;

public enum EditorPanelCachePolicy
{
    RecreateOnOpen,
    KeepAlive,
}
```

- [x] **Step 4: Implement the validated descriptor**

Create:

```csharp
using System;
using Asharia.Editor.Contributions;

namespace Asharia.Editor.Panels;

public sealed record EditorPanelDescriptor
{
    public EditorPanelDescriptor(
        EditorContributionId id,
        string title,
        EditorPanelKind kind,
        EditorDockPreference defaultDock,
        EditorPanelCachePolicy cachePolicy,
        UiBackendId backend,
        EditorFactoryLocalId contentFactory)
    {
        if (!id.IsValid)
        {
            throw new ArgumentException("Panel contribution identity is invalid.", nameof(id));
        }

        if (string.IsNullOrWhiteSpace(title))
        {
            throw new ArgumentException("Panel title must not be empty.", nameof(title));
        }

        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, "Panel kind is invalid.");
        }

        if (!Enum.IsDefined(defaultDock))
        {
            throw new ArgumentOutOfRangeException(
                nameof(defaultDock),
                defaultDock,
                "Panel dock preference is invalid.");
        }

        if (!Enum.IsDefined(cachePolicy))
        {
            throw new ArgumentOutOfRangeException(
                nameof(cachePolicy),
                cachePolicy,
                "Panel cache policy is invalid.");
        }

        if (!backend.IsValid)
        {
            throw new ArgumentException("UI backend identity is invalid.", nameof(backend));
        }

        if (!contentFactory.IsValid)
        {
            throw new ArgumentException(
                "Panel content factory identity is invalid.",
                nameof(contentFactory));
        }

        Id = id;
        Title = title;
        Kind = kind;
        DefaultDock = defaultDock;
        CachePolicy = cachePolicy;
        Backend = backend;
        ContentFactory = contentFactory;
    }

    public EditorContributionId Id { get; }
    public string Title { get; }
    public EditorPanelKind Kind { get; }
    public EditorDockPreference DefaultDock { get; }
    public EditorPanelCachePolicy CachePolicy { get; }
    public UiBackendId Backend { get; }
    public EditorFactoryLocalId ContentFactory { get; }
}
```

- [x] **Step 5: Verify GREEN**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~EditorPanelDescriptorTests
dotnet build apps/studio/src/Asharia.Editor/Asharia.Editor.csproj -c Release --no-restore -warnaserror
```

Expected: descriptor tests PASS; public build has 0 warnings/errors.

- [x] **Step 6: Commit**

```powershell
git add apps/studio/src/Asharia.Editor/Panels apps/studio/Tests/Asharia.Editor.Tests/Panels
git commit -m "feat(studio): add panel descriptor contract"
```

---

### Task 3: Integrate Panels into the module declaration freeze

**Files:**

- Create: `apps/studio/src/Asharia.Editor/Panels/EditorPanelContributionBuilder.cs`
- Modify: `apps/studio/src/Asharia.Editor/Extensions/EditorModuleBuilder.cs`
- Modify: `apps/studio/src/Asharia.Editor/Extensions/EditorModuleDeclaration.cs`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/Panels/EditorPanelContributionBuilderTests.cs`

**Interfaces:**

- Consumes: Task 2 `EditorPanelDescriptor`.
- Produces: `EditorModuleBuilder.Panels`, ordered immutable `EditorModuleDeclaration.Panels`, same-module duplicate validation, and shared freeze behavior.

- [x] **Step 1: Write failing builder/declaration tests**

Create a test class that uses the existing module identity helper shape from `EditorModuleDeclarationTests` and includes these exact behaviors:

```csharp
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
```

The file also defines complete `CreateBuilder`, `CreateDefinition`, and `CreatePanel` helpers using `com.asharia.tests`, `Asharia.Tests.Editor`, `EditorContributionId.Create`, `UiBackendId.CodeFirst`, and `EditorFactoryLocalId.Create`. Add `using System.Collections.Generic;` and all required public namespaces.

Use these exact helpers:

```csharp
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
```

- [x] **Step 2: Run and verify compile RED**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~EditorPanelContributionBuilderTests
```

Expected: compile FAIL because `EditorModuleBuilder.Panels` and `EditorModuleDeclaration.Panels` do not exist.

- [x] **Step 3: Add `EditorPanelContributionBuilder`**

Create:

```csharp
using System;
using Asharia.Editor.Extensions;

namespace Asharia.Editor.Panels;

public sealed class EditorPanelContributionBuilder
{
    private readonly EditorModuleBuilder owner_;

    internal EditorPanelContributionBuilder(EditorModuleBuilder owner)
    {
        owner_ = owner;
    }

    public void Add(EditorPanelDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        owner_.AddPanel(descriptor);
    }
}
```

- [x] **Step 4: Extend `EditorModuleBuilder`**

Add imports for `Asharia.Editor.Contributions` and `Asharia.Editor.Panels`, then add:

```csharp
private readonly List<EditorPanelDescriptor> panels_ = [];
```

In the constructor:

```csharp
Panels = new EditorPanelContributionBuilder(this);
```

Add the public property:

```csharp
public EditorPanelContributionBuilder Panels { get; }
```

Pass `panels_` as the final argument to `EditorModuleDeclaration`. Add this internal method:

```csharp
internal void AddPanel(EditorPanelDescriptor descriptor)
{
    EnsureMutable();

    if (panels_.Any(panel => panel.Id == descriptor.Id))
    {
        throw new InvalidOperationException(
            $"Panel contribution '{descriptor.Id}' is already declared by this module.");
    }

    if (panels_.Any(panel => panel.ContentFactory == descriptor.ContentFactory))
    {
        throw new InvalidOperationException(
            $"Panel factory '{descriptor.ContentFactory}' is already declared by this module.");
    }

    panels_.Add(descriptor);
}
```

Add `using System.Linq;`. `EditorPanelContributionBuilder.Add` already rejects null before this internal method.

- [x] **Step 5: Extend the immutable declaration**

Add `using Asharia.Editor.Panels;`. Extend the constructor with:

```csharp
IEnumerable<EditorPanelDescriptor> panels
```

Assign:

```csharp
Panels = Copy(panels);
```

Expose:

```csharp
public IReadOnlyList<EditorPanelDescriptor> Panels { get; }
```

Keep `Copy<T>` as the only collection freeze path.

- [x] **Step 6: Verify GREEN and existing builder regressions**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~EditorPanelContributionBuilderTests|FullyQualifiedName~EditorModuleDeclarationTests"
dotnet build apps/studio/src/Asharia.Editor/Asharia.Editor.csproj -c Release --no-restore -warnaserror
```

Expected: Panel tests and all existing dependency/capability declaration tests PASS; build has 0 warnings/errors.

- [x] **Step 7: Commit**

```powershell
git add apps/studio/src/Asharia.Editor/Extensions apps/studio/src/Asharia.Editor/Panels/EditorPanelContributionBuilder.cs apps/studio/Tests/Asharia.Editor.Tests/Panels
git commit -m "feat(studio): add panel module declarations"
```

---

### Task 4: Freeze architecture gates, documentation, and full validation

**Files:**

- Modify: `apps/studio/Tests/Asharia.Studio.Architecture.Tests/ProjectReferenceGraphTests.cs`
- Modify: `apps/studio/docs/architecture/studio-code-framework.md`
- Modify: `apps/studio/docs/architecture/studio-extension-model.md`
- Modify: `apps/studio/docs/superpowers/plans/2026-07-11-studio-editor-framework-refactor.md`
- Modify: `apps/studio/docs/superpowers/specs/2026-07-11-studio-declaration-only-panel-contribution-design.md`
- Modify: `apps/studio/docs/superpowers/plans/2026-07-11-studio-declaration-only-panel-contribution.md`

**Interfaces:**

- Consumes: Tasks 1–3 complete public contract.
- Produces: enforceable forbidden-vocabulary/API-shape gates, current documentation, final evidence, and Draft PR handoff.

- [x] **Step 1: Write the failing public Panel API-shape gate**

Add to `ProjectReferenceGraphTests`:

```csharp
[Fact]
public void Public_panel_descriptor_exposes_only_declaration_time_contracts()
{
    var descriptorType = typeof(Asharia.Editor.Panels.EditorPanelDescriptor);
    var properties = descriptorType.GetProperties();

    Assert.Equal("Asharia.Editor", descriptorType.Assembly.GetName().Name);
    Assert.DoesNotContain(properties, property => property.PropertyType == typeof(Type));
    Assert.DoesNotContain(properties, property => property.PropertyType == typeof(object));
    Assert.DoesNotContain(
        properties,
        property => typeof(Delegate).IsAssignableFrom(property.PropertyType));
    Assert.DoesNotContain(
        properties,
        property => property.PropertyType.Name.Contains(
            "GenerationScopedFactoryHandle",
            StringComparison.Ordinal));
    Assert.DoesNotContain(
        properties,
        property => property.Name.Contains("Scope", StringComparison.Ordinal));
}
```

Before Tasks 1–3 this test would not compile. At Task 4, temporarily add a negative assertion for a forbidden property name that exists nowhere, confirm the test fails for the assertion itself, remove that temporary assertion, then run the real gate to GREEN. Do not commit the temporary assertion.

- [x] **Step 2: Strengthen source vocabulary checks**

For `Public_editor_sources_do_not_reference_ui_native_or_studio_implementation`, keep all existing forbidden tokens and add these exact tokens if absent:

```csharp
"Func<object>",
"GenerationScopedFactoryHandle",
"PackageGenerationId",
```

This Slice intentionally forbids those types from public source. Do not forbid the word `factory`; `EditorFactoryLocalId` is required.

- [x] **Step 3: Update current architecture facts**

Document these exact facts:

- `Asharia.Editor` now provides declaration-only Panel contribution IDs, descriptor, builder, duplicate validation, and immutable freeze.
- Module definition scope is the only Panel scope source.
- Descriptor contains `EditorFactoryLocalId`, not a generation handle or CLR factory.
- No Panel registry, factory binding, Dock integration, Host resolver, or runtime display exists yet.
- Legacy `PanelDescriptor(Func<object>)` remains compatibility-only in `Editor`.

Insert a completed Task 4a in the master refactor plan before the remaining L-sized Task 4 model-family extraction. Mark this spec `Implemented` only after all validation passes.

- [x] **Step 4: Run public and architecture gates**

```powershell
dotnet build apps/studio/src/Asharia.Editor/Asharia.Editor.csproj -c Release --no-restore -warnaserror
dotnet build apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --no-restore -warnaserror -t:Rebuild
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore
dotnet test apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --no-restore
```

Expected: both builds have 0 warnings/errors; all public and architecture tests PASS.

- [x] **Step 5: Run both Solution gates**

```powershell
dotnet test apps/studio/Editor.sln -c Release --no-restore
dotnet test apps/studio/Asharia.Studio.sln -c Release --no-restore
```

Expected: zero failed tests; legacy Studio runtime behavior is unchanged.

- [x] **Step 6: Run format, encoding, docs, and diff gates**

```powershell
dotnet format apps/studio/src/Asharia.Editor/Asharia.Editor.csproj --verify-no-changes --no-restore
dotnet format apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj --verify-no-changes --no-restore
dotnet format apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj --verify-no-changes --no-restore
powershell -ExecutionPolicy Bypass -File tools/check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools/check-doc-sync.ps1 -NoDocsReason "Studio-local architecture, spec, and plan docs under apps/studio/docs were updated; the repository-level checker only classifies root docs paths."
git diff --check
git diff --cached --check
```

Verify every branch-changed managed/Markdown file with strict `UTF8Encoding(false, true)` and reject a leading `EF BB BF` BOM.

- [x] **Step 7: Record #235 evidence and commit**

Prepare Issue evidence containing each RED/GREEN result, final test counts, identity/descriptor/builder/freeze behavior, architecture vocabulary checks, encoding/format/diff results, and explicit future Host/factory-binding follow-up.

Commit:

```powershell
git add apps/studio/Tests/Asharia.Studio.Architecture.Tests apps/studio/docs
git commit -m "docs(studio): record panel contribution contract"
```

- [x] **Step 8: Prepare the Draft PR handoff**

Do not push or create the PR before task reviews and the final whole-branch review. Prepare a Draft PR summary with `Closes #235`, TDD evidence, validation counts, declaration-only impact, two-stage factory identity, and explicit non-goals. The controller publishes only after the final review verdict is clean.
