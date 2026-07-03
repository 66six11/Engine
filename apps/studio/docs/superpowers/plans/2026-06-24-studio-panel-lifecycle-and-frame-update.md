# Studio Panel Lifecycle And Frame Update Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define the editor-framework side of Unity-like editor panel lifecycle and future `OnDraw` / per-frame data refresh without connecting a renderer, native viewport, script VM, or plugin loader.

**Architecture:** Add a logical panel lifecycle contract first, then build a frame/update request scheduler on top of attached and active panels. Dock and Shell own scheduling; panel content only declares what callbacks it supports. Rendering remains Avalonia/view-owned or future renderer-adapter-owned, not a responsibility of `EditorExtensionHost`.

**Tech Stack:** .NET 10, C#, xUnit, Avalonia MVVM ViewModels, existing `PanelInstanceManager`, `EditorDockWorkspaceViewModel`, `EditorDockWindowViewModel`, `EditorDockTabViewModel`.

---

## Source Spec

- `docs/superpowers/specs/2026-06-23-studio-extension-lifecycle-v0-design.md`
- Slice 3 left logical Activated/Deactivated callbacks deferred.
- User requirement: panels may need an editor-window style update/draw lifecycle, including some panels requesting data every frame, but this stage must stay editor-framework-only.

## External Reference Notes

- Unity custom editor windows separate window GUI code (`OnGUI`) from update/repaint scheduling; `EditorWindow.Update()` is called repeatedly for visible windows, and `EditorWindow.Repaint()` queues rendering for the next frame.
- Avalonia uses a single UI thread; control creation, layout, rendering and input happen on that thread. `DispatcherTimer` is the appropriate UI-thread periodic update mechanism, but it should be hidden behind Shell scheduling contracts.

## Scope

In:

- Add logical panel lifecycle callbacks for attach, activate, deactivate and detach.
- Keep callbacks on editor ViewModel/content objects, not raw Avalonia controls.
- Make Dock activation/close/reset/session disposal drive the callbacks deterministically.
- Design the frame update request contract and scheduler boundary.

Out:

- No native renderer/RHI, Vulkan viewport, swapchain, render graph, or engine frame loop.
- No plugin loader, ALC reload, script VM, or runtime-loaded XAML.
- No actual `OnRender` override, GPU draw call, scene query, or shell command line.
- No polling of real engine data; tests use fake panel content only.

## Task 1: Logical Panel Lifecycle Callbacks

**Files:**
- Create: `Core/Abstractions/IEditorPanelLifecycleSink.cs`
- Create: `Core/Models/EditorPanelLifecycleContext.cs`
- Modify: `Shell/Docking/PanelInstanceManager.cs`
- Modify: `Shell/ViewModels/EditorDockTabViewModel.cs`
- Modify: `Shell/ViewModels/EditorDockWorkspaceViewModel.cs`
- Modify: `Shell/ViewModels/EditorDockWindowViewModel.cs`
- Modify: `Tests/Editor.Tests/Shell/Docking/PanelInstanceManagerTests.cs`
- Modify: `Tests/Editor.Tests/Shell/ViewModels/EditorDockWorkspaceViewModelTests.cs`

- [x] **Step 1: Write failing lifecycle callback tests**

Prove:

- creating a tab invokes `OnPanelAttached`;
- activating a tab invokes `OnPanelActivated`;
- switching active tabs invokes previous `OnPanelDeactivated` before next `OnPanelActivated`;
- closing or resetting detaches before disposal;
- moving/reordering/floating a tab does not detach or dispose;
- callbacks are idempotent when release is called twice.

- [x] **Step 2: Implement panel lifecycle sink**

Add a content-side contract:

```csharp
public interface IEditorPanelLifecycleSink
{
    void OnPanelAttached(EditorPanelLifecycleContext context);
    void OnPanelActivated(EditorPanelLifecycleContext context);
    void OnPanelDeactivated(EditorPanelLifecycleContext context);
    void OnPanelDetached(EditorPanelLifecycleContext context);
}
```

`EditorPanelLifecycleContext` carries panel id, title, dock area, and whether this is the main workspace or floating workspace. It must not expose Avalonia controls or native handles.

- [x] **Step 3: Wire Dock lifecycle events**

`PanelInstanceManager` owns attach/detach. `EditorDockWorkspaceViewModel` owns active/deactive because active window/tab state lives there. Callback dispatch must tolerate content that does not implement the sink.

- [x] **Step 4: Verify Task 1**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~PanelInstanceManagerTests|FullyQualifiedName~EditorDockWorkspaceViewModelTests"
```

## Task 2: Frame Update Request Boundary

**Files:**
- Create: `Core/Models/EditorPanelFrameUpdateMode.cs`
- Create: `Core/Models/EditorPanelFrameUpdateRequest.cs`
- Create: `Core/Models/EditorPanelFrameContext.cs`
- Create: `Core/Abstractions/IEditorPanelFrameUpdateSink.cs`
- Create: `Shell/Services/EditorPanelFrameScheduler.cs`
- Create: `Tests/Editor.Tests/Shell/Services/EditorPanelFrameSchedulerTests.cs`

- [x] **Step 1: Write scheduler tests**

Prove:

- manual panels are not ticked automatically;
- visible/active panels receive ticks only while attached and not detached;
- active-only panels pause when deactivated;
- target FPS throttles callback frequency;
- `RequestRepaint` style callbacks are request-based and do not force a renderer.

- [x] **Step 2: Implement UI-neutral scheduler**

The scheduler accepts explicit `Tick(now)` calls in tests and later can be driven by an Avalonia `DispatcherTimer`. It must not call native rendering APIs.

- [x] **Step 3: Wire scheduler to panel lifecycle**

Register frame sinks when panel content attaches, update active state on activation/deactivation, unregister on detach.

## Task 3: Documentation And Verification

**Files:**
- Modify: `docs/Dock系统指南.md`
- Modify: `docs/编辑器UI平台规范.md`
- Modify: this plan file.

- [x] **Step 1: Document boundary**

Record that panel lifecycle/frame update v0 is editor-framework scheduling only. It is the future place where a native viewport adapter can request data or repaint, but it does not own renderer FPS, swapchain presentation, or engine simulation.

- [x] **Step 2: Full verification**

Run:

```powershell
dotnet test Editor.sln -c Release
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
rg -n "AssemblyLoadContext|NativeEditorBridge|C\+\+ ABI|script VM|ScriptExecutionHost|PluginLoader|LoadFromAssembly|ProcessStartInfo|System\.Diagnostics\.Process|Console\.Read|Console\.Write|VkDevice|Vulkan|Swapchain" Core Shell Features Tests
```
