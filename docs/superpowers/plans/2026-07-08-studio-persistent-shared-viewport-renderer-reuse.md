# Studio Persistent Shared Viewport Renderer Reuse Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reuse one producer-owned `BasicFullscreenTextureRenderer` across Studio native shared viewport present packets so renderer resources are not recreated per packet while packet-owned submit resources, external image leases, and epoch accounting remain per packet.

**Architecture:** `EditorSharedViewportRuntime` continues to own Vulkan context lifetime, render producer lifetime, outstanding packet tracking, and shutdown drain. `EditorSharedViewportRenderProducer` creates a persistent `BasicFullscreenTextureRenderer` during its fallible `create()` factory and records each packet through that renderer. `EditorSharedViewportPacketState` continues to own each packet's external image lease, transient render graph images, command pool, command buffer, fence, exported image/semaphore OS handles, semaphores, and frame epoch lease. The persistent renderer rewinds descriptor/resource cursors only when the producer epoch tracker reports no pending packet. Runtime diagnostics copy producer renderer creation counts, and a new additive v4 native stats ABI exposes `rendererCreations` without changing v1, v2, or v3 struct layouts.

**Tech Stack:** C++23, Vulkan sync2 submit path, `renderer_basic_vulkan`, producer-local external image pool, producer-local frame epoch tracker, editor native C ABI, native viewport smoke, architecture documentation.

## Global Constraints

- Implement issue #221 only.
- Preserve #218 external image pool semantics and #219 frame epoch semantics.
- Preserve packet-owned transient render graph image lifetime; a transient image recorded into a packet command buffer must survive until that packet fence has completed.
- Do not pool semaphores, command pools, command buffers, fences, or exported OS handles.
- Do not adopt `VulkanFrameLoop` for this path.
- Do not change public `rhi_vulkan` API.
- Keep `asharia::rhi_vulkan` independent from RenderGraph.
- Keep Vulkan command recording in `apps/editor` host integration and `renderer_basic_vulkan`; do not move it into backend-neutral `renderer_basic`.
- Do not change managed Studio/Avalonia behavior.
- Keep `EditorViewportNativeRuntimeStats`, `EditorViewportNativeRuntimeStatsV2`, and `EditorViewportNativeRuntimeStatsV3` unchanged.
- Add `EditorViewportNativeRuntimeStatsV4` and `editor_viewport_query_runtime_stats_v4` for renderer reuse diagnostics.
- `rendererCreations` counts successful producer-owned renderer creation, not packet creation.
- Packet release remains the point where the packet fence is observed and the packet epoch lease completes.
- Producer teardown remains blocked by runtime shutdown drain until no packet is outstanding and no release operation is active.
- No `vkDeviceWaitIdle` may be added to render loops.
- C/C++ source and header files must be UTF-8 with BOM. Markdown and CMake files must be UTF-8 without BOM.

---

## File Structure

- Modify `apps/editor/src/editor_shared_viewport_render_producer.hpp`
  - Move renderer ownership from `EditorSharedViewportPacketState` to `EditorSharedViewportRenderProducer`.
  - Add packet-owned transient render graph image resources.
- Modify `apps/editor/src/editor_shared_viewport_render_producer.cpp`
  - Create the renderer in `EditorSharedViewportRenderProducer::create()`.
  - Record each packet with the producer-owned renderer.
  - Rewind renderer frame resource cursors only when no producer-local frame epoch is pending.
  - Increment `rendererCreations` once per producer.
- Modify `packages/renderer-basic/include/asharia/renderer_basic_vulkan/fullscreen_texture_renderer.hpp`
  - Add a `recordViewFrame()` overload that accepts caller-owned transient image resources.
  - Add a frame resource cursor reset entry point for hosts that prove previous packet GPU completion externally.
- Modify `packages/renderer-basic/src/basic_renderers/fullscreen_texture_renderer.inl`
  - Route the existing `recordViewFrame()` through the new overload using the renderer-owned transient resources.
  - Implement cursor reset for descriptor and debug line frame-local rings.
