# Studio Shared Viewport Packet Backpressure Guard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an explicit one-outstanding-packet backpressure guard to the Studio native shared viewport path and expose additive diagnostics proving rejected acquire attempts do not consume renderer or packet resources.

**Architecture:** `EditorSharedViewportRuntime` remains the authority for outstanding native present packets, so the guard belongs there, before context/producer acquisition and before `nextFrameIndex_` advances. The C ABI keeps existing v1-v4 stats unchanged, returns `EditorViewportNativeStatus_Unavailable` plus a specific message for the busy/backpressure acquire path, and adds `EditorViewportNativeRuntimeStatsV5` for `maxOutstandingPackets` and `packetBackpressureHits`.

**Tech Stack:** C++23, Vulkan 1.4 via project RHI wrappers, C ABI in `editor_native.dll`, `asharia-editor --smoke-editor-viewport-native`, PowerShell/CMake/Ninja/Conan gates, Avalonia/.NET Studio smoke only if managed status code changes.

## Global Constraints

- C/C++ sources (`.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hh`, `.hpp`, `.hxx`, `.ipp`, `.inl`) must use UTF-8 with BOM.
- Everything else (`.json`, `.cmake`, `.py`, `.md`, `.slang`, `.ps1`, `.clang-format`, `.clang-tidy`, etc.) must use UTF-8 without BOM.
- `asharia::rhi_vulkan` must not depend on RenderGraph.
- `asharia::renderer_basic` must remain backend-agnostic; Vulkan command recording stays in `asharia::renderer_basic_vulkan`.
- Packages must not include another package's `src/`; only `include/` is public API.
- No `vkDeviceWaitIdle` in render loops.
- `VkResult` must never be ignored.
- #223 scope excludes semaphore, command pool, command buffer, fence pooling, multi-viewport frame pacing, `VulkanFrameLoop` adoption, and managed Studio UI redesign.
- Use TDD: write the native smoke expectations first and observe a failing build or failing smoke before production implementation.

---

## File Structure

- Modify `apps/editor/src/native_bridge/viewport_native_api.hpp`: append `EditorViewportNativeRuntimeStatsV5` and `editor_viewport_query_runtime_stats_v5` without changing v1-v4 layouts.
- Modify `apps/editor/src/native_bridge/viewport_native_api.cpp`: add v5 header/result writer and map typed runtime backpressure errors to `EditorViewportNativeStatus_Unavailable`.
- Modify `apps/editor/src/native_bridge/viewport_native_smoke.cpp`: RED coverage for acquire while a packet is outstanding, then v5 counter assertions.
- Modify `apps/editor/src/editor_shared_viewport_runtime.hpp`: add typed render error, v5 stats fields, max outstanding constant, and changed `renderSceneViewFrame` expected error type.
- Modify `apps/editor/src/editor_shared_viewport_runtime.cpp`: enforce one outstanding packet, increment backpressure counter, keep frame/resource counters unchanged on rejection.
- Modify `docs/architecture/flow.md`: document the single-viewport backpressure contract and v5 diagnostics.

---

### Task 1: RED Native Smoke For Backpressure And V5 Diagnostics

**Files:**
- Modify: `apps/editor/src/native_bridge/viewport_native_api.hpp`
- Modify: `apps/editor/src/native_bridge/viewport_native_smoke.cpp`

**Interfaces:**
- Produces: `EditorViewportNativeRuntimeStatsV5` with fields copied from v4 plus `maxOutstandingPackets` and `packetBackpressureHits`.
- Produces: `editor_viewport_query_runtime_stats_v5(EditorViewportNativeRuntimeStatsV5* stats)`.
- Consumes later: runtime v5 stats and acquire rejection behavior.

- [ ] **Step 1: Add the v5 ABI declaration**

In `apps/editor/src/native_bridge/viewport_native_api.hpp`, insert this struct after `EditorViewportNativeRuntimeStatsV4` and declare the query function after `editor_viewport_query_runtime_stats_v4`:

```cpp
struct EditorViewportNativeRuntimeStatsV5 {
    EditorViewportNativeAbiHeader header;
    std::uint64_t framesRendered;
    std::uint64_t producersCreated;
    std::uint64_t packetsCreated;
    std::uint64_t outstandingPackets;
    std::uint64_t externalImagesAcquired;
    std::uint64_t externalImagesCreated;
    std::uint64_t externalImagesReused;
    std::uint64_t externalImagesReleased;
    std::uint64_t externalImagesAvailable;
    std::uint64_t externalImagesLeased;
    std::uint64_t frameEpochsSubmitted;
    std::uint64_t frameEpochsCompleted;
    std::uint64_t frameEpochsPending;
    std::uint64_t rendererCreations;
    std::uint64_t maxOutstandingPackets;
    std::uint64_t packetBackpressureHits;
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
};

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v5(EditorViewportNativeRuntimeStatsV5* stats);
```

