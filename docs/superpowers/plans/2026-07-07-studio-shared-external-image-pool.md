# Studio Shared External Image Pool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a producer-local shared external image pool to the Studio native viewport path so same-size Avalonia Composition GPU interop frames reuse Vulkan external images.

**Architecture:** `EditorSharedViewportRenderProducer` owns a private `EditorSharedViewportExternalImagePool`. Present packets hold a move-only image lease while managed Studio owns the imported handles; packet destruction waits the packet fence, closes per-packet OS handles, and then returns the Vulkan image resource to the pool. A v2 runtime diagnostics ABI exposes pool counters while the existing v1 stats ABI remains unchanged.

**Tech Stack:** C++23, Vulkan external memory with current Windows opaque NT handle export, VMA, `editor_native` C ABI, native editor smoke tests, Avalonia Studio interop tests.

## Global Constraints

- This slice implements original option B: Avalonia Composition GPU interop. It does not add CPU readback.
- The first validated backend is Windows `VulkanOpaqueNt`; the pool key still carries the external image handle family for future platform adapters.
- The pool owns Vulkan image resources only. It does not include `windows.h`, does not store exported OS handles, and does not close OS handles.
- Each present packet exports fresh image and semaphore handles and closes each successful export exactly once during packet release.
- The pool stays private to `apps/editor`; do not add a public `rhi_vulkan` pool API.
- Do not reuse `BasicFullscreenTextureRenderer`, semaphores, command pools, command buffers, or fences in this slice.
- Do not change managed Studio composition behavior in this slice.
- `EditorViewportNativeRuntimeStats` v1 is unchanged because `editor_viewport_query_runtime_stats` has no caller-provided struct size.
- Add `EditorViewportNativeRuntimeStatsV2` and `editor_viewport_query_runtime_stats_v2` for additive diagnostics.
- C/C++ source and header files must be UTF-8 with BOM; Markdown and CMake files must be UTF-8 without BOM.

---

## File Structure

- Create `apps/editor/src/editor_shared_viewport_external_image_pool.hpp`
  - Private editor-native pool key, stats, move-only lease, and pool class.
  - Includes Vulkan types and `VulkanExternalImage`; does not include Win32 headers.
- Create `apps/editor/src/editor_shared_viewport_external_image_pool.cpp`
  - Pool acquire/release implementation, key matching, stats accounting, and lease move/destructor behavior.
- Create `apps/editor/src/editor_shared_viewport_external_image_handle_family.hpp`
  - Internal editor-native image handle family enum shared by producer and pool without creating include cycles.
- Modify `apps/editor/src/editor_shared_viewport_render_producer.hpp`
  - Add internal external image handle family enum.
  - Add handle family to present desc.
  - Replace packet-owned `VulkanExternalImage` with `EditorSharedViewportExternalImageLease`.
  - Extend producer stats with pool counters.
  - Add producer-owned pool member.
- Modify `apps/editor/src/editor_shared_viewport_render_producer.cpp`
  - Acquire the external image lease before recording.
  - Record into `imageLease.image()`.
  - Export fresh handles from the leased image for every packet.
  - Return combined producer and pool stats.
- Modify `apps/editor/src/editor_shared_viewport_runtime.hpp`
  - Add v2 runtime stats fields for external image pool counters.
- Modify `apps/editor/src/editor_shared_viewport_runtime.cpp`
  - Copy producer pool counters into runtime stats.
- Modify `apps/editor/src/native_bridge/viewport_native_api.hpp`
  - Add `EditorViewportNativeRuntimeStatsV2`.
  - Add `editor_viewport_query_runtime_stats_v2`.
- Modify `apps/editor/src/native_bridge/viewport_native_api.cpp`
  - Add v2 stats header helper.
  - Fill v2 stats without changing v1 behavior.
  - Keep native present acquisition passing `VulkanOpaqueNt` as the image handle family.
- Modify `apps/editor/src/native_bridge/viewport_native_smoke.cpp`
  - Query v2 stats.
  - Prove first same-size packet creates one image, second same-size packet reuses it, and resized packet creates the second image.
- Modify `apps/editor/CMakeLists.txt`
  - Add the new pool source to `editor-native`.
- Modify `docs/architecture/flow.md`
  - Document the producer-local external image pool and the per-packet handle ownership boundary.

## Interfaces

These names must stay consistent across tasks:

