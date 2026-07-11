# Studio Public Dialog Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish validated, immutable, UI-neutral Dialog request/result contracts in `Asharia.Editor`, migrate the compatibility Dialog Host to consume them, and retire the legacy Dialog model family.

**Architecture:** Build the public contract bottom-up: semantic action primitives first, then request/result invariants, then migrate the current Presentation host without exposing a service or platform type. Retire the legacy models only after all compatibility callers consume the public contract, and finish with assembly/API-shape gates plus current-fact documentation.

**Tech Stack:** .NET 10, C# 14, xUnit, SDK-style projects, CommunityToolkit.Mvvm and Avalonia only in the legacy compatibility Presentation; dependency-free `Asharia.Editor`.

## Global Constraints

- `Asharia.Editor` remains `net10.0` with no `ProjectReference` and no `PackageReference`.
- Public Dialog source must not reference Avalonia, Window, Control, ViewModel, delegate factories, `object` factories, Studio implementation, P/Invoke, native libraries, Vulkan, Package generation, Dock, Viewport, renderer, or Play Mode.
- The public namespace is exactly `Asharia.Editor.Dialogs`.
- The public contract has exactly seven types: `EditorDialogSeverity`, `EditorDialogActionRole`, `EditorDialogActionId`, `EditorDialogActionDescriptor`, `EditorDialogCompletionKind`, `EditorDialogRequest`, and `EditorDialogResult`.
- Action IDs use lowercase-kebab syntax; default is invalid and renders empty.
- A request has 1–3 actions, unique IDs and roles, exactly one `Dismiss`, at most one default, at most one destructive, at most one action emphasized by either flag, and no destructive `Dismiss`.
- Title is `null` or non-blank; Message is non-blank.
- Request actions are copied before read-only wrapping. Never expose a caller-owned array/list.
- `ActionInvoked` always has a valid action ID; `SystemDismissed` has no action ID.
- User results and future asynchronous operation cancellation remain distinct.
- Do not add `IEditorDialogService`, modal queues, owner-window routing, custom content, localization tokens, file pickers, progress, toast, notification, wizard, native dialog, or OS-specific action ordering.
- Do not keep duplicate legacy DTOs, wrappers, or type forwarding after migration.
- Use existing uppercase `apps/studio/Tests/`; do not create a lowercase sibling.
- All changed managed/Markdown files use strict UTF-8 without BOM.
- Preserve user-owned untracked `apps/studio/.vs/` and `qodana.yaml`.
- Every production behavior follows RED → GREEN and every task ends in a buildable commit.

---

### Task 1: Add Dialog action primitives

**Files:**

- Create: `apps/studio/src/Asharia.Editor/Dialogs/EditorDialogSeverity.cs`
- Create: `apps/studio/src/Asharia.Editor/Dialogs/EditorDialogActionRole.cs`
- Create: `apps/studio/src/Asharia.Editor/Dialogs/EditorDialogActionId.cs`
- Create: `apps/studio/src/Asharia.Editor/Dialogs/EditorDialogActionDescriptor.cs`
- Modify: `apps/studio/src/Asharia.Editor/Extensions/EditorIdentityValidation.cs`
- Modify: `apps/studio/src/Asharia.Editor/Contributions/UiBackendId.cs`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/Dialogs/EditorDialogActionTests.cs`

**Interfaces:**

- Consumes: existing dependency-free `Asharia.Editor` and internal identity validation patterns.
- Produces: stable severity/role enums, `EditorDialogActionId`, and validated `EditorDialogActionDescriptor` for Task 2.

- [ ] **Step 1: Write the failing action primitive tests**

Create `EditorDialogActionTests.cs`:

```csharp
using System;
using System.Linq;
using Asharia.Editor.Dialogs;
using Xunit;

namespace Asharia.Editor.Tests.Dialogs;

public sealed class EditorDialogActionTests
{
    [Fact]
    public void Dialog_enums_have_stable_names_and_values()
    {
        Assert.Equal(
            ["Information", "Warning", "Error"],
            Enum.GetNames<EditorDialogSeverity>());
        Assert.Equal(
            [0, 1, 2],
            Enum.GetValues<EditorDialogSeverity>().Select(value => (int)value));
        Assert.Equal(
            ["Primary", "Secondary", "Dismiss"],
            Enum.GetNames<EditorDialogActionRole>());
        Assert.Equal(
            [0, 1, 2],
            Enum.GetValues<EditorDialogActionRole>().Select(value => (int)value));
    }

    [Theory]
    [InlineData("save", true)]
    [InlineData("dont-save", true)]
    [InlineData("retry-2", true)]
    [InlineData("Save", false)]
    [InlineData("dont_save", false)]
    [InlineData("dont--save", false)]
    [InlineData("-save", false)]
    [InlineData("save-", false)]
    [InlineData("", false)]
    public void Action_id_uses_lowercase_kebab_syntax(string value, bool valid)
    {
        Assert.Equal(valid, EditorDialogActionId.TryCreate(value, out _));
    }

    [Fact]
    public void Default_action_id_is_invalid_and_renders_empty()
    {
        Assert.False(default(EditorDialogActionId).IsValid);
        Assert.Equal(string.Empty, default(EditorDialogActionId).Value);
        Assert.Equal(string.Empty, default(EditorDialogActionId).ToString());
    }

    [Fact]
    public void Action_descriptor_preserves_valid_values()
    {
        var action = new EditorDialogActionDescriptor(
            EditorDialogActionId.Create("delete"),
            "Delete",
            EditorDialogActionRole.Primary,
            isDefault: true,
            isDestructive: true);

        Assert.Equal(EditorDialogActionId.Create("delete"), action.Id);
        Assert.Equal("Delete", action.Text);
        Assert.Equal(EditorDialogActionRole.Primary, action.Role);
        Assert.True(action.IsDefault);
        Assert.True(action.IsDestructive);
    }

