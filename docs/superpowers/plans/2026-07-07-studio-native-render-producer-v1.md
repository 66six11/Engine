# Studio Native Render Producer V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract Studio's native shared viewport rendering work into a native render producer boundary with observable runtime stats, while preserving the current C ABI and Avalonia managed ownership rules.

**Architecture:** `EditorSharedViewportRuntime` remains the process-level owner of the Vulkan context, outstanding packet tracking, shutdown gating, and ABI-facing packet lifetime. A new `EditorSharedViewportRenderProducer` owns the native render-frame production path below the runtime boundary and returns native-owned packet state for release. This slice does not introduce a persistent shared image pool yet because descriptor, transient resource, and in-flight GPU lifetime need a separate pool/epoch design.

**Tech Stack:** C++23, Vulkan, VMA, `renderer_basic_vulkan`, `editor_native` C ABI, Avalonia Studio managed interop.

## Global Constraints

- C/C++ sources must be UTF-8 with BOM; all other text files must be UTF-8 without BOM.
- `asharia::rhi_vulkan` must not depend on RenderGraph.
- `asharia::renderer_basic` must remain backend-agnostic; Vulkan command recording stays in `asharia::renderer_basic_vulkan`.
- Studio managed code must not record Vulkan commands or own native GPU resources.
- Do not add `vkDeviceWaitIdle` or render-loop GPU stalls.
- Preserve the existing `editor_viewport_*` C ABI signatures in this slice.

---

### Task 1: Add Native Producer Stats Smoke

**Files:**
- Modify: `apps/editor/src/editor_shared_viewport_runtime.hpp`
- Modify: `apps/editor/src/native_bridge/viewport_native_smoke.cpp`

**Interfaces:**
- Consumes: existing `editor_viewport_acquire_present_packet` smoke path.
- Produces: additive `editor_viewport_query_runtime_stats(EditorViewportNativeRuntimeStats*)` C ABI backed by `EditorSharedViewportRuntime::stats()`.

- [x] **Step 1: Write the failing smoke expectation**

Add a runtime stats read after the first successful packet and assert:

```cpp
EditorViewportNativeRuntimeStats statsAfterFirstPacket{};
const std::uint32_t statsStatus =
    editor_viewport_query_runtime_stats(&statsAfterFirstPacket);
if (statsStatus != EditorViewportNativeStatus_Success ||
    statsAfterFirstPacket.framesRendered != 1U ||
    statsAfterFirstPacket.producersCreated != 1U ||
    statsAfterFirstPacket.packetsCreated != 1U) {
    logError("Viewport native bridge smoke did not expose first render producer stats.");
    return false;
}
```

- [x] **Step 2: Run the smoke build to verify RED**

Run:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug --target asharia-editor"
```

Expected: compile failure because `EditorViewportNativeRuntimeStats` or `editor_viewport_query_runtime_stats()` is not defined.

### Task 2: Extract Native Render Producer

**Files:**
- Create: `apps/editor/src/editor_shared_viewport_render_producer.hpp`
- Create: `apps/editor/src/editor_shared_viewport_render_producer.cpp`
- Modify: `apps/editor/src/editor_shared_viewport_runtime.hpp`
- Modify: `apps/editor/src/editor_shared_viewport_runtime.cpp`
- Modify: `apps/editor/src/native_bridge/viewport_native_api.hpp`
- Modify: `apps/editor/src/native_bridge/viewport_native_api.cpp`
- Modify: `apps/editor/CMakeLists.txt`

**Interfaces:**
- Consumes: `VulkanContext`, `EditorSharedViewportPresentDesc`, frame index.
- Produces: `EditorSharedViewportRenderProducer::renderSceneViewFrame(...)`, `EditorSharedViewportRenderProducerStats`, `std::unique_ptr<EditorSharedViewportPacketState>`.

- [x] **Step 1: Move packet/render production below a producer boundary**

Create `EditorSharedViewportRenderProducer` and `EditorSharedViewportPacketState`. Keep the current per-packet command pool, command buffer, fence, external image, and semaphore behavior inside the producer implementation.

- [x] **Step 2: Keep runtime ownership narrow**

Update `EditorSharedViewportRuntime` so it owns:

```cpp
std::optional<asharia::VulkanContext> context_;
std::optional<EditorSharedViewportRenderProducer> renderProducer_;
std::unordered_set<void*> outstandingPackets_;
```

Runtime creates the producer once per context lifetime and resets it during shutdown after outstanding packets drain.

- [x] **Step 3: Implement stats**

Expose internal runtime stats and map them to an additive C ABI:

```cpp
struct EditorSharedViewportRuntimeStats {
    std::uint64_t framesRendered{};
    std::uint64_t producersCreated{};
    std::uint64_t packetsCreated{};
    std::size_t outstandingPackets{};
    bool hasContext{};
    bool hasRenderProducer{};
    bool shutdownRequested{};
};

struct EditorViewportNativeRuntimeStats {
    EditorViewportNativeAbiHeader header;
    std::uint64_t framesRendered;
    std::uint64_t producersCreated;
    std::uint64_t packetsCreated;
    std::uint64_t outstandingPackets;
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
};
```

### Task 3: Verify Native and Studio Surface

**Files:**
- Test: `apps/editor/src/native_bridge/viewport_native_smoke.cpp`
- Test: `apps/studio/Tests/Editor.Tests/Features/SceneView/SceneViewCompositionPresenterSourceTests.cs`

**Interfaces:**
- Consumes: existing `editor_viewport_acquire_present_packet` ABI.
- Produces: no ABI changes.

- [x] **Step 1: Run GREEN native verification**

Run:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug --target asharia-editor"
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

Expected: build succeeds and smoke exits 0.

- [x] **Step 2: Run managed viewport verification**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "SceneView|ViewportNative|Composition"
```

Expected: selected tests pass.

- [x] **Step 3: Run cheap gates**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Expected: no encoding or whitespace violations.