```cpp
namespace asharia::editor {

    enum class EditorSharedViewportExternalImageHandleFamily : std::uint32_t {
        VulkanOpaqueNt = 1U,
    };

    struct EditorSharedViewportExternalImagePoolStats {
        std::uint64_t acquired{};
        std::uint64_t created{};
        std::uint64_t reused{};
        std::uint64_t released{};
        std::uint64_t available{};
        std::uint64_t leased{};
    };

    class EditorSharedViewportExternalImageLease final {
    public:
        EditorSharedViewportExternalImageLease() = default;
        EditorSharedViewportExternalImageLease(
            const EditorSharedViewportExternalImageLease&) = delete;
        EditorSharedViewportExternalImageLease&
        operator=(const EditorSharedViewportExternalImageLease&) = delete;
        EditorSharedViewportExternalImageLease(
            EditorSharedViewportExternalImageLease&& other) noexcept;
        EditorSharedViewportExternalImageLease&
        operator=(EditorSharedViewportExternalImageLease&& other) noexcept;
        ~EditorSharedViewportExternalImageLease();

        [[nodiscard]] VulkanExternalImage& image();
        [[nodiscard]] const VulkanExternalImage& image() const;
        [[nodiscard]] bool hasImage() const;
    };

    class EditorSharedViewportExternalImagePool final {
    public:
        EditorSharedViewportExternalImagePool();
        EditorSharedViewportExternalImagePool(
            const EditorSharedViewportExternalImagePool&) = delete;
        EditorSharedViewportExternalImagePool&
        operator=(const EditorSharedViewportExternalImagePool&) = delete;
        EditorSharedViewportExternalImagePool(
            EditorSharedViewportExternalImagePool&&) noexcept = default;
        EditorSharedViewportExternalImagePool&
        operator=(EditorSharedViewportExternalImagePool&&) noexcept = default;
        ~EditorSharedViewportExternalImagePool() = default;

        [[nodiscard]] Result<EditorSharedViewportExternalImageLease>
        acquire(EditorSharedViewportExternalImageHandleFamily imageHandleFamily,
                const VulkanExternalImageDesc& desc);

        [[nodiscard]] EditorSharedViewportExternalImagePoolStats stats() const;
    };

} // namespace asharia::editor
```

---

### Task 1: Add Runtime Stats V2 Baseline

**Files:**
- Modify: `apps/editor/src/native_bridge/viewport_native_api.hpp`
- Modify: `apps/editor/src/native_bridge/viewport_native_api.cpp`
- Modify: `apps/editor/src/native_bridge/viewport_native_smoke.cpp`

**Interfaces:**
- Consumes: existing `EditorSharedViewportRuntime::stats()`.
- Produces: `EditorViewportNativeRuntimeStatsV2` and `editor_viewport_query_runtime_stats_v2(EditorViewportNativeRuntimeStatsV2*)`.

- [ ] **Step 1: Add the v2 ABI declaration**

In `apps/editor/src/native_bridge/viewport_native_api.hpp`, add this struct immediately after `EditorViewportNativeRuntimeStats`:

```cpp
struct EditorViewportNativeRuntimeStatsV2 {
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
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
};
```

Add this declaration immediately after `editor_viewport_query_runtime_stats`:

```cpp
EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v2(EditorViewportNativeRuntimeStatsV2* stats);
```

- [ ] **Step 2: Implement the v2 ABI with zero pool counters**

In `apps/editor/src/native_bridge/viewport_native_api.cpp`, add this helper after `runtimeStatsHeader()`:

```cpp
[[nodiscard]] EditorViewportNativeAbiHeader runtimeStatsV2Header() {
    return EditorViewportNativeAbiHeader{
        .abiVersion = EDITOR_NATIVE_ABI_VERSION,
        .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeRuntimeStatsV2)),
    };
}
```

Add this function immediately after `editor_viewport_query_runtime_stats`:

```cpp
std::uint32_t EDITOR_NATIVE_CALL editor_viewport_query_runtime_stats_v2(
    EditorViewportNativeRuntimeStatsV2* stats) {
    if (stats == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }

    const asharia::editor::EditorSharedViewportRuntimeStats runtimeStats =
        asharia::editor::EditorSharedViewportRuntime::instance().stats();
    *stats = EditorViewportNativeRuntimeStatsV2{
        .header = runtimeStatsV2Header(),
        .framesRendered = runtimeStats.framesRendered,
        .producersCreated = runtimeStats.producersCreated,
        .packetsCreated = runtimeStats.packetsCreated,
        .outstandingPackets = static_cast<std::uint64_t>(runtimeStats.outstandingPackets),
        .externalImagesAcquired = 0U,
        .externalImagesCreated = 0U,
        .externalImagesReused = 0U,
        .externalImagesReleased = 0U,
        .externalImagesAvailable = 0U,
        .externalImagesLeased = 0U,
        .hasContext = runtimeStats.hasContext ? 1U : 0U,
        .hasRenderProducer = runtimeStats.hasRenderProducer ? 1U : 0U,
        .shutdownRequested = runtimeStats.shutdownRequested ? 1U : 0U,
    };
    return EditorViewportNativeStatus_Success;
}
```

- [ ] **Step 3: Add a v2 baseline smoke check**

In `apps/editor/src/native_bridge/viewport_native_smoke.cpp`, after the existing first-packet v1 stats check and before releasing the first packet, add:

```cpp
EditorViewportNativeRuntimeStatsV2 statsV2AfterFirstPacket{};
const std::uint32_t statsV2Status =
    editor_viewport_query_runtime_stats_v2(&statsV2AfterFirstPacket);
if (statsV2Status != EditorViewportNativeStatus_Success ||
    statsV2AfterFirstPacket.header.structSize !=
        sizeof(EditorViewportNativeRuntimeStatsV2) ||
    statsV2AfterFirstPacket.framesRendered != 1U ||
    statsV2AfterFirstPacket.producersCreated != 1U ||
    statsV2AfterFirstPacket.packetsCreated != 1U ||
    statsV2AfterFirstPacket.outstandingPackets != 1U ||
    statsV2AfterFirstPacket.hasRenderProducer == 0U) {
    releaseIfNeeded(packet);
    logError("Viewport native bridge smoke did not expose runtime stats v2.");
    return false;
}
```

- [ ] **Step 4: Run the targeted native build**

Run:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug --target asharia-editor"
```

Expected: build succeeds.

- [ ] **Step 5: Run the native viewport smoke**

Run:

```powershell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