    [Fact]
    public void Action_descriptor_rejects_invalid_values()
    {
        Assert.Throws<ArgumentException>(() => new EditorDialogActionDescriptor(
            default,
            "Save",
            EditorDialogActionRole.Primary));
        Assert.Throws<ArgumentException>(() => new EditorDialogActionDescriptor(
            EditorDialogActionId.Create("save"),
            "   ",
            EditorDialogActionRole.Primary));
        Assert.Throws<ArgumentOutOfRangeException>(() => new EditorDialogActionDescriptor(
            EditorDialogActionId.Create("save"),
            "Save",
            (EditorDialogActionRole)42));
    }
}
```

- [ ] **Step 2: Run the focused test and verify compile RED**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~EditorDialogActionTests
```

Expected: compile FAIL because `Asharia.Editor.Dialogs` and all four public action primitive types do not exist.

- [ ] **Step 3: Extract shared lowercase-kebab validation**

Add to internal `EditorIdentityValidation` before its private helpers:

```csharp
public static bool IsLowercaseKebabId(string? value)
{
    if (string.IsNullOrEmpty(value))
    {
        return false;
    }

    var atSegmentStart = true;
    for (var index = 0; index < value.Length; index++)
    {
        var character = value[index];
        if (IsLowercaseAsciiLetter(character) || char.IsAsciiDigit(character))
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
```

Update `UiBackendId.TryCreate` to call:

```csharp
if (EditorIdentityValidation.IsLowercaseKebabId(value))
```

Add `using Asharia.Editor.Extensions;` and remove the now-duplicate private `IsLowercaseKebabId` method from `UiBackendId`.

- [ ] **Step 4: Implement the stable enums and action ID**

Create `EditorDialogSeverity.cs`:

```csharp
namespace Asharia.Editor.Dialogs;

public enum EditorDialogSeverity
{
    Information,
    Warning,
    Error,
}
```

Create `EditorDialogActionRole.cs`:

```csharp
namespace Asharia.Editor.Dialogs;

public enum EditorDialogActionRole
{
    Primary,
    Secondary,
    Dismiss,
}
```

Create `EditorDialogActionId.cs`:

```csharp
using System;
using Asharia.Editor.Extensions;

namespace Asharia.Editor.Dialogs;

public readonly record struct EditorDialogActionId
{
    private readonly string? value_;

    private EditorDialogActionId(string value)
    {
        value_ = value;
    }

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public static EditorDialogActionId Create(string value)
    {
        if (!TryCreate(value, out var result))
        {
            throw new ArgumentException(
                "Dialog action id must be a lowercase kebab id.",
                nameof(value));
        }

        return result;
    }

    public static bool TryCreate(string? value, out EditorDialogActionId result)
    {
        if (EditorIdentityValidation.IsLowercaseKebabId(value))
        {
            result = new EditorDialogActionId(value!);
            return true;
        }

        result = default;
        return false;
    }

    public override string ToString() => Value;
}
```

- [ ] **Step 5: Implement the validated action descriptor**

Create `EditorDialogActionDescriptor.cs`:

```csharp
using System;

namespace Asharia.Editor.Dialogs;

public sealed record EditorDialogActionDescriptor
{
    public EditorDialogActionDescriptor(
        EditorDialogActionId id,
        string text,
        EditorDialogActionRole role,
        bool isDefault = false,
        bool isDestructive = false)
    {
        if (!id.IsValid)
        {
            throw new ArgumentException("Dialog action identity is invalid.", nameof(id));
        }

        if (string.IsNullOrWhiteSpace(text))
        {
            throw new ArgumentException("Dialog action text must not be empty.", nameof(text));
        }

        if (!Enum.IsDefined(role))
        {
            throw new ArgumentOutOfRangeException(nameof(role), role, "Dialog action role is invalid.");
        }

        Id = id;
        Text = text;
        Role = role;
        IsDefault = isDefault;
        IsDestructive = isDestructive;
    }

    public EditorDialogActionId Id { get; }

    public string Text { get; }

    public EditorDialogActionRole Role { get; }

    public bool IsDefault { get; }

    public bool IsDestructive { get; }
}
```

- [ ] **Step 6: Verify GREEN and the existing backend identity regression**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~EditorDialogActionTests|FullyQualifiedName~EditorContributionIdentityTests"
dotnet build apps/studio/src/Asharia.Editor/Asharia.Editor.csproj -c Release --no-restore -warnaserror
```

Expected: Dialog action tests and existing `UiBackendId` cases PASS; public build has 0 warnings/errors.

- [ ] **Step 7: Commit**

```powershell
git add apps/studio/src/Asharia.Editor/Dialogs apps/studio/src/Asharia.Editor/Extensions/EditorIdentityValidation.cs apps/studio/src/Asharia.Editor/Contributions/UiBackendId.cs apps/studio/Tests/Asharia.Editor.Tests/Dialogs
git commit -m "feat(studio): add dialog action contracts"
```

---

### Task 2: Add immutable Dialog request and result contracts

**Files:**

- Create: `apps/studio/src/Asharia.Editor/Dialogs/EditorDialogCompletionKind.cs`
- Create: `apps/studio/src/Asharia.Editor/Dialogs/EditorDialogRequest.cs`
- Create: `apps/studio/src/Asharia.Editor/Dialogs/EditorDialogResult.cs`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/Dialogs/EditorDialogRequestTests.cs`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/Dialogs/EditorDialogResultTests.cs`

**Interfaces:**

- Consumes: Task 1 action primitives.
- Produces: fully validated `EditorDialogRequest` and invariant-safe `EditorDialogResult` for the compatibility Host in Task 3.

- [ ] **Step 1: Write failing request tests**

Create `EditorDialogRequestTests.cs`:

```csharp
using System;
using System.Collections.Generic;
using Asharia.Editor.Dialogs;
using Xunit;

