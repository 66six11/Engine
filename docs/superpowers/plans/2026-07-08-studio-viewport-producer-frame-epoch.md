# Studio Viewport Producer Frame Epoch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add producer-local submitted/completed frame epoch tracking to the Studio native viewport producer so later persistent renderer resources can retire after packet GPU completion without adopting `VulkanFrameLoop`.

**Architecture:** `EditorSharedViewportRenderProducer` owns a private frame epoch tracker. Each successfully submitted present packet receives a move-only epoch lease after `vkQueueSubmit2` succeeds. Packet destruction continues to wait the per-packet fence, close exported OS handles, destroy per-packet Vulkan command resources, and release the external image lease; it then marks the epoch complete through shared tracker state. Runtime stats copy producer epoch counters, and a new v3 native diagnostics ABI exposes the additive fields without changing v1 or v2 struct layouts.

**Tech Stack:** C++23, Vulkan sync2 submit path, per-packet Vulkan fences, existing editor native C ABI, native viewport smoke test, architecture documentation.

## Global Constraints

- Implement issue #219 only: producer-local frame epoch tracking for Studio shared viewport packets.
- Do not reuse `BasicFullscreenTextureRenderer` in this slice. Persistent renderer reuse belongs to the follow-up slice after epoch diagnostics are proven.
- Do not pool semaphores, command pools, command buffers, or fences in this slice.
- Do not introduce `VulkanFrameLoop` ownership or public `rhi_vulkan` API changes.
- Keep the implementation private to `apps/editor`.
- Do not change managed Studio/Avalonia behavior or the present packet ABI fields used for composition interop.
- Keep `EditorViewportNativeRuntimeStats` v1 unchanged.
- Keep `EditorViewportNativeRuntimeStatsV2` unchanged because callers compile against its current field order and size.
- Add `EditorViewportNativeRuntimeStatsV3` and `editor_viewport_query_runtime_stats_v3` for epoch diagnostics.
- Completion epoch means the native packet destructor has observed the packet fence and marked GPU submission completion. It does not mean Avalonia released every older packet. `outstandingPackets` remains the compositor lifetime signal.
- Packet releases may be observed out of presentation order. Since submissions use the same graphics queue, observing a later fence implies earlier GPU submissions completed, but older packet handles may still be outstanding.
- No `vkDeviceWaitIdle` may be added to render loops.
- C/C++ source and header files must be UTF-8 with BOM. Markdown and CMake files must be UTF-8 without BOM.

---

## File Structure

- Create `apps/editor/src/editor_shared_viewport_frame_epoch.hpp`
  - Private epoch stats, move-only epoch lease, and tracker class.
- Create `apps/editor/src/editor_shared_viewport_frame_epoch.cpp`
  - Shared state, submit accounting, completion accounting, and snapshot logic.
- Modify `apps/editor/src/editor_shared_viewport_render_producer.hpp`
  - Include the epoch helper.
  - Add epoch counters to producer stats.
  - Add `EditorSharedViewportFrameEpochLease` to `EditorSharedViewportPacketState`.
  - Add producer-owned `EditorSharedViewportFrameEpochTracker`.
- Modify `apps/editor/src/editor_shared_viewport_render_producer.cpp`
  - Assign an epoch only after a successful queue submit.
  - Mark the epoch complete after the packet fence wait in packet destruction.
  - Merge epoch stats into producer stats.
- Modify `apps/editor/src/editor_shared_viewport_runtime.hpp`
  - Add epoch counters to internal runtime stats.
- Modify `apps/editor/src/editor_shared_viewport_runtime.cpp`
  - Copy producer epoch counters into runtime stats.
- Modify `apps/editor/src/native_bridge/viewport_native_api.hpp`
  - Add `EditorViewportNativeRuntimeStatsV3`.
  - Add `editor_viewport_query_runtime_stats_v3`.
- Modify `apps/editor/src/native_bridge/viewport_native_api.cpp`
  - Fill v3 stats from runtime stats.
  - Leave v1 and v2 query behavior unchanged.
- Modify `apps/editor/src/native_bridge/viewport_native_smoke.cpp`
  - Query v3 stats.
  - Assert submitted/completed/pending epoch progress before and after packet release.
- Modify `apps/editor/CMakeLists.txt`
  - Add `editor_shared_viewport_frame_epoch.cpp` to `editor-native`.
- Modify `docs/architecture/flow.md`
  - Document the producer-local frame epoch and its relationship to packet lifetime and external image pool lifetime.

## Interfaces

Add this private editor-native helper. The tracker uses shared state so packet destruction can complete an epoch even if ownership is moving through runtime release paths. The runtime already prevents producer/context teardown while packet release is in progress; shared state still keeps the completion accounting independent from the producer object address.