Expected: command exits `0`.

- [ ] **Step 6: Commit**

```powershell
git add apps/editor/src/native_bridge/viewport_native_api.hpp apps/editor/src/native_bridge/viewport_native_api.cpp apps/editor/src/native_bridge/viewport_native_smoke.cpp
git commit -m "feat: add studio viewport runtime stats v2"
```

---

### Task 2: Add Producer-Local External Image Pool Helper

**Files:**
- Create: `apps/editor/src/editor_shared_viewport_external_image_handle_family.hpp`
- Create: `apps/editor/src/editor_shared_viewport_external_image_pool.hpp`
- Create: `apps/editor/src/editor_shared_viewport_external_image_pool.cpp`
- Modify: `apps/editor/src/editor_shared_viewport_render_producer.hpp`
- Modify: `apps/editor/CMakeLists.txt`

**Interfaces:**
- Consumes: `VulkanExternalImage` and `VulkanExternalImageDesc`.
- Produces: `EditorSharedViewportExternalImagePool`, `EditorSharedViewportExternalImageLease`, and `EditorSharedViewportExternalImagePoolStats`.

- [ ] **Step 1: Add the internal image handle family header**

Create `apps/editor/src/editor_shared_viewport_external_image_handle_family.hpp` with:

```cpp
#pragma once

#include <cstdint>

namespace asharia::editor {

enum class EditorSharedViewportExternalImageHandleFamily : std::uint32_t {
    VulkanOpaqueNt = 1U,
};

} // namespace asharia::editor
```

In `apps/editor/src/editor_shared_viewport_render_producer.hpp`, add this include after the RHI includes and before `editor_viewport.hpp`:

```cpp
#include "editor_shared_viewport_external_image_handle_family.hpp"
```

Add this field to `EditorSharedViewportPresentDesc` after `extent`:

```cpp
EditorSharedViewportExternalImageHandleFamily imageHandleFamily{
    EditorSharedViewportExternalImageHandleFamily::VulkanOpaqueNt};
```

- [ ] **Step 2: Create the pool header**

Create `apps/editor/src/editor_shared_viewport_external_image_pool.hpp` with:

```cpp
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vulkan_external_memory.hpp"

#include "editor_shared_viewport_external_image_handle_family.hpp"

namespace asharia::editor {

    struct EditorSharedViewportExternalImageKey {
        EditorSharedViewportExternalImageHandleFamily imageHandleFamily{
            EditorSharedViewportExternalImageHandleFamily::VulkanOpaqueNt};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkImageUsageFlags usage{};
        VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
    };

    struct EditorSharedViewportExternalImagePoolResource {
        EditorSharedViewportExternalImageKey key;
        VulkanExternalImage image;
    };

    struct EditorSharedViewportExternalImagePoolState;

    struct EditorSharedViewportExternalImagePoolStats {
        std::uint64_t acquired{};
        std::uint64_t created{};
        std::uint64_t reused{};
        std::uint64_t released{};
        std::uint64_t available{};
        std::uint64_t leased{};
    };

    class EditorSharedViewportExternalImagePool;

    class EditorSharedViewportExternalImageLease final {
    public:
        EditorSharedViewportExternalImageLease() = default;
        EditorSharedViewportExternalImageLease(
            const EditorSharedViewportExternalImageLease&) = delete;
        EditorSharedViewportExternalImageLease&
        operator=(const EditorSharedViewportExternalImageLease&) = delete;
        EditorSharedViewportExternalImageLease(
            EditorSharedViewportExternalImageLease&& other) noexcept;
        EditorSharedViewportExternalImageLease&
        operator=(EditorSharedViewportExternalImageLease&& other) noexcept;
        ~EditorSharedViewportExternalImageLease();

        [[nodiscard]] VulkanExternalImage& image();
        [[nodiscard]] const VulkanExternalImage& image() const;
        [[nodiscard]] bool hasImage() const;

    private:
        friend class EditorSharedViewportExternalImagePool;

        EditorSharedViewportExternalImageLease(
            std::shared_ptr<EditorSharedViewportExternalImagePoolState> state,
            EditorSharedViewportExternalImagePoolResource resource) noexcept;

        void release() noexcept;

        std::shared_ptr<EditorSharedViewportExternalImagePoolState> state_;
        std::optional<EditorSharedViewportExternalImagePoolResource> resource_;
    };

    class EditorSharedViewportExternalImagePool final {
    public:
        EditorSharedViewportExternalImagePool();
        EditorSharedViewportExternalImagePool(
            const EditorSharedViewportExternalImagePool&) = delete;
        EditorSharedViewportExternalImagePool&
        operator=(const EditorSharedViewportExternalImagePool&) = delete;
        EditorSharedViewportExternalImagePool(
            EditorSharedViewportExternalImagePool&&) noexcept = default;
        EditorSharedViewportExternalImagePool&
        operator=(EditorSharedViewportExternalImagePool&&) noexcept = default;
        ~EditorSharedViewportExternalImagePool() = default;

        [[nodiscard]] Result<EditorSharedViewportExternalImageLease>
        acquire(EditorSharedViewportExternalImageHandleFamily imageHandleFamily,
                const VulkanExternalImageDesc& desc);

        [[nodiscard]] EditorSharedViewportExternalImagePoolStats stats() const;

    private:
        std::shared_ptr<EditorSharedViewportExternalImagePoolState> state_;
    };

} // namespace asharia::editor
```