namespace Asharia.Editor.Tests.Dialogs;

public sealed class EditorDialogRequestTests
{
    [Fact]
    public void Request_preserves_valid_values_and_defensively_freezes_actions()
    {
        var source = new List<EditorDialogActionDescriptor>
        {
            Action("save", EditorDialogActionRole.Primary, isDefault: true),
            Action("close", EditorDialogActionRole.Dismiss),
        };

        var request = new EditorDialogRequest(
            EditorDialogSeverity.Warning,
            title: null,
            "Unsaved changes",
            allowSystemDismiss: false,
            source);
        source.Clear();

        Assert.Equal(EditorDialogSeverity.Warning, request.Severity);
        Assert.Null(request.Title);
        Assert.Equal("Unsaved changes", request.Message);
        Assert.False(request.AllowSystemDismiss);
        Assert.Equal(["save", "close"], request.Actions.Select(action => action.Id.Value));
        var collection = Assert.IsAssignableFrom<ICollection<EditorDialogActionDescriptor>>(
            request.Actions);
        Assert.True(collection.IsReadOnly);
        Assert.Throws<NotSupportedException>(() => collection.Add(
            Action("other", EditorDialogActionRole.Secondary)));
    }

    [Fact]
    public void Request_accepts_one_two_or_three_actions()
    {
        Assert.Single(CreateRequest([Action("close", EditorDialogActionRole.Dismiss)]).Actions);
        Assert.Equal(2, CreateRequest(
            [
                Action("save", EditorDialogActionRole.Primary),
                Action("close", EditorDialogActionRole.Dismiss),
            ]).Actions.Count);
        Assert.Equal(3, CreateRequest(
            [
                Action("save", EditorDialogActionRole.Primary),
                Action("dont-save", EditorDialogActionRole.Secondary),
                Action("cancel", EditorDialogActionRole.Dismiss),
            ]).Actions.Count);
    }

    [Fact]
    public void Request_rejects_invalid_scalar_fields()
    {
        var actions = new[] { Action("close", EditorDialogActionRole.Dismiss) };

        Assert.Throws<ArgumentOutOfRangeException>(() => new EditorDialogRequest(
            (EditorDialogSeverity)42,
            "Title",
            "Message",
            true,
            actions));
        Assert.Throws<ArgumentException>(() => new EditorDialogRequest(
            EditorDialogSeverity.Information,
            "   ",
            "Message",
            true,
            actions));
        Assert.Throws<ArgumentException>(() => new EditorDialogRequest(
            EditorDialogSeverity.Information,
            null,
            "   ",
            true,
            actions));
    }

    [Fact]
    public void Request_rejects_invalid_action_collection_shape()
    {
        Assert.Throws<ArgumentNullException>(() => new EditorDialogRequest(
            EditorDialogSeverity.Information,
            "Title",
            "Message",
            true,
            null!));
        Assert.Throws<ArgumentException>(() => CreateRequest([]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [
                Action("one", EditorDialogActionRole.Primary),
                Action("two", EditorDialogActionRole.Secondary),
                Action("close", EditorDialogActionRole.Dismiss),
                Action("four", EditorDialogActionRole.Primary),
            ]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [null!, Action("close", EditorDialogActionRole.Dismiss)]));
    }

    [Fact]
    public void Request_rejects_duplicate_ids_roles_and_missing_dismiss()
    {
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [
                Action("same", EditorDialogActionRole.Primary),
                Action("same", EditorDialogActionRole.Dismiss),
            ]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [
                Action("one", EditorDialogActionRole.Primary),
                Action("two", EditorDialogActionRole.Primary),
                Action("close", EditorDialogActionRole.Dismiss),
            ]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [Action("save", EditorDialogActionRole.Primary)]));
    }

    [Fact]
    public void Request_rejects_invalid_emphasis_and_destructive_dismiss()
    {
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [
                Action("save", EditorDialogActionRole.Primary, isDefault: true),
                Action("close", EditorDialogActionRole.Dismiss, isDefault: true),
            ]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [
                Action("delete", EditorDialogActionRole.Primary, isDestructive: true),
                Action("close", EditorDialogActionRole.Dismiss, isDestructive: true),
            ]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [
                Action("delete", EditorDialogActionRole.Primary, isDestructive: true),
                Action("close", EditorDialogActionRole.Dismiss, isDefault: true),
            ]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [Action("close", EditorDialogActionRole.Dismiss, isDestructive: true)]));
    }

    private static EditorDialogRequest CreateRequest(
        IReadOnlyList<EditorDialogActionDescriptor> actions)
    {
        return new EditorDialogRequest(
            EditorDialogSeverity.Information,
            "Title",
            "Message",
            allowSystemDismiss: true,
            actions);
    }

    private static EditorDialogActionDescriptor Action(
        string id,
        EditorDialogActionRole role,
        bool isDefault = false,
        bool isDestructive = false)
    {
        return new EditorDialogActionDescriptor(
            EditorDialogActionId.Create(id),
            id,
            role,
            isDefault,
            isDestructive);
    }
}
```

Add `using System.Linq;` because the defensive snapshot assertion uses `Select`.

- [ ] **Step 2: Write failing result tests**

Create `EditorDialogResultTests.cs`:

```csharp
using System;
using System.Linq;
using Asharia.Editor.Dialogs;
using Xunit;

namespace Asharia.Editor.Tests.Dialogs;