- [ ] **Step 2: Add native smoke helper**

In `apps/editor/src/native_bridge/viewport_native_smoke.cpp`, add this helper after `queryRuntimeStatsV4`:

```cpp
[[nodiscard]] bool queryRuntimeStatsV5(EditorViewportNativeRuntimeStatsV5& stats) {
    const std::uint32_t status = editor_viewport_query_runtime_stats_v5(&stats);
    return status == EditorViewportNativeStatus_Success &&
           stats.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
           stats.header.structSize == sizeof(EditorViewportNativeRuntimeStatsV5);
}
```

- [ ] **Step 3: Add RED behavior before releasing the first packet**

In `runViewportNativeBridgeSmoke()`, immediately after the existing v4 first-packet assertion and before `releaseIfNeeded(packet);`, insert:

```cpp
EditorViewportNativePresentPacket backpressuredPacket{};
const std::uint32_t backpressuredStatus =
    editor_viewport_acquire_present_packet(&firstPresentRequest, &backpressuredPacket);
const bool acquireRejectedWhilePending =
    backpressuredStatus == EditorViewportNativeStatus_Unavailable &&
    backpressuredPacket.status == EditorViewportNativeStatus_Unavailable &&
    backpressuredPacket.nativePacket == nullptr &&
    backpressuredPacket.imageHandle == nullptr &&
    backpressuredPacket.waitSemaphoreHandle == nullptr &&
    backpressuredPacket.signalSemaphoreHandle == nullptr;
if (!acquireRejectedWhilePending) {
    releaseIfNeeded(backpressuredPacket);
    releaseIfNeeded(packet);
    logError("Viewport native bridge smoke allowed acquire while a present packet was still pending.");
    return false;
}

EditorViewportNativeRuntimeStatsV5 statsV5AfterBackpressure{};
if (!queryRuntimeStatsV5(statsV5AfterBackpressure) ||
    statsV5AfterBackpressure.framesRendered != 1U ||
    statsV5AfterBackpressure.packetsCreated != 1U ||
    statsV5AfterBackpressure.outstandingPackets != 1U ||
    statsV5AfterBackpressure.rendererCreations != 1U ||
    statsV5AfterBackpressure.maxOutstandingPackets != 1U ||
    statsV5AfterBackpressure.packetBackpressureHits != 1U ||
    statsV5AfterBackpressure.frameEpochsSubmitted != 1U ||
    statsV5AfterBackpressure.frameEpochsCompleted != 0U ||
    statsV5AfterBackpressure.frameEpochsPending != 1U) {
    releaseIfNeeded(backpressuredPacket);
    releaseIfNeeded(packet);
    logError("Viewport native bridge smoke did not expose v5 backpressure stats.");
    return false;
}
releaseIfNeeded(backpressuredPacket);
```

- [ ] **Step 4: Extend post-release v5 checks**

After the existing `statsV4AfterFirstRelease` block, add:

```cpp
EditorViewportNativeRuntimeStatsV5 statsV5AfterFirstRelease{};
if (!queryRuntimeStatsV5(statsV5AfterFirstRelease) ||
    statsV5AfterFirstRelease.rendererCreations != 1U ||
    statsV5AfterFirstRelease.packetsCreated != 1U ||
    statsV5AfterFirstRelease.outstandingPackets != 0U ||
    statsV5AfterFirstRelease.maxOutstandingPackets != 1U ||
    statsV5AfterFirstRelease.packetBackpressureHits != 1U ||
    statsV5AfterFirstRelease.frameEpochsSubmitted != 1U ||
    statsV5AfterFirstRelease.frameEpochsCompleted != 1U ||
    statsV5AfterFirstRelease.frameEpochsPending != 0U) {
    logError("Viewport native bridge smoke did not preserve v5 stats after first release.");
    return false;
}
```

- [ ] **Step 5: Run RED verification**

Run:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-editor"
```

Expected: FAIL before implementation, either with unresolved `editor_viewport_query_runtime_stats_v5` or, after adding a stub, with smoke failure because a second acquire currently succeeds.

---

### Task 2: Runtime Guard And Typed Backpressure Error

**Files:**
- Modify: `apps/editor/src/editor_shared_viewport_runtime.hpp`
- Modify: `apps/editor/src/editor_shared_viewport_runtime.cpp`
- Modify: `apps/editor/src/native_bridge/viewport_native_api.cpp`

**Interfaces:**
- Consumes: Task 1 smoke expectations.
- Produces: `EditorSharedViewportRenderFrameResult`, where `Backpressure` maps to C ABI `Unavailable`.
- Produces: runtime stats fields `maxOutstandingPackets` and `packetBackpressureHits`.

- [ ] **Step 1: Add runtime typed error and v5 stats fields**

In `apps/editor/src/editor_shared_viewport_runtime.hpp`, add `<expected>` and `asharia/core/error.hpp`, then add:

```cpp
enum class EditorSharedViewportRenderFrameErrorKind {
    Failed,
    Backpressure,
};