- [ ] **Step 3: Create the pool implementation**

Create `apps/editor/src/editor_shared_viewport_external_image_pool.cpp` with:

```cpp
#include "editor_shared_viewport_external_image_pool.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#include "asharia/core/log.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] bool matchesKey(const EditorSharedViewportExternalImageKey& left,
                                      const EditorSharedViewportExternalImageKey& right) {
            return left.imageHandleFamily == right.imageHandleFamily &&
                   left.format == right.format && left.extent.width == right.extent.width &&
                   left.extent.height == right.extent.height && left.usage == right.usage &&
                   left.aspectMask == right.aspectMask;
        }

        [[nodiscard]] EditorSharedViewportExternalImageKey makeKey(
            EditorSharedViewportExternalImageHandleFamily imageHandleFamily,
            const VulkanExternalImageDesc& desc) {
            return EditorSharedViewportExternalImageKey{
                .imageHandleFamily = imageHandleFamily,
                .format = desc.format,
                .extent = desc.extent,
                .usage = desc.usage,
                .aspectMask = desc.aspectMask,
            };
        }

        [[nodiscard]] Result<void> validateAcquireInputs(
            EditorSharedViewportExternalImageHandleFamily imageHandleFamily,
            const VulkanExternalImageDesc& desc) {
            if (imageHandleFamily != EditorSharedViewportExternalImageHandleFamily::VulkanOpaqueNt) {
                return std::unexpected{
                    vulkanError("Shared viewport external image handle family is unsupported")};
            }

            if (desc.device == VK_NULL_HANDLE || desc.allocator == nullptr ||
                desc.format == VK_FORMAT_UNDEFINED || desc.extent.width == 0U ||
                desc.extent.height == 0U || desc.usage == 0U || desc.aspectMask == 0U) {
                return std::unexpected{
                    vulkanError("Cannot acquire a shared viewport external image from incomplete inputs")};
            }

            return {};
        }

    } // namespace

    struct EditorSharedViewportExternalImagePoolState {
        mutable std::mutex mutex;
        std::vector<EditorSharedViewportExternalImagePoolResource> available;
        EditorSharedViewportExternalImagePoolStats stats;
    };

    EditorSharedViewportExternalImageLease::EditorSharedViewportExternalImageLease(
        std::shared_ptr<EditorSharedViewportExternalImagePoolState> state,
        EditorSharedViewportExternalImagePoolResource resource) noexcept
        : state_{std::move(state)}, resource_{std::move(resource)} {}

    EditorSharedViewportExternalImageLease::EditorSharedViewportExternalImageLease(
        EditorSharedViewportExternalImageLease&& other) noexcept {
        *this = std::move(other);
    }

    EditorSharedViewportExternalImageLease&
    EditorSharedViewportExternalImageLease::operator=(
        EditorSharedViewportExternalImageLease&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        release();
        state_ = std::move(other.state_);
        resource_ = std::move(other.resource_);
        other.resource_.reset();
        return *this;
    }

    EditorSharedViewportExternalImageLease::~EditorSharedViewportExternalImageLease() {
        release();
    }

    VulkanExternalImage& EditorSharedViewportExternalImageLease::image() {
        return resource_->image;
    }

    const VulkanExternalImage& EditorSharedViewportExternalImageLease::image() const {
        return resource_->image;
    }

    bool EditorSharedViewportExternalImageLease::hasImage() const {
        return resource_.has_value();
    }

    void EditorSharedViewportExternalImageLease::release() noexcept {
        if (!state_ || !resource_) {
            return;
        }

        std::lock_guard lock{state_->mutex};
        ++state_->stats.released;
        if (state_->stats.leased > 0U) {
            --state_->stats.leased;
        }

        try {
            state_->available.push_back(std::move(*resource_));
            state_->stats.available =
                static_cast<std::uint64_t>(state_->available.size());
        } catch (const std::bad_alloc&) {
            logError("Shared viewport external image pool dropped an image during release.");
            state_->stats.available =
                static_cast<std::uint64_t>(state_->available.size());
        }

        resource_.reset();
    }

    EditorSharedViewportExternalImagePool::EditorSharedViewportExternalImagePool()
        : state_{std::make_shared<EditorSharedViewportExternalImageLease::PoolState>()} {}

    Result<EditorSharedViewportExternalImageLease>
    EditorSharedViewportExternalImagePool::acquire(
        EditorSharedViewportExternalImageHandleFamily imageHandleFamily,
        const VulkanExternalImageDesc& desc) {
        if (!state_) {
            return std::unexpected{
                vulkanError("Cannot acquire a shared viewport external image from a moved pool")};
        }

        auto validated = validateAcquireInputs(imageHandleFamily, desc);
        if (!validated) {
            return std::unexpected{std::move(validated.error())};
        }

        const EditorSharedViewportExternalImageKey key = makeKey(imageHandleFamily, desc);

        {
            std::lock_guard lock{state_->mutex};
            auto iter = std::find_if(
                state_->available.begin(), state_->available.end(),
                [&key](const EditorSharedViewportExternalImagePoolResource& resource) {
                    return matchesKey(resource.key, key);
                });
            if (iter != state_->available.end()) {
                EditorSharedViewportExternalImagePoolResource resource = std::move(*iter);
                state_->available.erase(iter);
                ++state_->stats.acquired;
                ++state_->stats.reused;
                ++state_->stats.leased;
                state_->stats.available =
                    static_cast<std::uint64_t>(state_->available.size());
                return EditorSharedViewportExternalImageLease{state_, std::move(resource)};
            }
        }

        auto image = VulkanExternalImage::create(desc);
        if (!image) {
            return std::unexpected{std::move(image.error())};
        }

        EditorSharedViewportExternalImagePoolResource resource{
            .key = key,
            .image = std::move(*image),
        };

        {
            std::lock_guard lock{state_->mutex};
            ++state_->stats.acquired;
            ++state_->stats.created;
            ++state_->stats.leased;
            state_->stats.available =
                static_cast<std::uint64_t>(state_->available.size());
        }

        return EditorSharedViewportExternalImageLease{state_, std::move(resource)};
    }

    EditorSharedViewportExternalImagePoolStats
    EditorSharedViewportExternalImagePool::stats() const {
        if (!state_) {
            return {};
        }

        std::lock_guard lock{state_->mutex};
        EditorSharedViewportExternalImagePoolStats snapshot = state_->stats;
        snapshot.available = static_cast<std::uint64_t>(state_->available.size());
        return snapshot;
    }

} // namespace asharia::editor
```