public sealed class EditorDialogResultTests
{
    [Fact]
    public void Completion_enum_has_stable_names_and_values()
    {
        Assert.Equal(
            ["ActionInvoked", "SystemDismissed"],
            Enum.GetNames<EditorDialogCompletionKind>());
        Assert.Equal(
            [0, 1],
            Enum.GetValues<EditorDialogCompletionKind>().Select(value => (int)value));
    }

    [Fact]
    public void Action_result_contains_exact_action_identity()
    {
        var id = EditorDialogActionId.Create("save");

        var result = EditorDialogResult.ActionInvoked(id);

        Assert.Equal(EditorDialogCompletionKind.ActionInvoked, result.Completion);
        Assert.Equal(id, result.ActionId);
    }

    [Fact]
    public void System_dismissed_result_contains_no_action_identity()
    {
        var result = EditorDialogResult.SystemDismissed();

        Assert.Equal(EditorDialogCompletionKind.SystemDismissed, result.Completion);
        Assert.Null(result.ActionId);
    }

    [Fact]
    public void Action_result_rejects_invalid_identity()
    {
        Assert.Throws<ArgumentException>(() => EditorDialogResult.ActionInvoked(default));
    }
}
```

- [ ] **Step 3: Run and verify compile RED**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~EditorDialogRequestTests|FullyQualifiedName~EditorDialogResultTests"
```

Expected: compile FAIL because `EditorDialogRequest`, `EditorDialogCompletionKind`, and `EditorDialogResult` do not exist.

- [ ] **Step 4: Implement completion kind and invariant-safe result**

Create `EditorDialogCompletionKind.cs`:

```csharp
namespace Asharia.Editor.Dialogs;

public enum EditorDialogCompletionKind
{
    ActionInvoked,
    SystemDismissed,
}
```

Create `EditorDialogResult.cs`:

```csharp
using System;

namespace Asharia.Editor.Dialogs;

public sealed record EditorDialogResult
{
    private EditorDialogResult(
        EditorDialogCompletionKind completion,
        EditorDialogActionId? actionId)
    {
        Completion = completion;
        ActionId = actionId;
    }

    public EditorDialogCompletionKind Completion { get; }

    public EditorDialogActionId? ActionId { get; }

    public static EditorDialogResult ActionInvoked(EditorDialogActionId actionId)
    {
        if (!actionId.IsValid)
        {
            throw new ArgumentException("Dialog action identity is invalid.", nameof(actionId));
        }

        return new EditorDialogResult(EditorDialogCompletionKind.ActionInvoked, actionId);
    }

    public static EditorDialogResult SystemDismissed()
    {
        return new EditorDialogResult(EditorDialogCompletionKind.SystemDismissed, null);
    }
}
```

- [ ] **Step 5: Implement validated request with a defensive snapshot**

Create `EditorDialogRequest.cs`:

```csharp
using System;
using System.Collections.Generic;
using System.Linq;

namespace Asharia.Editor.Dialogs;

public sealed class EditorDialogRequest
{
    public EditorDialogRequest(
        EditorDialogSeverity severity,
        string? title,
        string message,
        bool allowSystemDismiss,
        IReadOnlyList<EditorDialogActionDescriptor> actions)
    {
        if (!Enum.IsDefined(severity))
        {
            throw new ArgumentOutOfRangeException(
                nameof(severity),
                severity,
                "Dialog severity is invalid.");
        }

        if (title is not null && string.IsNullOrWhiteSpace(title))
        {
            throw new ArgumentException("Dialog title must be null or non-empty.", nameof(title));
        }

        if (string.IsNullOrWhiteSpace(message))
        {
            throw new ArgumentException("Dialog message must not be empty.", nameof(message));
        }

        ArgumentNullException.ThrowIfNull(actions);
        if (actions.Count is < 1 or > 3)
        {
            throw new ArgumentException("Dialog requests require one to three actions.", nameof(actions));
        }

        var copied = actions.ToArray();
        if (copied.Any(static action => action is null))
        {
            throw new ArgumentException("Dialog actions must not contain null.", nameof(actions));
        }

        if (copied.Select(static action => action.Id).Distinct().Count() != copied.Length)
        {
            throw new ArgumentException("Dialog action identities must be unique.", nameof(actions));
        }

        if (copied.Select(static action => action.Role).Distinct().Count() != copied.Length)
        {
            throw new ArgumentException("Dialog action roles must be unique.", nameof(actions));
        }

        if (copied.Count(static action => action.Role == EditorDialogActionRole.Dismiss) != 1)
        {
            throw new ArgumentException("Dialog requests require exactly one dismiss action.", nameof(actions));
        }

        if (copied.Count(static action => action.IsDefault) > 1)
        {
            throw new ArgumentException("Dialog requests allow at most one default action.", nameof(actions));
        }

        if (copied.Count(static action => action.IsDestructive) > 1)
        {
            throw new ArgumentException("Dialog requests allow at most one destructive action.", nameof(actions));
        }

        if (copied.Count(static action => action.IsDefault || action.IsDestructive) > 1)
        {
            throw new ArgumentException(
                "Dialog requests allow at most one emphasized action.",
                nameof(actions));
        }

        if (copied.Any(static action =>
            action.Role == EditorDialogActionRole.Dismiss && action.IsDestructive))
        {
            throw new ArgumentException("A dismiss action cannot be destructive.", nameof(actions));
        }

        Severity = severity;
        Title = title;
        Message = message;
        AllowSystemDismiss = allowSystemDismiss;
        Actions = Array.AsReadOnly(copied);
    }

    public EditorDialogSeverity Severity { get; }

    public string? Title { get; }

    public string Message { get; }

    public bool AllowSystemDismiss { get; }

    public IReadOnlyList<EditorDialogActionDescriptor> Actions { get; }
}
```