```cpp
namespace asharia::editor {

    struct EditorSharedViewportFrameEpochStats {
        std::uint64_t submitted{};
        std::uint64_t completed{};
        std::uint64_t pending{};
    };

    struct EditorSharedViewportFrameEpochState;

    class EditorSharedViewportFrameEpochTracker;

    class EditorSharedViewportFrameEpochLease final {
    public:
        EditorSharedViewportFrameEpochLease() = default;
        EditorSharedViewportFrameEpochLease(
            const EditorSharedViewportFrameEpochLease&) = delete;
        EditorSharedViewportFrameEpochLease&
        operator=(const EditorSharedViewportFrameEpochLease&) = delete;
        EditorSharedViewportFrameEpochLease(
            EditorSharedViewportFrameEpochLease&& other) noexcept;
        EditorSharedViewportFrameEpochLease&
        operator=(EditorSharedViewportFrameEpochLease&& other) noexcept;
        ~EditorSharedViewportFrameEpochLease();

        [[nodiscard]] bool hasEpoch() const noexcept;
        [[nodiscard]] std::uint64_t epoch() const noexcept;
        void complete() noexcept;

    private:
        friend class EditorSharedViewportFrameEpochTracker;

        EditorSharedViewportFrameEpochLease(
            std::shared_ptr<EditorSharedViewportFrameEpochState> state,
            std::uint64_t epoch) noexcept;

        std::shared_ptr<EditorSharedViewportFrameEpochState> state_;
        std::uint64_t epoch_{};
        bool completed_{false};
    };

    class EditorSharedViewportFrameEpochTracker final {
    public:
        EditorSharedViewportFrameEpochTracker();
        EditorSharedViewportFrameEpochTracker(
            const EditorSharedViewportFrameEpochTracker&) = delete;
        EditorSharedViewportFrameEpochTracker&
        operator=(const EditorSharedViewportFrameEpochTracker&) = delete;
        EditorSharedViewportFrameEpochTracker(
            EditorSharedViewportFrameEpochTracker&&) noexcept = default;
        EditorSharedViewportFrameEpochTracker&
        operator=(EditorSharedViewportFrameEpochTracker&&) noexcept = default;
        ~EditorSharedViewportFrameEpochTracker() = default;

        [[nodiscard]] EditorSharedViewportFrameEpochLease submit();
        [[nodiscard]] EditorSharedViewportFrameEpochStats stats() const;

    private:
        std::shared_ptr<EditorSharedViewportFrameEpochState> state_;
    };

} // namespace asharia::editor
```

The implementation must be deterministic and monotonic:

```cpp
struct EditorSharedViewportFrameEpochState {
    mutable std::mutex mutex;
    std::uint64_t submitted{};
    std::uint64_t completed{};
};

EditorSharedViewportFrameEpochLease EditorSharedViewportFrameEpochTracker::submit() {
    std::lock_guard lock{state_->mutex};
    const std::uint64_t epoch = ++state_->submitted;
    return EditorSharedViewportFrameEpochLease{state_, epoch};
}

void EditorSharedViewportFrameEpochLease::complete() noexcept {
    if (!state_ || epoch_ == 0U || completed_) {
        return;
    }

    {
        std::lock_guard lock{state_->mutex};
        state_->completed = std::max(state_->completed, epoch_);
    }

    completed_ = true;
}
```

`pending` is derived as `submitted - completed` in `stats()`, clamped defensively if future code ever changes completion semantics.

## Task 1: Add Epoch Helper

- [ ] Create `apps/editor/src/editor_shared_viewport_frame_epoch.hpp`.
- [ ] Include:
  - `<cstdint>`
  - `<memory>`
- [ ] Declare `EditorSharedViewportFrameEpochStats`, `EditorSharedViewportFrameEpochState`, `EditorSharedViewportFrameEpochLease`, and `EditorSharedViewportFrameEpochTracker` exactly in namespace `asharia::editor`.
- [ ] Create `apps/editor/src/editor_shared_viewport_frame_epoch.cpp`.
- [ ] Include:
  - `"editor_shared_viewport_frame_epoch.hpp"`
  - `<algorithm>`
  - `<mutex>`
  - `<utility>`
- [ ] Implement move construction and move assignment with the existing lease pattern from `editor_shared_viewport_external_image_pool.cpp`: complete the current lease before replacing it, then move state, epoch, and completion flag.
- [ ] Implement lease destructor as a completion fallback by calling `complete()`.
- [ ] Implement `hasEpoch()` as `return static_cast<bool>(state_) && epoch_ != 0U;`.
- [ ] Implement `epoch()` as a no-throw getter returning `epoch_`.
- [ ] Implement `submit()` under the shared mutex and return a lease for the incremented epoch.
- [ ] Implement `stats()` under the shared mutex:

```cpp
EditorSharedViewportFrameEpochStats EditorSharedViewportFrameEpochTracker::stats() const {
    std::lock_guard lock{state_->mutex};
    const std::uint64_t completed = std::min(state_->completed, state_->submitted);
    return EditorSharedViewportFrameEpochStats{
        .submitted = state_->submitted,
        .completed = completed,
        .pending = state_->submitted - completed,
    };
}
```

- [ ] Add `apps/editor/src/editor_shared_viewport_frame_epoch.cpp` to the `editor-native` source list in `apps/editor/CMakeLists.txt`.

## Task 2: Integrate Producer Epochs

- [ ] Modify `apps/editor/src/editor_shared_viewport_render_producer.hpp`.
- [ ] Add:

```cpp
#include "editor_shared_viewport_frame_epoch.hpp"
```

- [ ] Extend `EditorSharedViewportRenderProducerStats`:

```cpp
std::uint64_t frameEpochsSubmitted{};
std::uint64_t frameEpochsCompleted{};
std::uint64_t frameEpochsPending{};
```

- [ ] Add the packet field after `submitted` or before `renderer`:

```cpp
EditorSharedViewportFrameEpochLease frameEpoch;
```

- [ ] Add the producer member near `externalImagePool_`:

```cpp
EditorSharedViewportFrameEpochTracker frameEpochTracker_;
```

- [ ] Modify `apps/editor/src/editor_shared_viewport_render_producer.cpp`.
- [ ] Change the internal `recordSharedViewportFrame` helper signature to accept the tracker:

```cpp
EditorSharedViewportFrameEpochTracker& frameEpochTracker,
EditorSharedViewportExternalImagePool& externalImagePool,
```

- [ ] Keep all existing image lease, semaphore, command buffer, fence, renderer, and handle export behavior unchanged.
- [ ] After `vkQueueSubmit2` returns success and before returning the packet, assign the epoch:

```cpp
state.frameEpoch = frameEpochTracker.submit();
state.submitted = true;
```

- [ ] Do not submit an epoch when `vkQueueSubmit2` fails.
- [ ] In `EditorSharedViewportPacketState::~EditorSharedViewportPacketState()`, call `frameEpoch.complete()` only after the submitted fence wait path has run:

```cpp
if (submitted && device != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) {
    const VkResult waitResult =
        vkWaitForFences(device, 1U, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    (void)waitResult;
    frameEpoch.complete();
}
```

- [ ] If the current destructor already waits the fence with equivalent code, add only `frameEpoch.complete()` after that wait. Do not otherwise reorder handle closing, command pool destruction, renderer destruction, or image lease release.
- [ ] Pass `frameEpochTracker_` from `EditorSharedViewportRenderProducer::renderSceneViewFrame` into `recordSharedViewportFrame`.
- [ ] Extend `EditorSharedViewportRenderProducer::stats()`:

```cpp
const EditorSharedViewportFrameEpochStats epochStats = frameEpochTracker_.stats();
snapshot.frameEpochsSubmitted = epochStats.submitted;
snapshot.frameEpochsCompleted = epochStats.completed;
snapshot.frameEpochsPending = epochStats.pending;
```

- [ ] Keep pool stats aggregation unchanged.

## Task 3: Surface Runtime And ABI Stats

- [ ] Modify `apps/editor/src/editor_shared_viewport_runtime.hpp`.
- [ ] Add the same three epoch counters to `EditorSharedViewportRuntimeStats`:

```cpp
std::uint64_t frameEpochsSubmitted{};
std::uint64_t frameEpochsCompleted{};
std::uint64_t frameEpochsPending{};
```

- [ ] Modify `apps/editor/src/editor_shared_viewport_runtime.cpp`.
- [ ] Copy producer stats into runtime stats in the aggregate return object:

```cpp
.frameEpochsSubmitted = producerStats.frameEpochsSubmitted,
.frameEpochsCompleted = producerStats.frameEpochsCompleted,
.frameEpochsPending = producerStats.frameEpochsPending,
```

- [ ] Preserve existing `outstandingPackets`, `hasContext`, `hasRenderProducer`, and `shutdownRequested` semantics.
- [ ] Modify `apps/editor/src/native_bridge/viewport_native_api.hpp`.
- [ ] Add `EditorViewportNativeRuntimeStatsV3` after v2. It must copy v2 fields and append epoch counters before the trailing booleans, or copy v2 fields and append epoch counters after the trailing booleans. Use one order consistently in header, implementation, and smoke. Preferred order:

```cpp
struct EditorViewportNativeRuntimeStatsV3 {
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
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
};
```

- [ ] Add the exported function declaration:

```cpp
EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v3(EditorViewportNativeRuntimeStatsV3* stats);
```

- [ ] Modify `apps/editor/src/native_bridge/viewport_native_api.cpp`.
- [ ] Add a v3 header helper only if the file already uses version-specific helpers for stats headers. Otherwise set the header inline in the new function exactly as v1/v2 do.
- [ ] Implement `editor_viewport_query_runtime_stats_v3` with the same null handling and status convention as v2.
- [ ] Fill all v2-compatible fields from `EditorSharedViewportRuntimeStats`.
- [ ] Fill epoch fields from `runtimeStats.frameEpochsSubmitted`, `runtimeStats.frameEpochsCompleted`, and `runtimeStats.frameEpochsPending`.
- [ ] Do not change `editor_viewport_query_runtime_stats` or `editor_viewport_query_runtime_stats_v2`.

## Task 4: Extend Native Smoke Coverage

- [ ] Modify `apps/editor/src/native_bridge/viewport_native_smoke.cpp`.
- [ ] Add a `queryRuntimeStatsV3()` helper that mirrors the current v2 helper.
- [ ] Keep existing v1/v2 assertions.
- [ ] After the first successful packet acquisition and before release, assert:

```cpp
stats.frameEpochsSubmitted == 1U
stats.frameEpochsCompleted == 0U
stats.frameEpochsPending == 1U
stats.outstandingPackets == 1U
```

- [ ] After releasing the first packet, assert:

```cpp
stats.frameEpochsSubmitted == 1U
stats.frameEpochsCompleted == 1U
stats.frameEpochsPending == 0U
stats.outstandingPackets == 0U
```

- [ ] After the second same-size packet is released, assert:

```cpp
stats.frameEpochsSubmitted == 2U
stats.frameEpochsCompleted == 2U
stats.frameEpochsPending == 0U
```

- [ ] After the resized packet is released, assert:

```cpp
stats.frameEpochsSubmitted == 3U
stats.frameEpochsCompleted == 3U
stats.frameEpochsPending == 0U
```

- [ ] In the existing shutdown-pending-packet section, query v3 after acquiring the pending packet and before `editor_viewport_shutdown()`:

```cpp
stats.frameEpochsSubmitted == 4U
stats.frameEpochsCompleted == 3U
stats.frameEpochsPending == 1U
stats.outstandingPackets == 1U
```

- [ ] Keep the existing post-shutdown acquire rejection assertion. Do not require epoch stats after shutdown because the current runtime stats intentionally report no producer after shutdown drains context state.

## Task 5: Update Architecture Docs

- [ ] Modify `docs/architecture/flow.md`.
- [ ] In the Studio Avalonia scene view composition section, add that the shared viewport runtime owns outstanding packet tracking and shutdown drain, while the render producer owns:
  - per-packet render submission
  - per-packet command/semaphore/fence resources
  - producer-local shared external image pool
  - producer-local submitted/completed frame epoch tracker
- [ ] State that the frame epoch tracker is independent from `VulkanFrameLoop`.
- [ ] State that epoch completion is driven by packet release observing the packet fence.
- [ ] State that `outstandingPackets` remains the authoritative count for managed compositor packet ownership.
- [ ] Mention that v3 native runtime stats expose epoch diagnostics and v1/v2 stats remain unchanged.

## Task 6: Validation

- [ ] Fix source encoding if new C++ files were created without BOM:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1 -Fix
```

- [ ] Run encoding check:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
```

- [ ] Run whitespace check:

```powershell
git diff --check
```

- [ ] Build clang-cl debug:

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
```

- [ ] Build MSVC debug:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

- [ ] Run the native viewport smoke executable from the built output path used by `docs/workflow/review.md`. If the exact executable path differs between presets, use the path produced by the successful build log. The smoke must include the v3 epoch assertions above.
- [ ] If frame-loop, swapchain, or render-graph code changes unexpectedly during execution, run the full current smoke list from `docs/workflow/review.md` on both MSVC and clang-cl builds. This plan should not require those broader changes.

## Done Evidence For Issue #219

When implementation and validation are complete, comment on #219 with:

- Branch name.
- Files changed.
- Statement that v1/v2 stats ABI remained unchanged and v3 was added.
- Native smoke evidence covering submitted/completed/pending epoch counters.
- Encoding, diff, clang-cl build, and MSVC build results.
- Any validation that could not run, with the exact reason and the command to refresh it.

Use `Closes #219` only in the PR body if all acceptance criteria are satisfied and the Done evidence above is posted. Use `Refs #219` if any validation remains blocked.
