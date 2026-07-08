# Studio Shared External Image Pool Design

## Decision

Implement B-1 as a producer-local shared external image pool for the existing
Option B path: Avalonia Composition GPU interop. This keeps the formal path GPU
native-to-Avalonia and does not reintroduce CPU readback.

The pool belongs to `EditorSharedViewportRenderProducer` in `apps/editor`. It
reuses `VulkanExternalImage` objects after a present packet has been released
and its submitted GPU work has completed. Each packet still exports fresh Win32
handles and closes those handles during packet release.

The first implementation targets Windows because the current Avalonia
composition request uses `EditorViewportNativeHandleType_VulkanOpaqueNt`. The
design still keeps platform adaptation at the ABI/export boundary: the pool is
keyed by external handle family, but it does not include `windows.h`, does not
close OS handles, and does not assume that every future backend uses Win32 NT
handles.

## Context

PR #214 added the native render producer boundary. The current producer creates
these resources for every present packet:

- `BasicFullscreenTextureRenderer`
- `VulkanExternalImage`
- two `VulkanExternalSemaphore` instances
- command pool, command buffer, and fence
- exported Win32 image/semaphore handles

This is correct but expensive. The next safe reduction is external image reuse.
Renderer persistence and semaphore reuse have separate lifetime risks and stay
out of this slice.

Vulkan Win32 external memory and semaphore export APIs return handles owned by
the application. The application must close each successful exported handle when
it is no longer needed. Therefore, image pooling may reuse the Vulkan image and
allocation, but it must not reuse the exported packet handles themselves.

## Goals

- Reduce per-frame `VulkanExternalImage` and VMA allocation churn in the Studio
  shared viewport path.
- Keep resource ownership below `EditorSharedViewportRuntime` and inside the
  native producer boundary.
- Preserve the managed Avalonia import/release contract.
- Add smoke-visible counters proving same-size image reuse and resize allocation
  behavior.
- Keep the change PR-sized and independently verifiable.

## Non-Goals

- No CPU readback fallback work.
- No persistent `BasicFullscreenTextureRenderer` reuse.
- No producer-owned frame epoch for descriptor/transient lifetime.
- No semaphore pool.
- No command pool or fence pool.
- No public `rhi_vulkan` external image pool API.
- No managed Studio composition behavior change.
- No multi-viewport frame pacing work.
- No cross-platform composition backend implementation. This slice preserves
  extension points for other platforms, but validates the current Windows path.

## Architecture

Add `EditorSharedViewportExternalImagePool` as a private editor-native helper:

- Header: `apps/editor/src/editor_shared_viewport_external_image_pool.hpp`
- Source: `apps/editor/src/editor_shared_viewport_external_image_pool.cpp`
- Owner: `EditorSharedViewportRenderProducer`
- Dependencies: `VulkanExternalImage`, `VmaAllocator`, `VkDevice`

The pool key is:

```cpp
struct EditorSharedViewportExternalImageKey {
    std::uint32_t imageHandleType{EditorViewportNativeHandleType_VulkanOpaqueNt};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkExtent2D extent{};
    VkImageUsageFlags usage{};
    VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
};
```

`imageHandleType` is part of the key even though this slice only creates opaque
Win32/NT external images. Future platform work can map additional ABI handle
families to matching Vulkan external memory handle types without reusing an
incompatible image allocation.

The pool exposes an acquire-only public surface and returns a move-only lease:

```cpp
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
    EditorSharedViewportExternalImageLease(const EditorSharedViewportExternalImageLease&) = delete;
    EditorSharedViewportExternalImageLease& operator=(const EditorSharedViewportExternalImageLease&) = delete;
    EditorSharedViewportExternalImageLease(EditorSharedViewportExternalImageLease&&) noexcept;
    EditorSharedViewportExternalImageLease& operator=(EditorSharedViewportExternalImageLease&&) noexcept;
    ~EditorSharedViewportExternalImageLease();

    [[nodiscard]] VulkanExternalImage& image();
    [[nodiscard]] const VulkanExternalImage& image() const;
    [[nodiscard]] bool hasImage() const;

private:
    friend class EditorSharedViewportExternalImagePool;
};

class EditorSharedViewportExternalImagePool final {
public:
    [[nodiscard]] Result<EditorSharedViewportExternalImageLease>
    acquire(const VulkanExternalImageDesc& desc);

    [[nodiscard]] EditorSharedViewportExternalImagePoolStats stats() const;
};
```

The lease owns the checked-out image while the packet is outstanding. Its
destructor returns the image to a shared pool state. The shared state is
mutex-protected so concurrent packet releases cannot corrupt the available list.