- [ ] **Step 6: Verify GREEN and the full public contract suite**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~EditorDialog
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore
dotnet build apps/studio/src/Asharia.Editor/Asharia.Editor.csproj -c Release --no-restore -warnaserror
```

Expected: all Dialog and existing public tests PASS; public build has 0 warnings/errors.

- [ ] **Step 7: Commit**

```powershell
git add apps/studio/src/Asharia.Editor/Dialogs apps/studio/Tests/Asharia.Editor.Tests/Dialogs
git commit -m "feat(studio): add dialog request contracts"
```

---

### Task 3: Migrate the compatibility Dialog Host

**Files:**

- Create: `apps/studio/Shell/ViewModels/Dialogs/StudioDialogRequests.cs`
- Modify: `apps/studio/Shell/ViewModels/Dialogs/EditorDialogHostViewModel.cs`
- Modify: `apps/studio/Shell/ViewModels/Dialogs/EditorDialogButtonViewModel.cs`
- Modify: `apps/studio/Shell/ViewModels/Dialogs/EditorDialogHostDesignViewModel.cs`
- Modify: `apps/studio/Shell/ViewModels/Windowing/MainWindowViewModel.cs`
- Modify: `apps/studio/Shell/Views/Dialogs/EditorDialogHostView.axaml.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/ViewModels/Dialogs/EditorDialogHostViewModelTests.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/Views/Dialogs/EditorDialogHostViewTests.cs`
- Modify: Dialog namespace imports in `apps/studio/Tests/Editor.Tests/Shell/ViewModels/Windowing/MainWindowViewModelTests.cs` if present after compilation.

**Interfaces:**

- Consumes: Task 2 public request/result contract.
- Produces: compatibility Presentation that uses only `Asharia.Editor.Dialogs`; legacy model files remain temporarily for Task 4 removal.

- [ ] **Step 1: Replace compatibility Host tests with the new semantics**

Replace `EditorDialogHostViewModelTests.cs` with:

```csharp
using System;
using System.Linq;
using System.Threading.Tasks;
using Asharia.Editor.Dialogs;
using Editor.Shell.ViewModels.Dialogs;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Dialogs;

public sealed class EditorDialogHostViewModelTests
{
    [Fact]
    public void Initial_state_is_closed()
    {
        var host = new EditorDialogHostViewModel();

        Assert.False(host.IsOpen);
        Assert.Null(host.ActiveRequest);
        Assert.Empty(host.Buttons);
    }

    [Fact]
    public void Design_preview_opens_about_dialog()
    {
        var host = new EditorDialogHostDesignViewModel();

        Assert.True(host.IsOpen);
        Assert.Equal("About Studio", host.Title);
        Assert.Single(host.Buttons);
    }

    [Fact]
    public void ShowAsync_projects_action_semantics()
    {
        var host = new EditorDialogHostViewModel();
        var request = CreateRequest(
            allowSystemDismiss: true,
            [
                new EditorDialogActionDescriptor(
                    EditorDialogActionId.Create("delete"),
                    "Delete",
                    EditorDialogActionRole.Primary,
                    isDefault: true,
                    isDestructive: true),
                DismissAction(isDefault: false),
            ]);

        var resultTask = host.ShowAsync(request);

        Assert.True(host.IsOpen);
        Assert.Equal("Title", host.Title);
        Assert.Equal("Message", host.Message);
        var button = host.Buttons.First();
        Assert.Equal("delete", button.Id);
        Assert.Equal("Delete", button.Text);
        Assert.Equal(EditorDialogActionRole.Primary, button.Role);
        Assert.True(button.IsDefault);
        Assert.True(button.IsDestructive);
        Assert.False(resultTask.IsCompleted);
    }

    [Fact]
    public async Task Action_command_returns_exact_identity_and_closes_host()
    {
        var host = new EditorDialogHostViewModel();
        var resultTask = host.ShowAsync(CreateRequest(
            allowSystemDismiss: true,
            [DismissAction()]));

        host.Buttons.Single().Command.Execute(null);

        var result = await resultTask;
        Assert.Equal(EditorDialogCompletionKind.ActionInvoked, result.Completion);
        Assert.Equal(EditorDialogActionId.Create("close"), result.ActionId);
        Assert.False(host.IsOpen);
        Assert.Null(host.ActiveRequest);
        Assert.Empty(host.Buttons);
    }

    [Fact]
    public async Task TrySystemDismiss_completes_allowed_dialog()
    {
        var host = new EditorDialogHostViewModel();
        var resultTask = host.ShowAsync(CreateRequest(
            allowSystemDismiss: true,
            [DismissAction()]));

        Assert.True(host.TrySystemDismiss());

        var result = await resultTask;
        Assert.Equal(EditorDialogCompletionKind.SystemDismissed, result.Completion);
        Assert.Null(result.ActionId);
        Assert.False(host.IsOpen);
    }

    [Fact]
    public void TrySystemDismiss_ignores_non_dismissible_dialog()
    {
        var host = new EditorDialogHostViewModel();
        _ = host.ShowAsync(CreateRequest(
            allowSystemDismiss: false,
            [DismissAction()]));

        Assert.False(host.TrySystemDismiss());
        Assert.True(host.IsOpen);
    }

    [Fact]
    public void ShowAsync_rejects_second_active_dialog()
    {
        var host = new EditorDialogHostViewModel();
        _ = host.ShowAsync(CreateRequest(true, [DismissAction()]));

        var exception = Assert.Throws<InvalidOperationException>(
            () => { _ = host.ShowAsync(CreateRequest(true, [DismissAction()])); });
        Assert.Contains("already active", exception.Message);
    }

    private static EditorDialogRequest CreateRequest(
        bool allowSystemDismiss,
        EditorDialogActionDescriptor[] actions)
    {
        return new EditorDialogRequest(
            EditorDialogSeverity.Information,
            "Title",
            "Message",
            allowSystemDismiss,
            actions);
    }