- Modify `apps/editor/src/editor_shared_viewport_runtime.hpp`
  - Add `rendererCreations` to internal runtime stats.
- Modify `apps/editor/src/editor_shared_viewport_runtime.cpp`
  - Copy producer renderer creation stats into runtime stats.
- Modify `apps/editor/src/native_bridge/viewport_native_api.hpp`
  - Add `EditorViewportNativeRuntimeStatsV4`.
  - Add `editor_viewport_query_runtime_stats_v4`.
- Modify `apps/editor/src/native_bridge/viewport_native_api.cpp`
  - Add a v4 stats header helper.
  - Fill v4 stats from runtime stats.
- Modify `apps/editor/src/native_bridge/viewport_native_smoke.cpp`
  - Add a v4 stats query helper.
  - Assert one renderer creation across first packet, same-size reuse, resize, and shutdown-pending packet flow.
- Modify `docs/architecture/flow.md`
  - Distinguish persistent producer-owned renderer lifetime from packet-owned submit resources.
  - Document v4 runtime stats.

## Test-First Step: Native Smoke RED

- [ ] Modify `apps/editor/src/native_bridge/viewport_native_smoke.cpp` first.
- [ ] Add a `queryRuntimeStatsV4()` helper that calls `editor_viewport_query_runtime_stats_v4`.
- [ ] Add smoke assertions that expect `rendererCreations == 1U` after:
  - first packet acquisition before release
  - first packet release
  - second same-size packet release
  - resized packet release
  - shutdown-pending packet acquisition before shutdown
- [ ] Keep the existing v1, v2, and v3 assertions.
- [ ] Verify RED with the narrowest available editor executable build command:

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --build --preset clangcl-debug --target asharia-editor"
```

- [ ] The expected RED result is compile failure because v4 stats are not declared yet.

## Task 1: Preserve Packet-Owned Transient Resources

- [ ] Modify `packages/renderer-basic/include/asharia/renderer_basic_vulkan/fullscreen_texture_renderer.hpp`.
- [ ] Add a `recordViewFrame()` overload that accepts caller-owned transient resources:

```cpp
[[nodiscard]] Result<VulkanFrameRecordResult>
recordViewFrame(const VulkanFrameRecordContext& frame, BasicRenderViewDesc view,
                VulkanTransientImagePool& transientImagePool,
                std::vector<VulkanTransientImageResource>& transientImages);
```

- [ ] Add:

```cpp
void resetFrameResourceCursors() noexcept;
```

- [ ] Modify `packages/renderer-basic/src/basic_renderers/fullscreen_texture_renderer.inl`.
- [ ] Make the existing two-argument `recordViewFrame()` call the new overload with `transientImagePool_` and `transientImages_`.
- [ ] Change the graph preparation call inside the overload to use the caller-provided pool and vector.
- [ ] Implement `resetFrameResourceCursors()` by resetting descriptor set epochs, descriptor set cursors, and debug line vertex buffer ring cursor state.
- [ ] Do not change existing callers outside the Studio shared viewport path.

## Task 2: Producer Owns Persistent Renderer

- [ ] Modify `apps/editor/src/editor_shared_viewport_render_producer.hpp`.
- [ ] Remove this packet field:

```cpp
BasicFullscreenTextureRenderer renderer;
```

- [ ] Add this producer member near `stats_`:

```cpp
BasicFullscreenTextureRenderer renderer_;
```

- [ ] Add packet-owned transient resources:

```cpp
VulkanTransientImagePool transientImagePool;
std::vector<VulkanTransientImageResource> transientImages;
```

- [ ] Keep packet fields for command resources, semaphores, fence, image lease, handles, `submitted`, `frameEpoch`, and `frameIndex` unchanged.
- [ ] Modify `apps/editor/src/editor_shared_viewport_render_producer.cpp`.
- [ ] Create the renderer in `EditorSharedViewportRenderProducer::create()` after device, allocator, queue, and queue family are copied:

```cpp
auto renderer = BasicFullscreenTextureRenderer::create(
    BasicFullscreenTextureRendererDesc{
        .device = producer.device_,
        .allocator = producer.allocator_,
        .shaderDirectory = ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR,
    });