## Data Flow

Acquire:

1. `EditorSharedViewportRenderProducer::renderSceneViewFrame` asks the pool for
   an external image matching the requested present extent.
2. The pool returns a reused available image when key-compatible; otherwise it
   creates a new `VulkanExternalImage`.
3. The producer records RenderView into the lease image and submits the command
   buffer.
4. The producer exports fresh Win32 handles for the lease image and semaphores.
5. The producer returns a present packet containing the native packet pointer
   and exported handles.

Release:

1. Managed Studio calls `editor_viewport_release_present_packet` after the
   Avalonia composition update path finishes or fails.
2. `EditorSharedViewportRuntime` removes the packet from `outstandingPackets_`.
3. Packet destruction waits for the packet fence, closes all exported Win32
   handles, destroys per-packet command/semaphore resources, and then lets the
   image lease return the image to the pool.
4. Runtime shutdown still waits until outstanding packets and release operations
   have drained before destroying the producer and Vulkan context.

## Safety Invariants

- A pooled image is never available while any packet still owns its lease.
- A pooled image is never reused before packet release has closed that packet's
  exported image handle.
- A pooled image is never reused before the packet fence reports GPU completion.
- The producer and pool are destroyed only after runtime outstanding packet
  tracking and `releasingPacketCount_` drain.
- Every successful Win32 handle export is paired with exactly one `CloseHandle`
  call in packet release.
- OS handle ownership stays at the packet/ABI boundary. The pool owns Vulkan
  image resources only and never closes or stores exported OS handles.
- The pool does not infer compositor progress from the Vulkan fence; compositor
  progress is represented by managed packet release.
- Reusing an image starts the next RenderView import from
  `RenderGraphImageState::Undefined`, so previous contents are discarded.
- Resize creates a new pool key. Old-size images remain available for future
  matching requests until producer shutdown.

## ABI And Stats

Do not extend `EditorViewportNativeRuntimeStats` in place for pool counters.
`editor_viewport_query_runtime_stats` has no input struct size, so blindly
appending fields would be unsafe for an older caller compiled with a smaller
struct.

Add a v2 diagnostics ABI for pool counters:

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

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v2(EditorViewportNativeRuntimeStatsV2* stats);
```

The existing v1 stats function remains unchanged. Native smoke uses v2 when
asserting external image pool counters.

The existing `EditorViewportNativeHandleType` enum remains the platform-facing
composition contract. This slice keeps `VulkanOpaqueNt` as the only supported
value, but image pool acquisition carries that handle type through the key so a
future FD-based Vulkan handle family or other platform-specific family cannot
accidentally reuse a Win32-compatible image.

## Error Handling

- Invalid image desc inputs fail before pool lookup.
- Image creation failures preserve the Vulkan error context from
  `VulkanExternalImage::create`.
- If render recording or handle export fails after image acquisition, normal
  packet cleanup returns the lease to the pool.
- If a lease is moved from, its destructor performs no pool operation.
- If the pool shared state has already moved, acquiring from it fails with a
  Vulkan-domain error rather than silently creating detached resources.

## Testing Strategy

Update `--smoke-editor-viewport-native`:

- Acquire and release a first `320x180` packet.
- Acquire and release a second `320x180` packet.
- Query runtime stats v2 and require:
  - `externalImagesCreated == 1`
  - `externalImagesReused >= 1`
  - `externalImagesReleased >= 1`
  - `externalImagesAvailable >= 1`
  - `externalImagesLeased == 0` after releases
- Acquire a resized `640x360` packet.
- Query runtime stats v2 and require `externalImagesCreated == 2`.

Regression gates:

- `cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug --target asharia-editor"`
- `build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native`
- `dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "SceneView|ViewportNative|Composition"`
- Full native and Studio gates before PR, matching `docs/workflow/review.md`.

## Documentation Impact

Update `docs/architecture/flow.md` after implementation to show that the native
render producer now owns a producer-local external image pool beneath the
runtime boundary. The docs must state that this is not the public
`rhi_vulkan` transient image pool and that Win32 handles remain per-packet.
The docs must also state that Windows is the current validated backend and that
other platform handle families require explicit compatibility probing and pool
keys before reuse.

## Follow-Up Slices

- B-2: producer frame epoch for descriptor/transient lifetime without
  `VulkanFrameLoop`.
- B-3: persistent `BasicFullscreenTextureRenderer` reuse after the epoch design
  is in place.
- Optional later: semaphore/command resource pooling if smoke counters prove
  image churn is no longer the dominant cost.