- [ ] **Step 4: Add the pool source to the native target**

In `apps/editor/CMakeLists.txt`, add the new source to `editor-native`:

```cmake
add_library(editor-native SHARED
    src/editor_shared_viewport_external_image_pool.cpp
    src/editor_shared_viewport_render_producer.cpp
    src/editor_shared_viewport_runtime.cpp
    src/editor_frame_debugger.cpp
    src/editor_frame_debugger_replay.cpp
    src/editor_frame_debugger_snapshot_projector.cpp
    src/native_bridge/frame_debugger_native_api.cpp
    src/native_bridge/viewport_native_api.cpp
)
```

- [ ] **Step 5: Include the pool from the producer header**

In `apps/editor/src/editor_shared_viewport_render_producer.hpp`, add this include after the handle-family include:

```cpp
#include "editor_shared_viewport_external_image_pool.hpp"
```

- [ ] **Step 6: Confirm no CMake entry is needed for headers**

No CMake entry is needed for `editor_shared_viewport_external_image_handle_family.hpp` because headers are included by sources.

- [ ] **Step 7: Fix C++ file encodings**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1 -Fix
```

Expected: new `.hpp` and `.cpp` files are UTF-8 with BOM; Markdown and CMake files remain UTF-8 without BOM.

- [ ] **Step 8: Build the native editor target**

Run:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug --target asharia-editor"
```

Expected: build succeeds.

- [ ] **Step 9: Commit**

```powershell
git add apps/editor/src/editor_shared_viewport_external_image_handle_family.hpp apps/editor/src/editor_shared_viewport_external_image_pool.hpp apps/editor/src/editor_shared_viewport_external_image_pool.cpp apps/editor/src/editor_shared_viewport_render_producer.hpp apps/editor/CMakeLists.txt
git commit -m "feat: add studio shared external image pool"
```

---

### Task 3: Integrate Pool Into Render Producer

**Files:**
- Modify: `apps/editor/src/editor_shared_viewport_render_producer.hpp`
- Modify: `apps/editor/src/editor_shared_viewport_render_producer.cpp`

**Interfaces:**
- Consumes: `EditorSharedViewportExternalImagePool::acquire(...)`.
- Produces: producer packets whose image resource is held by `EditorSharedViewportExternalImageLease`.

- [ ] **Step 1: Extend producer stats**

In `apps/editor/src/editor_shared_viewport_render_producer.hpp`, replace `EditorSharedViewportRenderProducerStats` with:

```cpp
struct EditorSharedViewportRenderProducerStats {
    std::uint64_t framesRendered{};
    std::uint64_t packetsCreated{};
    std::uint64_t rendererCreations{};
    std::uint64_t externalImagesAcquired{};
    std::uint64_t externalImagesCreated{};
    std::uint64_t externalImagesReused{};
    std::uint64_t externalImagesReleased{};
    std::uint64_t externalImagesAvailable{};
    std::uint64_t externalImagesLeased{};
};
```

- [ ] **Step 2: Replace packet-owned image with a lease**

In `EditorSharedViewportPacketState`, replace:

```cpp
VulkanExternalImage image;
```

with:

```cpp
EditorSharedViewportExternalImageLease imageLease;
```

Add the pool member to `EditorSharedViewportRenderProducer` after `stats_`:

```cpp
EditorSharedViewportExternalImagePool externalImagePool_;
```

- [ ] **Step 3: Acquire pooled images in frame recording**

In `apps/editor/src/editor_shared_viewport_render_producer.cpp`, change `recordSharedViewportFrame` signature to:

```cpp
[[nodiscard]] Result<void> recordSharedViewportFrame(
    VkDevice device, VmaAllocator allocator, VkQueue graphicsQueue,
    std::uint32_t graphicsQueueFamily,
    EditorSharedViewportExternalImagePool& externalImagePool,
    EditorSharedViewportPacketState& state, EditorSharedViewportPresentDesc desc,
    std::uint64_t frameIndex) {
```

Replace the existing `VulkanExternalImage::create(...)` block with:

```cpp
auto imageLease = externalImagePool.acquire(
    desc.imageHandleFamily,
    VulkanExternalImageDesc{
        .device = device,
        .allocator = allocator,
        .format = kSharedViewportFormat,
        .extent = VkExtent2D{.width = desc.extent.width, .height = desc.extent.height},
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    });
if (!imageLease) {
    return std::unexpected{std::move(imageLease.error())};
}
state.imageLease = std::move(*imageLease);
```

Immediately after this block add:

```cpp
VulkanExternalImage& targetImage = state.imageLease.image();
```

Then replace every `state.image.` use in `recordSharedViewportFrame` with `targetImage.`. The replacements are:

```cpp
.image = targetImage.image(),
.imageView = targetImage.imageView(),
.format = targetImage.format(),
.extent = targetImage.extent(),
```

```cpp
.image = targetImage.image(),
.imageView = targetImage.imageView(),
.format = targetImage.format(),
.extent = targetImage.extent(),
.aspectMask = targetImage.aspectMask(),
```

```cpp
auto imageHandle = targetImage.exportOpaqueWin32Handle();
```

- [ ] **Step 4: Return packet metadata from the lease**

In `EditorSharedViewportPacketState::toPresentPacket`, replace the body with:

```cpp
VulkanExternalImage& targetImage = imageLease.image();
return EditorSharedViewportPresentPacket{
    .nativePacket = this,
    .imageHandle = imageHandle,
    .waitSemaphoreHandle = waitSemaphoreHandle,
    .signalSemaphoreHandle = signalSemaphoreHandle,
    .format = targetImage.format(),
    .extent = targetImage.extent(),
    .memorySizeBytes = targetImage.memorySizeBytes(),
    .frameIndex = frameIndex,
};
```

- [ ] **Step 5: Preserve packet release ordering**

Keep `EditorSharedViewportPacketState::~EditorSharedViewportPacketState()` body order as:

```cpp
if (submitted && device != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) {
    // Packet release is a compositor/native ownership boundary. Waiting here keeps
    // command-buffer resources alive without stalling the render submit path.
    const VkResult waited = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (waited != VK_SUCCESS) {
        logError("Shared viewport packet fence wait failed during release.");
    }
}

closeHandle(imageHandle);
closeHandle(waitSemaphoreHandle);
closeHandle(signalSemaphoreHandle);

if (device != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) {
    vkDestroyFence(device, fence, nullptr);
}
if (device != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device, commandPool, nullptr);
}
```

Do not manually call `imageLease.release()`. The field destructor runs after the destructor body, so the image returns to the pool after fence wait and OS handle closure.

- [ ] **Step 6: Pass the pool into recording**

In `EditorSharedViewportRenderProducer::renderSceneViewFrame`, replace the call with:

```cpp
auto rendered = recordSharedViewportFrame(device_, allocator_, graphicsQueue_,
                                          graphicsQueueFamily_, externalImagePool_, *state,
                                          desc, frameIndex);
```

- [ ] **Step 7: Merge pool stats into producer stats**

Replace `EditorSharedViewportRenderProducer::stats()` with:

```cpp
EditorSharedViewportRenderProducerStats EditorSharedViewportRenderProducer::stats() const {
    EditorSharedViewportRenderProducerStats snapshot = stats_;
    const EditorSharedViewportExternalImagePoolStats poolStats = externalImagePool_.stats();
    snapshot.externalImagesAcquired = poolStats.acquired;
    snapshot.externalImagesCreated = poolStats.created;
    snapshot.externalImagesReused = poolStats.reused;
    snapshot.externalImagesReleased = poolStats.released;
    snapshot.externalImagesAvailable = poolStats.available;
    snapshot.externalImagesLeased = poolStats.leased;
    return snapshot;
}
```

- [ ] **Step 8: Build the native editor target**

Run:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug --target asharia-editor"
```

Expected: build succeeds.

- [ ] **Step 9: Commit**

```powershell
git add apps/editor/src/editor_shared_viewport_render_producer.hpp apps/editor/src/editor_shared_viewport_render_producer.cpp
git commit -m "feat: reuse studio shared viewport external images"
```

---

### Task 4: Surface Pool Counters Through Runtime And ABI

**Files:**
- Modify: `apps/editor/src/editor_shared_viewport_runtime.hpp`
- Modify: `apps/editor/src/editor_shared_viewport_runtime.cpp`
- Modify: `apps/editor/src/native_bridge/viewport_native_api.cpp`

**Interfaces:**
- Consumes: `EditorSharedViewportRenderProducerStats` pool fields.
- Produces: nonzero v2 pool counters for native smoke.

- [ ] **Step 1: Extend runtime stats**

In `apps/editor/src/editor_shared_viewport_runtime.hpp`, replace `EditorSharedViewportRuntimeStats` with:

```cpp
struct EditorSharedViewportRuntimeStats {
    std::uint64_t framesRendered{};
    std::uint64_t producersCreated{};
    std::uint64_t packetsCreated{};
    std::uint64_t externalImagesAcquired{};
    std::uint64_t externalImagesCreated{};
    std::uint64_t externalImagesReused{};
    std::uint64_t externalImagesReleased{};
    std::uint64_t externalImagesAvailable{};
    std::uint64_t externalImagesLeased{};
    std::size_t outstandingPackets{};
    bool hasContext{};
    bool hasRenderProducer{};
    bool shutdownRequested{};
};
```

- [ ] **Step 2: Copy producer counters into runtime stats**

In `EditorSharedViewportRuntime::stats()`, replace the function body with:

```cpp
std::lock_guard lock{mutex_};
EditorSharedViewportRenderProducerStats producerStats{};
if (renderProducer_) {
    producerStats = renderProducer_->stats();
}