if (!renderer) {
    return std::unexpected{std::move(renderer.error())};
}
producer.renderer_ = std::move(*renderer);
++producer.stats_.rendererCreations;
```

- [ ] Do not increment `rendererCreations` when renderer creation fails.
- [ ] Change the internal `recordSharedViewportFrame` helper signature to accept a renderer reference:

```cpp
BasicFullscreenTextureRenderer& renderer,
EditorSharedViewportFrameEpochTracker& frameEpochTracker,
EditorSharedViewportExternalImagePool& externalImagePool,
```

- [ ] Remove the per-packet `BasicFullscreenTextureRenderer::create()` call from `recordSharedViewportFrame`.
- [ ] Replace packet renderer recording with producer renderer recording:

```cpp
auto recorded =
    renderer.recordViewFrame(frame, view, state.transientImagePool, state.transientImages);
```

- [ ] Pass `renderer_` from `EditorSharedViewportRenderProducer::renderSceneViewFrame()` into `recordSharedViewportFrame`.
- [ ] Before recording, if `frameEpochTracker_.stats().pending == 0U`, call `renderer_.resetFrameResourceCursors()` so sequential packet releases do not exhaust rewritten descriptor rings.
- [ ] Remove `++stats_.rendererCreations;` from `renderSceneViewFrame()`.
- [ ] Keep `++stats_.framesRendered;` and `++stats_.packetsCreated;` after successful recording.
- [ ] Keep all per-packet creation, submit, export, fence wait, handle closing, command pool destruction, image lease release, and epoch completion behavior otherwise unchanged.

## Task 3: Add Runtime And ABI V4 Diagnostics

- [ ] Modify `apps/editor/src/editor_shared_viewport_runtime.hpp`.
- [ ] Add to `EditorSharedViewportRuntimeStats`:

```cpp
std::uint64_t rendererCreations{};
```

- [ ] Modify `apps/editor/src/editor_shared_viewport_runtime.cpp`.
- [ ] Copy the producer field into runtime stats:

```cpp
.rendererCreations = producerStats.rendererCreations,
```

- [ ] Preserve existing `framesRendered`, `producersCreated`, `packetsCreated`, external image counters, epoch counters, `outstandingPackets`, `hasContext`, `hasRenderProducer`, and `shutdownRequested` semantics.
- [ ] Modify `apps/editor/src/native_bridge/viewport_native_api.hpp`.
- [ ] Add `EditorViewportNativeRuntimeStatsV4` after v3. Use this exact field order:

```cpp
struct EditorViewportNativeRuntimeStatsV4 {
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
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
};
```

- [ ] Add:

```cpp
EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v4(EditorViewportNativeRuntimeStatsV4* stats);
```

- [ ] Modify `apps/editor/src/native_bridge/viewport_native_api.cpp`.
- [ ] Add `runtimeStatsV4Header()` with `sizeof(EditorViewportNativeRuntimeStatsV4)`.
- [ ] Implement `editor_viewport_query_runtime_stats_v4()` with the same null handling and success status as v3.
- [ ] Fill all v3-compatible fields from runtime stats.
- [ ] Fill `.rendererCreations = runtimeStats.rendererCreations`.
- [ ] Do not change v1, v2, or v3 query functions.

## Task 4: Complete Native Smoke V4 Assertions

- [ ] After the first packet acquisition and before release, assert:

```cpp
stats.rendererCreations == 1U
stats.framesRendered == 1U
stats.producersCreated == 1U
stats.packetsCreated == 1U
stats.outstandingPackets == 1U
stats.frameEpochsSubmitted == 1U
stats.frameEpochsCompleted == 0U
stats.frameEpochsPending == 1U
```

- [ ] After first release, assert:

```cpp
stats.rendererCreations == 1U
stats.outstandingPackets == 0U
stats.frameEpochsSubmitted == 1U
stats.frameEpochsCompleted == 1U
stats.frameEpochsPending == 0U
```

- [ ] After second same-size packet release, assert:

```cpp
stats.rendererCreations == 1U
stats.packetsCreated == 2U
stats.frameEpochsSubmitted == 2U
stats.frameEpochsCompleted == 2U
stats.frameEpochsPending == 0U
```

- [ ] Keep the existing v2 same-size external image pool assertions.
- [ ] After resized packet release, assert:

```cpp
stats.rendererCreations == 1U
stats.packetsCreated == 3U
stats.frameEpochsSubmitted == 3U
stats.frameEpochsCompleted == 3U
stats.frameEpochsPending == 0U
```

- [ ] Keep the existing v2 resize external image pool assertions.
- [ ] Before shutdown with one pending packet, assert:

```cpp
stats.rendererCreations == 1U
stats.packetsCreated == 4U
stats.outstandingPackets == 1U
stats.frameEpochsSubmitted == 4U
stats.frameEpochsCompleted == 3U
stats.frameEpochsPending == 1U
```

- [ ] Keep the existing shutdown acquire rejection assertion.

## Task 5: Update Architecture Docs

- [ ] Modify `docs/architecture/flow.md` in the Studio Avalonia Scene View Composition section.
- [ ] State that `editor shared viewport runtime` owns context lifetime, producer lifetime, outstanding packet tracking, and shutdown drain.
- [ ] State that the native render producer owns:
  - persistent `BasicFullscreenTextureRenderer`
  - producer-local external image pool
  - producer-local submitted/completed frame epoch tracker
  - per-packet render submission orchestration
- [ ] State that each `EditorSharedViewportPacketState` owns:
  - external image lease
  - transient render graph images recorded for that packet
  - wait and signal semaphores
  - command pool and command buffer
  - fence
  - exported image and semaphore OS handles
  - frame epoch lease
- [ ] State that persistent renderer destruction is safe because runtime shutdown drain prevents producer/context teardown while packets or release operations are active.
- [ ] State that descriptor/resource cursor reset is allowed only when the producer epoch tracker reports no pending packet.
- [ ] Update the diagnostics sentence so v4 stats expose renderer creation reuse while v1, v2, and v3 remain unchanged.

## Task 6: Local Review Checkpoint

- [ ] Run a targeted search to confirm no per-packet renderer storage or creation remains:

```powershell
rg -n "state\.renderer|BasicFullscreenTextureRenderer::create|rendererCreations" apps\editor\src\editor_shared_viewport_render_producer.*
```

- [ ] Expected result:
  - `BasicFullscreenTextureRenderer::create` appears only in `EditorSharedViewportRenderProducer::create()`.
  - `rendererCreations` increments only in the producer factory.
  - No `state.renderer` result remains.
- [ ] Run a targeted search for v4 ABI declarations and use:

```powershell
rg -n "RuntimeStatsV4|query_runtime_stats_v4|rendererCreations" apps\editor\src
```

## Task 7: Validation

- [ ] Fix encoding if any C/C++ file was created or if the checker reports a violation:

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

- [ ] Run editor viewport smokes on both presets using the built editor executable path from `docs/workflow/review.md`:

```powershell
.\build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
.\build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
.\build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-native-bridge
.\build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
.\build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
.\build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
.\build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-native-bridge
.\build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

- [ ] If executable locations differ, use the paths produced by the successful build logs.
- [ ] Run the Vulkan C++ review helper on touched editor native files if the repo exposes the helper command referenced by `docs/workflow/review.md`.

## Done Evidence For Issue #221

When implementation and validation are complete, comment on #221 with:

- Branch name.
- Files changed.
- Statement that producer-owned renderer creation occurs once per producer and packets keep their own submit resources.
- Statement that v1, v2, and v3 stats ABI remained unchanged and v4 was added.
- Native smoke evidence showing `rendererCreations == 1U` across first, same-size, resize, and shutdown-pending packet flow.
- Existing pool and epoch smoke evidence.
- Encoding, diff, clang-cl build, MSVC build, and smoke results.
- Any validation that could not run, with exact command and reason.

Use `Closes #221` only in the PR body if all acceptance criteria are satisfied and Done evidence is posted. Use `Refs #221` if any validation remains blocked.