    private static EditorDialogActionDescriptor DismissAction(bool isDefault = true)
    {
        return new EditorDialogActionDescriptor(
            EditorDialogActionId.Create("close"),
            "Close",
            EditorDialogActionRole.Dismiss,
            isDefault: isDefault);
    }
}
```

Replace `EditorDialogHostViewTests.cs` with:

```csharp
using System.Threading.Tasks;
using Asharia.Editor.Dialogs;
using Editor.Shell.ViewModels.Dialogs;
using Editor.Shell.Views.Dialogs;
using Xunit;

namespace Editor.Tests.Shell.Views.Dialogs;

public sealed class EditorDialogHostViewTests
{
    [Fact]
    public void TrySystemDismissDialog_rejects_non_dialog_context()
    {
        Assert.False(EditorDialogHostView.TrySystemDismissDialog(new object()));
    }

    [Fact]
    public async Task TrySystemDismissDialog_dismisses_active_dialog()
    {
        var host = new EditorDialogHostViewModel();
        var resultTask = host.ShowAsync(new EditorDialogRequest(
            EditorDialogSeverity.Information,
            "About",
            "Studio editor shell",
            allowSystemDismiss: true,
            [
                new EditorDialogActionDescriptor(
                    EditorDialogActionId.Create("close"),
                    "Close",
                    EditorDialogActionRole.Dismiss,
                    isDefault: true),
            ]));

        Assert.True(EditorDialogHostView.TrySystemDismissDialog(host));

        var result = await resultTask;
        Assert.Equal(EditorDialogCompletionKind.SystemDismissed, result.Completion);
        Assert.False(host.IsOpen);
    }
}
```

- [ ] **Step 2: Run and verify compatibility compile RED**

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~EditorDialogHostViewModelTests|FullyQualifiedName~EditorDialogHostViewTests"
```

Expected: compile FAIL because `EditorDialogHostViewModel` still accepts the legacy request/result types and does not expose `TrySystemDismiss` or new action properties.

- [ ] **Step 3: Add one internal About request factory**

Create `StudioDialogRequests.cs`:

```csharp
using Asharia.Editor.Dialogs;

namespace Editor.Shell.ViewModels.Dialogs;

internal static class StudioDialogRequests
{
    public static EditorDialogRequest About()
    {
        return new EditorDialogRequest(
            EditorDialogSeverity.Information,
            "About Studio",
            "Studio editor shell for VkEngine.",
            allowSystemDismiss: true,
            [
                new EditorDialogActionDescriptor(
                    EditorDialogActionId.Create("close"),
                    "Close",
                    EditorDialogActionRole.Dismiss,
                    isDefault: true),
            ]);
    }
}
```

- [ ] **Step 4: Migrate the button projection**

Replace `EditorDialogButtonViewModel.cs` with:

```csharp
using Asharia.Editor.Dialogs;
using CommunityToolkit.Mvvm.Input;

namespace Editor.Shell.ViewModels.Dialogs;

public sealed class EditorDialogButtonViewModel
{
    internal EditorDialogButtonViewModel(
        EditorDialogActionDescriptor descriptor,
        IRelayCommand command)
    {
        Descriptor = descriptor;
        Command = command;
    }

    internal EditorDialogActionDescriptor Descriptor { get; }

    public string Id => Descriptor.Id.Value;

    public string Text => Descriptor.Text;

    public EditorDialogActionRole Role => Descriptor.Role;

    public bool IsDefault => Descriptor.IsDefault;

    public bool IsDestructive => Descriptor.IsDestructive;

    public IRelayCommand Command { get; }
}
```

- [ ] **Step 5: Migrate the Host state and completion mapping**

In `EditorDialogHostViewModel.cs`:

1. Replace the legacy Dialog namespace import with `using Asharia.Editor.Dialogs;`.
2. Keep the existing field/property types, which now resolve to the public request/result types.
3. Replace `request.Buttons` with `request.Actions`.
4. Replace the action command body with:

```csharp
new RelayCommand(() => Complete(EditorDialogResult.ActionInvoked(action.Id)))
```

5. Replace `TryCancel()` with:

```csharp
public bool TrySystemDismiss()
{
    if (ActiveRequest is null || !ActiveRequest.AllowSystemDismiss)
    {
        return false;
    }

    Complete(EditorDialogResult.SystemDismissed());
    return true;
}
```

The complete projection becomes:

```csharp
Buttons = request.Actions
    .Select(action => new EditorDialogButtonViewModel(
        action,
        new RelayCommand(() => Complete(EditorDialogResult.ActionInvoked(action.Id)))))
    .ToArray();
```

Keep `TaskCreationOptions.RunContinuationsAsynchronously` and the existing clear-before-`TrySetResult` order.

- [ ] **Step 6: Migrate Presentation callers and Escape bridge**

Replace all of `EditorDialogHostDesignViewModel.cs` so no legacy namespace import remains:

```csharp
namespace Editor.Shell.ViewModels.Dialogs;

public sealed class EditorDialogHostDesignViewModel : EditorDialogHostViewModel
{
    public EditorDialogHostDesignViewModel()
    {
        _ = ShowAsync(StudioDialogRequests.About());
    }
}
```

Remove `using Editor.Core.Models.Dialogs;` from `MainWindowViewModel.cs`. The file already imports `Editor.Shell.ViewModels.Dialogs`, so no replacement Dialog import is required.

Replace `MainWindowViewModel.OpenAboutDialogFromCommand` with:

```csharp
private bool OpenAboutDialogFromCommand()
{
    _ = DialogHost.ShowAsync(StudioDialogRequests.About());
    return true;
}
```

In `EditorDialogHostView.axaml.cs`, rename `TryCancelDialog` to:

```csharp
internal static bool TrySystemDismissDialog(object? dataContext)
{
    return dataContext is EditorDialogHostViewModel viewModel
        && viewModel.TrySystemDismiss();
}
```

Update `OnDialogHostKeyDown` to call `TrySystemDismissDialog(DataContext)`.

- [ ] **Step 7: Verify GREEN and all compatibility regressions**

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~Dialog|FullyQualifiedName~MainWindowViewModelTests"
dotnet test apps/studio/Editor.sln -c Release --no-restore
```

Expected: focused Dialog/MainWindow tests and all 600 legacy tests PASS. The old legacy Dialog models still compile but have no production callers outside their own files and the soon-to-change architecture assertion.

- [ ] **Step 8: Prove no compatibility production caller uses the legacy namespace**

```powershell
rg -n "using Editor\.Core\.Models\.Dialogs|Editor\.Core\.Models\.Dialogs\." apps/studio --glob "*.cs"
```

Expected: matches only in `Core/Models/Dialogs/**` declarations, legacy architecture tests, or files intentionally deferred to Task 4. No `Shell/**`, `Features/**`, `UI/**`, or production test behavior file may remain.

- [ ] **Step 9: Commit**

```powershell
git add apps/studio/Shell/ViewModels/Dialogs apps/studio/Shell/ViewModels/Windowing/MainWindowViewModel.cs apps/studio/Shell/Views/Dialogs/EditorDialogHostView.axaml.cs apps/studio/Tests/Editor.Tests/Shell/ViewModels/Dialogs apps/studio/Tests/Editor.Tests/Shell/Views/Dialogs apps/studio/Tests/Editor.Tests/Shell/ViewModels/Windowing
git commit -m "refactor(studio): consume public dialog contracts"
```

---

### Task 4: Retire legacy Dialog models and add architecture gates

**Files:**

- Modify: `apps/studio/Tests/Asharia.Studio.Architecture.Tests/ProjectReferenceGraphTests.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Architecture/StudioLayeringTests.cs`
- Delete: `apps/studio/Core/Models/Dialogs/EditorDialogButtonDescriptor.cs`
- Delete: `apps/studio/Core/Models/Dialogs/EditorDialogButtonRole.cs`
- Delete: `apps/studio/Core/Models/Dialogs/EditorDialogKind.cs`
- Delete: `apps/studio/Core/Models/Dialogs/EditorDialogRequest.cs`
- Delete: `apps/studio/Core/Models/Dialogs/EditorDialogResult.cs`
- Delete: `apps/studio/Core/Models/Dialogs/EditorDialogResultKind.cs`

**Interfaces:**

- Consumes: Tasks 1–3 completed public contract and migrated callers.
- Produces: one source of truth, public assembly ownership, and enforceable UI/native-neutral gates.

- [ ] **Step 1: Write the failing public ownership/API-shape gate**

Add `using Asharia.Editor.Dialogs;` to `ProjectReferenceGraphTests.cs`, then add:

```csharp
[Fact]
public void Public_dialog_contracts_replace_legacy_dialog_models()
{
    var studioRoot = FindStudioRoot();
    var legacyRoot = Path.Combine(studioRoot, "Core", "Models", "Dialogs");
    Assert.False(Directory.Exists(legacyRoot), $"Legacy Dialog models remain at {legacyRoot}.");

    var dialogTypes = typeof(EditorDialogRequest).Assembly
        .GetExportedTypes()
        .Where(type => type.Namespace == "Asharia.Editor.Dialogs")
        .OrderBy(type => type.Name, StringComparer.Ordinal)
        .ToArray();

    Assert.Equal(
        [
            "EditorDialogActionDescriptor",
            "EditorDialogActionId",
            "EditorDialogActionRole",
            "EditorDialogCompletionKind",
            "EditorDialogRequest",
            "EditorDialogResult",
            "EditorDialogSeverity",
        ],
        dialogTypes.Select(type => type.Name));
    Assert.All(
        dialogTypes,
        type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));

    var properties = dialogTypes.SelectMany(type => type.GetProperties()).ToArray();
    Assert.DoesNotContain(properties, property => property.PropertyType == typeof(Type));
    Assert.DoesNotContain(properties, property => property.PropertyType == typeof(object));
    Assert.DoesNotContain(
        properties,
        property => typeof(Delegate).IsAssignableFrom(property.PropertyType));

    var sourceRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "Dialogs");
    var source = string.Join(
        Environment.NewLine,
        Directory.EnumerateFiles(sourceRoot, "*.cs", SearchOption.AllDirectories)
            .Select(File.ReadAllText));
    foreach (var forbidden in new[]
    {
        "Avalonia",
        "Window",
        "Control",
        "ViewModel",
        "Func<object>",
        "LibraryImport",
        "DllImport",
        "Vulkan",
        "Asharia.Studio.",
        "Editor.Core",
    })
    {
        Assert.DoesNotContain(forbidden, source, StringComparison.Ordinal);
    }
}
```

- [ ] **Step 2: Run and verify assertion RED**

```powershell
dotnet test apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~Public_dialog_contracts_replace_legacy_dialog_models
```

Expected: test FAIL because `apps/studio/Core/Models/Dialogs` still exists. All public type/API-shape assertions compile.

- [ ] **Step 3: Remove the obsolete legacy physical-folder test**

Delete the entire `Dialog_core_models_live_in_dialog_model_folder` fact from `StudioLayeringTests.cs`. Do not replace it with another legacy directory assertion; the target architecture test owns the new boundary.

- [ ] **Step 4: Delete all six legacy Dialog model files**

Delete only the exact six files listed in this task. Confirm the folder is empty/absent and do not delete sibling `Core/Models` families.

- [ ] **Step 5: Verify GREEN, one source of truth, and both Solutions**

```powershell
dotnet test apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --no-restore --filter FullyQualifiedName~Public_dialog_contracts_replace_legacy_dialog_models
dotnet test apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --no-restore
dotnet test apps/studio/Editor.sln -c Release --no-restore
dotnet test apps/studio/Asharia.Studio.sln -c Release --no-restore
rg -n "Editor\.Core\.Models\.Dialogs|EditorDialogButtonDescriptor|EditorDialogButtonRole|EditorDialogKind|EditorDialogResultKind" apps/studio --glob "*.cs"
```

Expected: architecture and both Solutions PASS. The final `rg` has no production/source matches; if historical docs mention old types, leave them for Task 5 documentation classification rather than modifying source outside scope.

- [ ] **Step 6: Commit**

```powershell
git add apps/studio/Core/Models/Dialogs apps/studio/Tests/Asharia.Studio.Architecture.Tests/ProjectReferenceGraphTests.cs apps/studio/Tests/Editor.Tests/Architecture/StudioLayeringTests.cs
git commit -m "refactor(studio): retire legacy dialog models"
```

---

### Task 5: Update current facts, run final gates, and prepare PR handoff

**Files:**

- Modify: `apps/studio/docs/architecture/studio-code-framework.md`
- Modify: `apps/studio/docs/architecture/studio-extension-model.md`
- Modify: `apps/studio/docs/superpowers/plans/2026-07-11-studio-editor-framework-refactor.md`
- Modify: `apps/studio/docs/superpowers/specs/2026-07-11-studio-public-dialog-contract-design.md`
- Modify: `apps/studio/docs/superpowers/plans/2026-07-11-studio-public-dialog-contract.md`

**Interfaces:**

- Consumes: Tasks 1–4 complete implementation.
- Produces: implemented architecture facts, final evidence, completed plan checkboxes, and Draft PR handoff for #237.

- [ ] **Step 1: Update current architecture facts**

Document these exact facts:

- `Asharia.Editor.Dialogs` now owns the seven UI-neutral Dialog public types.
- Action ID, role, default, destructive and completion semantics are independent.
- Request construction validates all invariants and freezes a defensive read-only action snapshot.
- Public action declaration order is deterministic but is not a cross-platform screen-order promise.
- Existing compatibility Dialog Host consumes the public contract; Presentation still owns overlay/focus/action projection and rejects a second active modal.
- User system-dismiss result remains distinct from future operation cancellation.
- No dialog service, owner-window routing, custom content, platform ordering, localization, file picker, progress, notification, or modal queue exists yet.
- Legacy `Editor.Core.Models.Dialogs` no longer exists.

Insert completed Task 4b in the master refactor plan after Task 4a and before the remaining L-sized Task 4 model-family extraction.

- [ ] **Step 2: Run public and architecture warning-as-error gates**

```powershell
dotnet build apps/studio/src/Asharia.Editor/Asharia.Editor.csproj -c Release --no-restore -warnaserror
dotnet build apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --no-restore -warnaserror -t:Rebuild
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore
dotnet test apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --no-restore
```

Expected: both builds have 0 warnings/errors; all public and architecture tests PASS.

- [ ] **Step 3: Run focused compatibility and both Solution gates**

```powershell
dotnet test apps/studio/Tests/Editor.Tests/Editor.Tests.csproj -c Release --no-restore --filter "FullyQualifiedName~Dialog|FullyQualifiedName~MainWindowViewModelTests"
dotnet test apps/studio/Editor.sln -c Release --no-restore
dotnet test apps/studio/Asharia.Studio.sln -c Release --no-restore
```

Expected: zero failed tests; current user-visible About/Dialog behavior remains covered.

- [ ] **Step 4: Run format, encoding, docs, and diff gates**

```powershell
dotnet format apps/studio/src/Asharia.Editor/Asharia.Editor.csproj --verify-no-changes --no-restore
dotnet format apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj --verify-no-changes --no-restore
dotnet format apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj --verify-no-changes --no-restore
dotnet format apps/studio/Editor.csproj --verify-no-changes --no-restore
powershell -ExecutionPolicy Bypass -File tools/check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools/check-doc-sync.ps1 -NoDocsReason "Studio-local architecture, spec, and plan docs under apps/studio/docs were updated; the repository-level checker only classifies root docs paths."
git diff --check
git diff --cached --check
```

Verify every branch-changed managed/Markdown file with strict `UTF8Encoding(false, true)` and reject a leading `EF BB BF` BOM.

- [ ] **Step 5: Mark the spec and implementation plan complete**

Only after Steps 2–4 pass:

- change the design spec status to `Implemented`;
- add actual public/architecture/legacy test counts and build/format/encoding/diff evidence;
- mark every implementation-plan checkbox complete;
- record the intentional future order: Application static Host/service resolution, then `IEditorDialogService`, then owner-window routing/per-platform layout tests.

- [ ] **Step 6: Record #237 evidence and commit**

Prepare Issue evidence containing every RED/GREEN result, final test counts, public contract invariants, compatibility migration, architecture gate, deleted legacy model family, documentation, encoding/format/diff results, and explicit future service/Host boundary.

```powershell
git add apps/studio/docs apps/studio/Tests/Asharia.Studio.Architecture.Tests apps/studio/Tests/Editor.Tests/Architecture
git commit -m "docs(studio): record public dialog contracts"
```

- [ ] **Step 7: Prepare the Draft PR handoff**

Do not push or create the PR before task reviews and the final whole-branch review. Prepare a Draft PR summary with:

- `Closes #237`;
- external evidence used to shape roles/ownership/immutability;
- TDD evidence and final validation counts;
- public ABI and compatibility impact;
- explicit Windows/Linux/macOS neutrality;
- explicit non-goals and future dialog-service dependency order.