struct EditorSharedViewportRenderFrameError {
    EditorSharedViewportRenderFrameErrorKind kind{EditorSharedViewportRenderFrameErrorKind::Failed};
    asharia::Error error;
};

using EditorSharedViewportRenderFrameResult =
    std::expected<EditorSharedViewportPresentPacket, EditorSharedViewportRenderFrameError>;
```

Extend `EditorSharedViewportRuntimeStats`:

```cpp
std::uint64_t packetBackpressureHits{};
std::size_t maxOutstandingPackets{};
```

Change `renderSceneViewFrame` declaration:

```cpp
[[nodiscard]] EditorSharedViewportRenderFrameResult
renderSceneViewFrame(EditorSharedViewportPresentDesc desc);
```

Add a private constant:

```cpp
static constexpr std::size_t kMaxOutstandingPackets = 1U;
```

- [ ] **Step 2: Add runtime error helpers**

In `apps/editor/src/editor_shared_viewport_runtime.cpp`, add local helpers in the unnamed namespace:

```cpp
[[nodiscard]] std::unexpected<EditorSharedViewportRenderFrameError>
renderFrameFailure(asharia::Error error) {
    return std::unexpected{EditorSharedViewportRenderFrameError{
        .kind = EditorSharedViewportRenderFrameErrorKind::Failed,
        .error = std::move(error),
    }};
}

[[nodiscard]] std::unexpected<EditorSharedViewportRenderFrameError>
renderFrameBackpressure() {
    return std::unexpected{EditorSharedViewportRenderFrameError{
        .kind = EditorSharedViewportRenderFrameErrorKind::Backpressure,
        .error = vulkanError(
            "Shared viewport present packet is still pending; release it before acquiring another packet"),
    }};
}
```

- [ ] **Step 3: Enforce one outstanding packet**

In `EditorSharedViewportRuntime::renderSceneViewFrame`, convert existing `std::unexpected{...}` returns to `renderFrameFailure(...)` and insert this check under `mutex_` after `shutdownRequested_` and before `ensureSharedContextStorage(context_)`:

```cpp
if (outstandingPackets_.size() >= kMaxOutstandingPackets) {
    ++packetBackpressureHits_;
    return renderFrameBackpressure();
}
```

The guard must run before context creation, producer creation, frame index increment, external image acquire, renderer recording, queue submit, and packet insertion.

- [ ] **Step 4: Expose runtime stats**

In `EditorSharedViewportRuntime::stats()`, set:

```cpp
.packetBackpressureHits = packetBackpressureHits_,
.maxOutstandingPackets = kMaxOutstandingPackets,
```

- [ ] **Step 5: Map backpressure in native API**

In `apps/editor/src/native_bridge/viewport_native_api.cpp`, update the failed `present` branch in `editor_viewport_acquire_present_packet`:

```cpp
if (!present) {
    const std::uint32_t status =
        present.error().kind ==
                asharia::editor::EditorSharedViewportRenderFrameErrorKind::Backpressure
            ? EditorViewportNativeStatus_Unavailable
            : EditorViewportNativeStatus_RenderFailed;
    return writePresentPacketFailure(packet, status, present.error().error.message);
}
```

- [ ] **Step 6: Verify GREEN for native smoke**

Run:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug --target asharia-editor"
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

Expected: build passes and the native viewport smoke passes.

---

### Task 3: Add C ABI V5 Stats Implementation

**Files:**
- Modify: `apps/editor/src/native_bridge/viewport_native_api.cpp`

**Interfaces:**
- Consumes: runtime stats fields from Task 2.
- Produces: `editor_viewport_query_runtime_stats_v5`.

- [ ] **Step 1: Add v5 header helper**

After `runtimeStatsV4Header()`, add:

```cpp
[[nodiscard]] EditorViewportNativeAbiHeader runtimeStatsV5Header() {
    return EditorViewportNativeAbiHeader{
        .abiVersion = EDITOR_NATIVE_ABI_VERSION,
        .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeRuntimeStatsV5)),
    };
}
```

- [ ] **Step 2: Add v5 query implementation**

After `editor_viewport_query_runtime_stats_v4`, add:

```cpp
std::uint32_t EDITOR_NATIVE_CALL editor_viewport_query_runtime_stats_v5(
    EditorViewportNativeRuntimeStatsV5* stats) {
    if (stats == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }

    const asharia::editor::EditorSharedViewportRuntimeStats runtimeStats =
        asharia::editor::EditorSharedViewportRuntime::instance().stats();
    *stats = EditorViewportNativeRuntimeStatsV5{
        .header = runtimeStatsV5Header(),
        .framesRendered = runtimeStats.framesRendered,
        .producersCreated = runtimeStats.producersCreated,
        .packetsCreated = runtimeStats.packetsCreated,
        .outstandingPackets = static_cast<std::uint64_t>(runtimeStats.outstandingPackets),
        .externalImagesAcquired = runtimeStats.externalImagesAcquired,
        .externalImagesCreated = runtimeStats.externalImagesCreated,
        .externalImagesReused = runtimeStats.externalImagesReused,
        .externalImagesReleased = runtimeStats.externalImagesReleased,
        .externalImagesAvailable = runtimeStats.externalImagesAvailable,
        .externalImagesLeased = runtimeStats.externalImagesLeased,
        .frameEpochsSubmitted = runtimeStats.frameEpochsSubmitted,
        .frameEpochsCompleted = runtimeStats.frameEpochsCompleted,
        .frameEpochsPending = runtimeStats.frameEpochsPending,
        .rendererCreations = runtimeStats.rendererCreations,
        .maxOutstandingPackets =
            static_cast<std::uint64_t>(runtimeStats.maxOutstandingPackets),
        .packetBackpressureHits = runtimeStats.packetBackpressureHits,
        .hasContext = runtimeStats.hasContext ? 1U : 0U,
        .hasRenderProducer = runtimeStats.hasRenderProducer ? 1U : 0U,
        .shutdownRequested = runtimeStats.shutdownRequested ? 1U : 0U,
    };
    return EditorViewportNativeStatus_Success;
}
```

- [ ] **Step 3: Run v5 ABI smoke**

Run:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-editor"
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

Expected: PASS, including v5 stats assertions from Task 1.

---

### Task 4: Documentation And Focused Regression Gates

**Files:**
- Modify: `docs/architecture/flow.md`

**Interfaces:**
- Consumes: runtime guard behavior and v5 diagnostics.
- Produces: documented contract for future Studio composition, descriptor/resource reuse, and pooling follow-ups.

- [ ] **Step 1: Update native viewport ownership bullets**

In `docs/architecture/flow.md`, in the Studio native viewport section near the `outstanding packet tracking` bullets, replace or extend the stats/backpressure bullets with:

```markdown
- The current Studio Scene View path is single-viewport and allows at most one
  outstanding native present packet. If a new acquire arrives before the
  previous packet is released, native returns `Unavailable` with a
  backpressure diagnostic instead of allocating an external image, submitting
  GPU work, or advancing persistent renderer descriptor/transient resource
  cursors.