return EditorSharedViewportRuntimeStats{
    .framesRendered = framesRendered_,
    .producersCreated = producersCreated_,
    .packetsCreated = packetsCreated_,
    .externalImagesAcquired = producerStats.externalImagesAcquired,
    .externalImagesCreated = producerStats.externalImagesCreated,
    .externalImagesReused = producerStats.externalImagesReused,
    .externalImagesReleased = producerStats.externalImagesReleased,
    .externalImagesAvailable = producerStats.externalImagesAvailable,
    .externalImagesLeased = producerStats.externalImagesLeased,
    .outstandingPackets = outstandingPackets_.size(),
    .hasContext = context_.has_value(),
    .hasRenderProducer = renderProducer_.has_value(),
    .shutdownRequested = shutdownRequested_,
};
```

- [ ] **Step 3: Fill the v2 ABI pool counters**

In `editor_viewport_query_runtime_stats_v2`, replace the zero pool assignments with:

```cpp
.externalImagesAcquired = runtimeStats.externalImagesAcquired,
.externalImagesCreated = runtimeStats.externalImagesCreated,
.externalImagesReused = runtimeStats.externalImagesReused,
.externalImagesReleased = runtimeStats.externalImagesReleased,
.externalImagesAvailable = runtimeStats.externalImagesAvailable,
.externalImagesLeased = runtimeStats.externalImagesLeased,
```

- [ ] **Step 4: Build the native editor target**

Run:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug --target asharia-editor"
```

Expected: build succeeds.

- [ ] **Step 5: Commit**

```powershell
git add apps/editor/src/editor_shared_viewport_runtime.hpp apps/editor/src/editor_shared_viewport_runtime.cpp apps/editor/src/native_bridge/viewport_native_api.cpp
git commit -m "feat: expose studio shared image pool stats"
```

---

### Task 5: Prove Same-Size Reuse And Resize Allocation

**Files:**
- Modify: `apps/editor/src/native_bridge/viewport_native_smoke.cpp`

**Interfaces:**
- Consumes: v2 pool counters from `editor_viewport_query_runtime_stats_v2`.
- Produces: native smoke assertions that guard pool reuse and resize behavior.

- [ ] **Step 1: Add a v2 stats query helper**

In `apps/editor/src/native_bridge/viewport_native_smoke.cpp`, add this helper in the unnamed namespace after `expectCompatibilityStatus`:

```cpp
[[nodiscard]] bool queryRuntimeStatsV2(EditorViewportNativeRuntimeStatsV2& stats) {
    const std::uint32_t status = editor_viewport_query_runtime_stats_v2(&stats);
    return status == EditorViewportNativeStatus_Success &&
           stats.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
           stats.header.structSize == sizeof(EditorViewportNativeRuntimeStatsV2);
}
```

- [ ] **Step 2: Acquire and release a second same-size packet**

After the first packet release, insert:

```cpp
EditorViewportNativePresentPacket secondPacket{};
EditorViewportNativePresentRequest secondPresentRequest =
    makePresentRequest(VkExtent2D{.width = 320U, .height = 180U});
const std::uint32_t secondPacketStatus =
    editor_viewport_acquire_present_packet(&secondPresentRequest, &secondPacket);
const bool secondPacketAvailable =
    secondPacketStatus == EditorViewportNativeStatus_Success &&
    secondPacket.status == EditorViewportNativeStatus_Success &&
    secondPacket.nativePacket != nullptr && secondPacket.imageHandle != nullptr &&
    secondPacket.waitSemaphoreHandle != nullptr &&
    secondPacket.signalSemaphoreHandle != nullptr && secondPacket.widthPixels == 320U &&
    secondPacket.heightPixels == 180U && secondPacket.frameIndex == 2U;
if (!secondPacketAvailable) {
    logPresentPacketMessage(secondPacket);
    releaseIfNeeded(secondPacket);
    logError("Viewport native bridge smoke did not produce the second same-size packet.");
    return false;
}
releaseIfNeeded(secondPacket);

EditorViewportNativeRuntimeStatsV2 statsAfterSameSizeReuse{};
if (!queryRuntimeStatsV2(statsAfterSameSizeReuse) ||
    statsAfterSameSizeReuse.externalImagesAcquired != 2U ||
    statsAfterSameSizeReuse.externalImagesCreated != 1U ||
    statsAfterSameSizeReuse.externalImagesReused < 1U ||
    statsAfterSameSizeReuse.externalImagesReleased < 2U ||
    statsAfterSameSizeReuse.externalImagesAvailable < 1U ||
    statsAfterSameSizeReuse.externalImagesLeased != 0U) {
    logError("Viewport native bridge smoke did not observe same-size external image reuse.");
    return false;
}
```

- [ ] **Step 3: Update resized packet frame index and pool assertions**

In the resized packet check, change:

```cpp
resizedPacket.heightPixels == 360U && resizedPacket.frameIndex == 2U;
```

to:

```cpp
resizedPacket.heightPixels == 360U && resizedPacket.frameIndex == 3U;
```

After releasing the resized packet, insert:

```cpp
EditorViewportNativeRuntimeStatsV2 statsAfterResize{};
if (!queryRuntimeStatsV2(statsAfterResize) ||
    statsAfterResize.externalImagesAcquired != 3U ||
    statsAfterResize.externalImagesCreated != 2U ||
    statsAfterResize.externalImagesReused < 1U ||
    statsAfterResize.externalImagesReleased < 3U ||
    statsAfterResize.externalImagesAvailable < 2U ||
    statsAfterResize.externalImagesLeased != 0U) {
    logError("Viewport native bridge smoke did not observe resize external image allocation.");
    return false;
}
```

- [ ] **Step 4: Update shutdown pending frame index**

In the shutdown pending packet check, change:

```cpp
shutdownPendingPacket.frameIndex == 3U;
```

to:

```cpp
shutdownPendingPacket.frameIndex == 4U;
```

- [ ] **Step 5: Run the native viewport smoke**

Run:

```powershell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

Expected: command exits `0`.

- [ ] **Step 6: Commit**

```powershell
git add apps/editor/src/native_bridge/viewport_native_smoke.cpp
git commit -m "test: cover studio shared image pool reuse"
```

---

### Task 6: Document Flow And Run Regression Gates

**Files:**
- Modify: `docs/architecture/flow.md`

**Interfaces:**
- Consumes: implemented producer-local pool behavior.
- Produces: architecture documentation and validation evidence.

- [ ] **Step 1: Update Studio composition data flow text**

In `docs/architecture/flow.md`, in the "Studio Avalonia Scene View Composition" section, update the producer and release notes to state:

```markdown
- `editor shared viewport runtime` owns Vulkan context, producer lifetime,
  outstanding packet tracking and shutdown drain. The native render producer owns
  RenderView recording, per-packet external semaphore/command/fence resources,
  and a producer-local external image pool keyed by image handle family, format,
  extent, usage and aspect mask.
- External image pool entries own Vulkan image resources only. Win32 opaque NT
  image/semaphore handles are exported fresh per packet and are closed during
  native packet release; the pool does not store or close OS handles.
- Windows `VulkanOpaqueNt` is the current validated composition backend. Other
  platforms must map their handle family through compatibility probing and a
  distinct pool key before image reuse.
```

- [ ] **Step 2: Run encoding and whitespace checks**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Expected: both commands exit `0`.

- [ ] **Step 3: Run targeted native builds and smokes on MSVC**

Run:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug --target asharia-editor"
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
```

Expected: build succeeds and all four smoke commands exit `0`.

- [ ] **Step 4: Run targeted native builds and smokes on ClangCL**

Run:

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug --target asharia-editor"
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
```

Expected: build succeeds and all four smoke commands exit `0`.

- [ ] **Step 5: Run Studio managed tests**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "SceneView|ViewportNative|Composition"
```

Expected: test run exits `0`.

- [ ] **Step 6: Run the Vulkan review helper**

Run:

```powershell
python C:/Users/C66/.codex/skills/vulkan-cpp23-engineering/scripts/review_vulkan_cpp.py . --fail-on warning
```

Expected: command exits `0`, or each reported warning is investigated and fixed before PR.

- [ ] **Step 7: Commit**

```powershell
git add docs/architecture/flow.md
git commit -m "docs: document studio shared image pool flow"
```

---

### Task 7: Final PR Gate

**Files:**
- Modify only files changed by Tasks 1 through 6.

**Interfaces:**
- Consumes: all implementation and documentation commits.
- Produces: final evidence for review and PR creation.

- [ ] **Step 1: Run the full pre-PR native gate**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1
git diff --check
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
python C:/Users/C66/.codex/skills/vulkan-cpp23-engineering/scripts/review_vulkan_cpp.py . --fail-on warning
```

Expected: all commands exit `0`.

- [ ] **Step 2: Run final editor viewport smokes**

Run:

```powershell
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
```

Expected: all commands exit `0`.

- [ ] **Step 3: Run final Studio test and launch check**

Run:

```powershell
dotnet build apps\studio\Editor.csproj -c Debug
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "SceneView|ViewportNative|Composition"
```

Expected: both commands exit `0`.

Launch `apps\studio\bin\Debug\net10.0\Editor.exe`, open Scene View, wait for at least one native present attempt, then close the app from the UI. Expected: the process exits without `DllNotFoundException` and without the CLR thread-state assertion seen in the previous slice.

- [ ] **Step 4: Review changed files**

Run:

```powershell
git status --short
git diff --stat origin/main...HEAD
git diff --check
```

Expected: only intentional tracked files are modified; untracked local files such as `apps/studio/.vs/` and `qodana.yaml` remain unstaged.

- [ ] **Step 5: Prepare PR notes**

Use this PR summary:

```markdown
## Summary
- add a producer-local shared external image pool for Studio native viewport packets
- expose v2 runtime diagnostics counters for pool acquire/create/reuse/release state
- extend native viewport smoke coverage for same-size reuse and resize allocation
- document the Studio composition ownership boundary and platform handle-family reservation

## Validation
- powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
- powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1
- git diff --check
- cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
- cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
- build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
- build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
- build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
- build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
- build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
- build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
- build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
- build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
- dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "SceneView|ViewportNative|Composition"
- python C:/Users/C66/.codex/skills/vulkan-cpp23-engineering/scripts/review_vulkan_cpp.py . --fail-on warning
```

Expected: PR stays draft if the Studio launch check or any validation command fails.