- `editor_viewport_query_runtime_stats` is an additive native smoke /
  diagnostics ABI. v3 stats expose epoch diagnostics, v4 stats expose
  renderer creation reuse diagnostics, and v5 stats expose max outstanding
  packet and packet backpressure hit counters while v1-v4 layouts remain
  unchanged.
```

- [ ] **Step 2: Run focused regression gates**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1
git diff --check
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug --target asharia-editor"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug --target asharia-editor"
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-native-bridge
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-native-bridge
```

Expected: all commands PASS.

- [ ] **Step 3: Run C++ review helper**

Run:

```powershell
python C:\Users\C66\.codex\skills\vulkan-cpp23-engineering\scripts\review_vulkan_cpp.py apps\editor\src\native_bridge\viewport_native_api.hpp apps\editor\src\native_bridge\viewport_native_api.cpp apps\editor\src\native_bridge\viewport_native_smoke.cpp apps\editor\src\editor_shared_viewport_runtime.hpp apps\editor\src\editor_shared_viewport_runtime.cpp --fail-on warning
```

Expected: 0 errors, 0 warnings. If info-level direct `vulkan.h` loader strategy notes appear, record them as existing informational context if no actionable warning is reported.

---

## Self-Review

Spec coverage:
- Backpressure while previous packet is outstanding: Task 1 and Task 2.
- Rejected request does not allocate, submit, or advance counters: Task 1 v5 assertions check `framesRendered`, `packetsCreated`, `rendererCreations`, and epoch counters remain unchanged.
- Release clears guard and next acquire succeeds: existing second-packet path remains after first release, plus Task 1 post-release v5 check.
- Additive diagnostics ABI: Task 1 and Task 3 add v5 while v1-v4 stay unchanged.
- Docs: Task 4 updates `docs/architecture/flow.md`.

Placeholder scan:
- No TBD/TODO/fill-in placeholders are present.
- All modified interfaces and exact commands are specified.

Type consistency:
- `EditorViewportNativeRuntimeStatsV5`, `editor_viewport_query_runtime_stats_v5`, `packetBackpressureHits`, and `maxOutstandingPackets` names are consistent across tasks.
- Runtime typed error names are consistent across header, runtime implementation, and native bridge mapping.
