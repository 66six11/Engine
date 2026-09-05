#include "editor_shared_viewport_runtime.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <exception>
#include <expected>
#include <future>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include "asharia/core/log.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

#include "editor_shared_viewport_dispatch.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] const EditorSharedViewportRuntime*& sharedViewportRenderThreadOwner() {
            static thread_local const EditorSharedViewportRuntime* owner{};
            return owner;
        }

        [[nodiscard]] asharia::Result<void>
        ensureSharedContextStorage(std::optional<asharia::VulkanContext>& contextStorage) {
            if (contextStorage) {
                return {};
            }

            auto context = asharia::VulkanContext::create(asharia::VulkanContextDesc{
                .applicationName = "Asharia Studio Shared Viewport",
                .requiredInstanceExtensions = {},
                .createSurface = {},
                // Studio runtime availability must not depend on a separately
                // installed Vulkan SDK validation layer. Dedicated renderer
                // validation gates still create strict validation contexts.
                .enableValidation = false,
                .debugLabels = asharia::VulkanDebugLabelMode::Optional,
                .requireVulkan14 = true,
                .externalInterop =
                    asharia::VulkanExternalInteropOptions{
                        .opaqueWin32Memory = true,
                        .opaqueWin32Semaphore = true,
                    },
            });
            if (!context) {
                return std::unexpected{std::move(context.error())};
            }

            contextStorage.emplace(std::move(*context));
            return {};
        }

        [[nodiscard]] std::unexpected<EditorSharedViewportRenderFrameError>
        renderFrameFailure(asharia::Error error) {
            return std::unexpected{EditorSharedViewportRenderFrameError{
                .kind = EditorSharedViewportRenderFrameErrorKind::RenderFailed,
                .error = std::move(error),
            }};
        }

        [[nodiscard]] std::unexpected<EditorSharedViewportRenderFrameError>
        renderFrameUnavailable(asharia::Error error) {
            return std::unexpected{EditorSharedViewportRenderFrameError{
                .kind = EditorSharedViewportRenderFrameErrorKind::Unavailable,
                .error = std::move(error),
            }};
        }

        [[nodiscard]] std::unexpected<EditorSharedViewportRenderFrameError> renderFrameBackpressure(
            std::string message = "Shared viewport present resources are still in use") {
            return std::unexpected{EditorSharedViewportRenderFrameError{
                .kind = EditorSharedViewportRenderFrameErrorKind::Backpressure,
                .error = vulkanError(std::move(message)),
            }};
        }

        [[nodiscard]] bool isTerminal(EditorSharedViewportRuntimeLifecycle lifecycle) {
            return lifecycle == EditorSharedViewportRuntimeLifecycle::Stopped ||
                   lifecycle == EditorSharedViewportRuntimeLifecycle::Faulted;
        }

    } // namespace

    EditorSharedViewportRuntime::RenderFramePacket
    EditorSharedViewportRuntime::RenderFramePacket::copyOf(EditorSharedViewportPresentDesc desc) {
        RenderFramePacket packet{
            .panelId = std::string{desc.panelId},
            .kind = desc.kind,
            .logicalExtent = desc.logicalExtent,
            .allocationExtent = desc.allocationExtent,
            .imageHandleFamily = desc.imageHandleFamily,
            .hasScene = desc.hasScene,
            .sceneRevision = desc.sceneRevision,
            .sessionId = desc.sessionId,
            .targetId = desc.targetId,
            .requestSequence = desc.requestSequence,
            .viewStateRevision = desc.viewStateRevision,
            .hasCamera = desc.hasCamera,
            .camera = desc.camera,
            .debugProxies = {},
            .authoredMeshes = {},
            .sceneRasterMode = desc.sceneRasterMode,
            .captureSceneMeshEvidence = desc.captureSceneMeshEvidence,
            .flashSentinelCorners = desc.flashSentinelCorners,
            .hasSelectionOutline = desc.hasSelectionOutline,
            .selectedObjectId = desc.selectedObjectId,
            .hasTransformGizmo = desc.hasTransformGizmo,
            .transformGizmoKind = desc.transformGizmoKind,
            .transformGizmoObjectId = desc.transformGizmoObjectId,
            .transformGizmoPosition = desc.transformGizmoPosition,
            .transformGizmoRotation = desc.transformGizmoRotation,
            .transformGizmoHoveredAxis = desc.transformGizmoHoveredAxis,
            .transformGizmoActiveAxis = desc.transformGizmoActiveAxis,
        };
        if (!desc.debugProxies.empty()) {
            packet.debugProxies.assign(desc.debugProxies.begin(), desc.debugProxies.end());
        }
        if (!desc.authoredMeshes.empty()) {
            packet.authoredMeshes.assign(desc.authoredMeshes.begin(), desc.authoredMeshes.end());
        }
        return packet;
    }

    EditorSharedViewportPresentDesc EditorSharedViewportRuntime::RenderFramePacket::view() const {
        return EditorSharedViewportPresentDesc{
            .panelId = panelId,
            .kind = kind,
            .logicalExtent = logicalExtent,
            .allocationExtent = allocationExtent,
            .imageHandleFamily = imageHandleFamily,
            .hasScene = hasScene,
            .sceneRevision = sceneRevision,
            .sessionId = sessionId,
            .targetId = targetId,
            .requestSequence = requestSequence,
            .viewStateRevision = viewStateRevision,
            .hasCamera = hasCamera,
            .camera = camera,
            .debugProxies = debugProxies,
            .authoredMeshes = authoredMeshes,
            .sceneRasterMode = sceneRasterMode,
            .captureSceneMeshEvidence = captureSceneMeshEvidence,
            .flashSentinelCorners = flashSentinelCorners,
            .hasSelectionOutline = hasSelectionOutline,
            .selectedObjectId = selectedObjectId,
            .hasTransformGizmo = hasTransformGizmo,
            .transformGizmoKind = transformGizmoKind,
            .transformGizmoObjectId = transformGizmoObjectId,
            .transformGizmoPosition = transformGizmoPosition,
            .transformGizmoRotation = transformGizmoRotation,
            .transformGizmoHoveredAxis = transformGizmoHoveredAxis,
            .transformGizmoActiveAxis = transformGizmoActiveAxis,
        };
    }

    EditorSharedViewportRuntime::EditorSharedViewportRuntime() {
        publishedStats_.maxOutstandingPackets = kMaxOutstandingPackets;
        publishedStats_.maxQueuedRenderCommands = kMaxQueuedRenderCommands;
    }

    EditorSharedViewportRuntime& EditorSharedViewportRuntime::instance() {
        // The aligned static storage owns the process-lifetime runtime without
        // registering a destructor. Normal shutdown still drains and joins the
        // render thread, while quarantined GPU work is left to process teardown.
        alignas(EditorSharedViewportRuntime) static std::array<std::byte,
                                                               sizeof(EditorSharedViewportRuntime)>
            runtimeStorage{};
        // This is non-allocating placement construction into the static owner.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        static EditorSharedViewportRuntime& runtime =
            *::new (static_cast<void*>(runtimeStorage.data())) EditorSharedViewportRuntime;
        return runtime;
    }

    asharia::Result<EditorSharedViewportStreamId>
    EditorSharedViewportRuntime::openStream(bool supportsWireframe) {
        if (isOnRenderThread()) {
            return std::unexpected{
                vulkanError("Shared viewport stream API cannot re-enter its render thread")};
        }
        auto threadStarted = ensureRenderThreadStarted();
        if (!threadStarted) {
            return std::unexpected{std::move(threadStarted.error())};
        }
        if (shutdownRequestedByCaller_.load(std::memory_order_acquire)) {
            return std::unexpected{vulkanError("Shared viewport runtime has shut down")};
        }

        try {
            const EditorSharedViewportStreamId streamId =
                nextStreamId_.fetch_add(1U, std::memory_order_relaxed);
            if (streamId == 0U) {
                return std::unexpected{vulkanError("Shared viewport stream id space exhausted")};
            }
            auto stream = std::make_shared<StreamState>();
            stream->slots.reserve(kMaxStreamSlots);
            stream->supportsWireframe = supportsWireframe;
            {
                std::lock_guard lock{streamsMutex_};
                streams_.emplace(streamId, std::move(stream));
            }
            return streamId;
        } catch (const std::bad_alloc&) {
            return std::unexpected{vulkanError("Shared viewport stream allocation failed")};
        }
    }

    asharia::Result<void> EditorSharedViewportRuntime::validateSceneRasterMode(
        EditorSharedViewportStreamId streamId, EditorSharedViewportSceneRasterMode rasterMode) {
        auto stream = findStream(streamId);
        if (!stream) {
            return std::unexpected{vulkanError("Shared viewport stream does not exist")};
        }

        std::lock_guard lock{stream->mutex};
        if (stream->closeRequested || stream->closed || stream->faulted) {
            return std::unexpected{vulkanError("Shared viewport stream is not accepting frames")};
        }
        if (rasterMode == EditorSharedViewportSceneRasterMode::Wireframe &&
            !stream->supportsWireframe) {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::Vulkan,
                static_cast<int>(VK_ERROR_FEATURE_NOT_PRESENT),
                "Shared viewport wireframe is unavailable because fillModeNonSolid was not "
                "enabled on the stream device",
            }};
        }
        return {};
    }

    asharia::Result<void>
    EditorSharedViewportRuntime::submitLatest(EditorSharedViewportStreamId streamId,
                                              EditorSharedViewportPresentDesc desc) {
        if (desc.logicalExtent.width == 0U || desc.logicalExtent.height == 0U ||
            desc.allocationExtent.width < desc.logicalExtent.width ||
            desc.allocationExtent.height < desc.logicalExtent.height) {
            return std::unexpected{
                vulkanError("Cannot submit a shared viewport stream frame with invalid extents")};
        }
        auto rasterModeValid = validateSceneRasterMode(streamId, desc.sceneRasterMode);
        if (!rasterModeValid) {
            return std::unexpected{std::move(rasterModeValid.error())};
        }
        auto stream = findStream(streamId);
        if (!stream) {
            return std::unexpected{vulkanError("Shared viewport stream does not exist")};
        }

        try {
            RenderFramePacket packet = RenderFramePacket::copyOf(desc);
            {
                std::lock_guard lock{stream->mutex};
                if (stream->closeRequested || stream->closed || stream->faulted) {
                    return std::unexpected{
                        vulkanError("Shared viewport stream is not accepting frames")};
                }
                const bool allocationExtentChanged =
                    stream->allocationExtent
                        .transform([&desc](const EditorExtent2D& current) {
                            return current.width != desc.allocationExtent.width ||
                                   current.height != desc.allocationExtent.height;
                        })
                        .value_or(false);
                if (allocationExtentChanged) {
                    return std::unexpected{vulkanError(
                        "Shared viewport stream allocation extent cannot change in place")};
                }
                stream->allocationExtent = desc.allocationExtent;
                if (stream->pendingLatest) {
                    ++stream->coalescedRequests;
                }
                stream->pendingLatest = std::move(packet);
                ++stream->submittedRequests;
            }
            queueReady_.notify_one();
            return {};
        } catch (const std::bad_alloc&) {
            return std::unexpected{
                vulkanError("Shared viewport stream frame snapshot allocation failed")};
        }
    }

    asharia::Result<std::optional<EditorSharedViewportReadyFrame>>
    EditorSharedViewportRuntime::tryTakeReady(EditorSharedViewportStreamId streamId) {
        auto stream = findStream(streamId);
        if (!stream) {
            return std::unexpected{vulkanError("Shared viewport stream does not exist")};
        }

        std::optional<EditorSharedViewportReadyFrame> taken;
        {
            std::lock_guard lock{stream->mutex};
            if (!stream->readyFrame) {
                return taken;
            }
            StreamReadyFrame ready = *stream->readyFrame;
            if (ready.slotIndex >= stream->slots.size()) {
                stream->faulted = true;
                ++stream->stateRevision;
                stream->stateChanged.notify_all();
                return std::unexpected{vulkanError("Shared viewport ready slot index is invalid")};
            }
            StreamSlot& slot = stream->slots.at(ready.slotIndex);
            if (slot.phase != StreamSlotPhase::Ready || slot.nativeSlot == nullptr) {
                stream->faulted = true;
                ++stream->stateRevision;
                stream->stateChanged.notify_all();
                return std::unexpected{vulkanError("Shared viewport ready slot state is invalid")};
            }

            slot.phase = StreamSlotPhase::Presented;
            slot.importExposed = true;
            slot.importReleased = false;
            stream->readyFrame.reset();
            taken.emplace(ready.frame);
        }

        // Removing the single ready frame can make an already coalesced pending-latest request
        // dispatchable. Wake the render owner even when the completed producer fence no longer
        // keeps its 1 ms retirement poll active.
        queueReady_.notify_one();
        return taken;
    }

    asharia::Result<void> EditorSharedViewportRuntime::completeFrame(
        EditorSharedViewportStreamId streamId, void* nativeSlot,
        EditorSharedViewportPresentCompletionKind completionKind) {
        if (nativeSlot == nullptr) {
            return std::unexpected{vulkanError("Cannot complete a null shared viewport slot")};
        }
        auto stream = findStream(streamId);
        if (!stream) {
            return std::unexpected{vulkanError("Shared viewport stream does not exist")};
        }

        {
            std::lock_guard lock{stream->mutex};
            const auto slot =
                std::ranges::find_if(stream->slots, [nativeSlot](const StreamSlot& item) {
                    return item.nativeSlot == nativeSlot;
                });
            if (slot == stream->slots.end() || slot->phase != StreamSlotPhase::Presented) {
                return std::unexpected{
                    vulkanError("Shared viewport frame completion does not own a presented slot")};
            }
            slot->completionKind = completionKind;
            slot->phase = StreamSlotPhase::Completing;
        }
        queueReady_.notify_one();
        return {};
    }

    asharia::Result<void>
    EditorSharedViewportRuntime::releaseSlotImport(EditorSharedViewportStreamId streamId,
                                                   void* nativeSlot) {
        if (nativeSlot == nullptr) {
            return std::unexpected{
                vulkanError("Cannot release imports for a null shared viewport slot")};
        }
        auto stream = findStream(streamId);
        if (!stream) {
            return std::unexpected{vulkanError("Shared viewport stream does not exist")};
        }

        {
            std::lock_guard lock{stream->mutex};
            const auto slot =
                std::ranges::find_if(stream->slots, [nativeSlot](const StreamSlot& item) {
                    return item.nativeSlot == nativeSlot;
                });
            if (slot == stream->slots.end() || !slot->importExposed) {
                return std::unexpected{
                    vulkanError("Shared viewport slot imports were not exposed to the caller")};
            }
            slot->importReleased = true;
        }
        queueReady_.notify_one();
        return {};
    }

    asharia::Result<void>
    EditorSharedViewportRuntime::requestCloseStream(EditorSharedViewportStreamId streamId) {
        auto stream = findStream(streamId);
        if (!stream) {
            return std::unexpected{vulkanError("Shared viewport stream does not exist")};
        }
        {
            std::lock_guard lock{stream->mutex};
            stream->closeRequested = true;
            ++stream->stateRevision;
            stream->stateChanged.notify_all();
            stream->pendingLatest.reset();
        }
        queueReady_.notify_one();
        return {};
    }

    asharia::Result<EditorSharedViewportStreamSnapshot>
    EditorSharedViewportRuntime::pollStream(EditorSharedViewportStreamId streamId) {
        auto stream = findStream(streamId);
        if (!stream) {
            return std::unexpected{vulkanError("Shared viewport stream does not exist")};
        }

        std::lock_guard lock{stream->mutex};
        std::size_t presentedSlotCount{};
        for (const StreamSlot& slot : stream->slots) {
            if (slot.phase == StreamSlotPhase::Presented ||
                slot.phase == StreamSlotPhase::Completing) {
                ++presentedSlotCount;
            }
        }
        EditorSharedViewportStreamLifecycle lifecycle = EditorSharedViewportStreamLifecycle::Open;
        if (stream->faulted) {
            lifecycle = EditorSharedViewportStreamLifecycle::Faulted;
        } else if (stream->closed) {
            lifecycle = EditorSharedViewportStreamLifecycle::Closed;
        } else if (stream->closeRequested) {
            lifecycle = EditorSharedViewportStreamLifecycle::Closing;
        }
        return EditorSharedViewportStreamSnapshot{
            .lifecycle = lifecycle,
            .hasPendingLatest = stream->pendingLatest.has_value(),
            .hasReadyFrame = stream->readyFrame.has_value(),
            .renderExecuting = stream->renderExecuting,
            .slotCount = stream->slots.size(),
            .presentedSlotCount = presentedSlotCount,
            .submittedRequests = stream->submittedRequests,
            .coalescedRequests = stream->coalescedRequests,
            .renderedFrames = stream->renderedFrames,
            .stateRevision = stream->stateRevision,
        };
    }

    asharia::Result<void>
    EditorSharedViewportRuntime::destroyClosedStream(EditorSharedViewportStreamId streamId) {
        std::lock_guard lock{streamsMutex_};
        const auto stream = streams_.find(streamId);
        if (stream == streams_.end()) {
            return std::unexpected{vulkanError("Shared viewport stream does not exist")};
        }
        {
            std::lock_guard streamLock{stream->second->mutex};
            if (!stream->second->closed || stream->second->faulted) {
                return std::unexpected{
                    vulkanError("Shared viewport stream has not closed cleanly")};
            }
        }
        streams_.erase(stream);
        return {};
    }

    asharia::Result<void>
    EditorSharedViewportRuntime::waitForStreamChange(EditorSharedViewportStreamId streamId,
                                                     std::chrono::milliseconds timeout,
                                                     std::uint64_t observedRevision) {
        if (isOnRenderThread() || timeout < std::chrono::milliseconds::zero() ||
            timeout > std::chrono::milliseconds{50}) {
            return std::unexpected{vulkanError("Invalid shared viewport wait context or timeout")};
        }
        // Retain the stream independently of registry identity while the mutex is released.
        auto stream = findStream(streamId);
        if (!stream) {
            return std::unexpected{vulkanError("Shared viewport stream does not exist")};
        }
        std::unique_lock lock{stream->mutex};
        stream->stateChanged.wait_for(lock, timeout, [&] {
            return stream->stateRevision != observedRevision || stream->closed || stream->faulted;
        });
        return {};
    }

    std::shared_ptr<EditorSharedViewportRuntime::StreamState>
    EditorSharedViewportRuntime::findStream(EditorSharedViewportStreamId streamId) const {
        std::lock_guard lock{streamsMutex_};
        const auto stream = streams_.find(streamId);
        return stream != streams_.end() ? stream->second : nullptr;
    }

    asharia::Result<EditorSharedViewportDeviceSnapshot>
    EditorSharedViewportRuntime::ensureDeviceSnapshot() {
        if (isOnRenderThread()) {
            return std::unexpected{
                vulkanError("Shared viewport synchronous API cannot re-enter its render thread")};
        }
        auto threadStarted = ensureRenderThreadStarted();
        if (!threadStarted) {
            return std::unexpected{std::move(threadStarted.error())};
        }
        if (shutdownRequestedByCaller_.load(std::memory_order_acquire)) {
            return std::unexpected{vulkanError("Shared viewport runtime has shut down")};
        }

        {
            std::lock_guard lock{publishedStateMutex_};
            if (publishedDeviceSnapshot_) {
                return *publishedDeviceSnapshot_;
            }
        }

        try {
            std::packaged_task<asharia::Result<EditorSharedViewportDeviceSnapshot>()> task{[this] {
                auto result = ensureDeviceSnapshotOnRenderThread();
                if (result) {
                    std::lock_guard lock{publishedStateMutex_};
                    publishedDeviceSnapshot_ = *result;
                }
                publishRuntimeStatsOnRenderThread();
                return result;
            }};
            auto result = task.get_future();
            std::packaged_task<void()> work{[task = std::move(task)]() mutable { task(); }};
            if (!enqueueControlWork(std::move(work))) {
                return std::unexpected{vulkanError("Shared viewport runtime has shut down")};
            }
            return result.get();
        } catch (const std::bad_alloc&) {
            return std::unexpected{vulkanError("Shared viewport device request allocation failed")};
        } catch (const std::exception& exception) {
            return std::unexpected{vulkanError(
                std::string{"Shared viewport device request failed: "} + exception.what())};
        }
    }

    EditorSharedViewportRenderFrameResult
    EditorSharedViewportRuntime::renderSceneViewFrame(EditorSharedViewportPresentDesc desc) {
        if (isOnRenderThread()) {
            return renderFrameUnavailable(
                vulkanError("Shared viewport synchronous API cannot re-enter its render thread"));
        }
        auto threadStarted = ensureRenderThreadStarted();
        if (!threadStarted) {
            return renderFrameUnavailable(std::move(threadStarted.error()));
        }
        if (desc.logicalExtent.width == 0 || desc.logicalExtent.height == 0 ||
            desc.allocationExtent.width < desc.logicalExtent.width ||
            desc.allocationExtent.height < desc.logicalExtent.height) {
            return renderFrameFailure(
                vulkanError("Cannot render a shared viewport frame for invalid extents"));
        }
        if (shutdownRequestedByCaller_.load(std::memory_order_acquire)) {
            return renderFrameUnavailable(vulkanError("Shared viewport runtime has shut down"));
        }

        try {
            RenderFramePacket packet = RenderFramePacket::copyOf(desc);
            std::packaged_task<EditorSharedViewportRenderFrameResult()> task{
                [this, packet = std::move(packet)] {
                    auto result = renderSceneViewFrameOnRenderThread(packet);
                    publishRuntimeStatsOnRenderThread();
                    return result;
                }};
            auto result = task.get_future();
            std::packaged_task<void()> work{[task = std::move(task)]() mutable { task(); }};
            if (!enqueueRenderWork(std::move(work))) {
                if (shutdownRequestedByCaller_.load(std::memory_order_acquire)) {
                    return renderFrameUnavailable(
                        vulkanError("Shared viewport runtime has shut down"));
                }
                return renderFrameBackpressure("Shared viewport render queue is full");
            }
            return result.get();
        } catch (const std::bad_alloc&) {
            return renderFrameFailure(vulkanError("Shared viewport frame allocation failed"));
        } catch (const std::exception& exception) {
            return renderFrameFailure(vulkanError(
                std::string{"Shared viewport frame dispatch failed: "} + exception.what()));
        }
    }

    EditorSharedViewportRenderFrameResult
    EditorSharedViewportRuntime::createPresentSlot(EditorSharedViewportPresentDesc desc) {
        if (isOnRenderThread()) {
            return renderFrameUnavailable(
                vulkanError("Shared viewport synchronous API cannot re-enter its render thread"));
        }
        auto threadStarted = ensureRenderThreadStarted();
        if (!threadStarted) {
            return renderFrameUnavailable(std::move(threadStarted.error()));
        }
        if (desc.logicalExtent.width == 0 || desc.logicalExtent.height == 0 ||
            desc.allocationExtent.width < desc.logicalExtent.width ||
            desc.allocationExtent.height < desc.logicalExtent.height) {
            return renderFrameFailure(
                vulkanError("Cannot create a shared viewport present slot for invalid extents"));
        }
        if (shutdownRequestedByCaller_.load(std::memory_order_acquire)) {
            return renderFrameUnavailable(vulkanError("Shared viewport runtime has shut down"));
        }

        try {
            RenderFramePacket packet = RenderFramePacket::copyOf(desc);
            std::packaged_task<EditorSharedViewportRenderFrameResult()> task{
                [this, packet = std::move(packet)] {
                    auto result = createPresentSlotOnRenderThread(packet);
                    publishRuntimeStatsOnRenderThread();
                    return result;
                }};
            auto result = task.get_future();
            std::packaged_task<void()> work{[task = std::move(task)]() mutable { task(); }};
            if (!enqueueRenderWork(std::move(work))) {
                if (shutdownRequestedByCaller_.load(std::memory_order_acquire)) {
                    return renderFrameUnavailable(
                        vulkanError("Shared viewport runtime has shut down"));
                }
                return renderFrameBackpressure("Shared viewport render queue is full");
            }
            return result.get();
        } catch (const std::bad_alloc&) {
            return renderFrameFailure(
                vulkanError("Shared viewport present slot allocation failed"));
        } catch (const std::exception& exception) {
            return renderFrameFailure(vulkanError(
                std::string{"Shared viewport slot dispatch failed: "} + exception.what()));
        }
    }

    EditorSharedViewportRenderFrameResult
    EditorSharedViewportRuntime::renderPresentSlot(void* nativeSlot,
                                                   EditorSharedViewportPresentDesc desc) {
        if (isOnRenderThread()) {
            return renderFrameUnavailable(
                vulkanError("Shared viewport synchronous API cannot re-enter its render thread"));
        }
        auto threadStarted = ensureRenderThreadStarted();
        if (!threadStarted) {
            return renderFrameUnavailable(std::move(threadStarted.error()));
        }
        if (nativeSlot == nullptr) {
            return renderFrameFailure(
                vulkanError("Cannot render a null shared viewport present slot"));
        }
        if (desc.logicalExtent.width == 0 || desc.logicalExtent.height == 0 ||
            desc.allocationExtent.width < desc.logicalExtent.width ||
            desc.allocationExtent.height < desc.logicalExtent.height) {
            return renderFrameFailure(
                vulkanError("Cannot render a shared viewport present slot for invalid extents"));
        }
        if (shutdownRequestedByCaller_.load(std::memory_order_acquire)) {
            return renderFrameUnavailable(vulkanError("Shared viewport runtime has shut down"));
        }

        try {
            RenderFramePacket packet = RenderFramePacket::copyOf(desc);
            std::packaged_task<EditorSharedViewportRenderFrameResult()> task{
                [this, nativeSlot, packet = std::move(packet)] {
                    auto result = renderPresentSlotOnRenderThread(nativeSlot, packet);
                    publishRuntimeStatsOnRenderThread();
                    return result;
                }};
            auto result = task.get_future();
            std::packaged_task<void()> work{[task = std::move(task)]() mutable { task(); }};
            if (!enqueueRenderWork(std::move(work))) {
                if (shutdownRequestedByCaller_.load(std::memory_order_acquire)) {
                    return renderFrameUnavailable(
                        vulkanError("Shared viewport runtime has shut down"));
                }
                return renderFrameBackpressure("Shared viewport render queue is full");
            }
            return result.get();
        } catch (const std::bad_alloc&) {
            return renderFrameFailure(
                vulkanError("Shared viewport present slot render allocation failed"));
        } catch (const std::exception& exception) {
            return renderFrameFailure(vulkanError(
                std::string{"Shared viewport slot render dispatch failed: "} + exception.what()));
        }
    }

    asharia::Result<void> EditorSharedViewportRuntime::releasePresentPacket(
        void* nativePacket, EditorSharedViewportPresentCompletionKind completionKind) {
        if (nativePacket == nullptr) {
            return {};
        }
        if (isOnRenderThread()) {
            return std::unexpected{
                vulkanError("Shared viewport packet release cannot re-enter its render thread")};
        }

        auto threadStarted = ensureRenderThreadStarted();
        if (!threadStarted) {
            return std::unexpected{std::move(threadStarted.error())};
        }

        try {
            std::packaged_task<asharia::Result<void>()> task{[this, nativePacket, completionKind] {
                auto released = releasePresentPacketOnRenderThread(nativePacket, completionKind);
                if (shutdownRequested_) {
                    [[maybe_unused]] const bool finished = tryFinishShutdownOnRenderThread();
                }
                publishRuntimeStatsOnRenderThread();
                return released;
            }};
            auto completed = task.get_future();
            std::packaged_task<void()> work{[task = std::move(task)]() mutable { task(); }};
            if (!enqueueReleaseWork(std::move(work))) {
                return std::unexpected{
                    vulkanError("Shared viewport packet release arrived after the render thread "
                                "stopped")};
            }
            auto released = completed.get();
            if (shutdownRequestedByCaller_.load(std::memory_order_acquire) &&
                outstandingPacketCount_.load(std::memory_order_acquire) == 0U) {
                std::unique_lock lock{queueMutex_};
                lifecycleChanged_.wait(lock, [this] {
                    return isTerminal(lifecycle_.load(std::memory_order_acquire));
                });
            }
            joinRenderThreadIfTerminal();
            return released;
        } catch (const std::exception& exception) {
            return std::unexpected{vulkanError(
                std::string{"Shared viewport packet release failed: "} + exception.what())};
        }
    }

    void EditorSharedViewportRuntime::shutdown() {
        if (isOnRenderThread()) {
            shutdownRequestedByCaller_.store(true, std::memory_order_release);
            queueReady_.notify_one();
            return;
        }
        if (lifecycle_.load(std::memory_order_acquire) ==
            EditorSharedViewportRuntimeLifecycle::Starting) {
            auto threadStarted = ensureRenderThreadStarted();
            if (!threadStarted) {
                logError(threadStarted.error().message);
                return;
            }
        }
        shutdownRequestedByCaller_.store(true, std::memory_order_release);
        queueReady_.notify_one();
        queueSpaceAvailable_.notify_all();

        {
            std::unique_lock lock{queueMutex_};
            lifecycleChanged_.wait(lock, [this] {
                const EditorSharedViewportRuntimeLifecycle state =
                    lifecycle_.load(std::memory_order_acquire);
                return isTerminal(state) ||
                       (state == EditorSharedViewportRuntimeLifecycle::Draining &&
                        outstandingPacketCount_.load(std::memory_order_acquire) != 0U);
            });
        }
        joinRenderThreadIfTerminal();
    }

    EditorSharedViewportRuntimeStats EditorSharedViewportRuntime::stats() {
        EditorSharedViewportRuntimeStats snapshot;
        {
            std::lock_guard lock{publishedStateMutex_};
            snapshot = publishedStats_;
        }
        {
            std::lock_guard lock{queueMutex_};
            snapshot.queuedRenderCommands = renderQueue_.size();
        }
        const std::uint64_t currentRenderQueueBackpressureHits =
            renderQueueBackpressureHits_.load(std::memory_order_relaxed);
        snapshot.renderQueueBackpressureHits = currentRenderQueueBackpressureHits;
        snapshot.maxObservedQueuedRenderCommands =
            maxObservedQueuedRenderCommands_.load(std::memory_order_relaxed);
        const EditorSharedViewportRuntimeLifecycle state =
            lifecycle_.load(std::memory_order_acquire);
        snapshot.lifecycle = state;
        snapshot.renderThreadRunning = state == EditorSharedViewportRuntimeLifecycle::Running ||
                                       state == EditorSharedViewportRuntimeLifecycle::Draining;
        snapshot.shutdownRequested = shutdownRequestedByCaller_.load(std::memory_order_acquire);
        {
            std::lock_guard lock{threadMutex_};
            snapshot.renderThreadJoined = !renderThread_.joinable();
        }
        return snapshot;
    }

    asharia::Result<EditorSharedViewportDeviceSnapshot>
    EditorSharedViewportRuntime::ensureDeviceSnapshotOnRenderThread() {
        assert(isOnRenderThread());
        ++renderThreadDispatches_;
        if (shutdownRequested_) {
            return std::unexpected{vulkanError("Shared viewport runtime has shut down")};
        }

        auto ensured = ensureSharedContextStorage(context_);
        if (!ensured) {
            return std::unexpected{std::move(ensured.error())};
        }
        if (!context_) {
            return std::unexpected{
                vulkanError("Shared viewport context creation completed without a context")};
        }

        const asharia::VulkanDeviceInfo& deviceInfo = context_->deviceInfo();
        return EditorSharedViewportDeviceSnapshot{
            .vendorId = deviceInfo.vendorId,
            .deviceId = deviceInfo.deviceId,
            .fillModeNonSolid = context_->capabilities().fillModeNonSolid,
            .identity = deviceInfo.identity,
        };
    }

    asharia::Result<EditorSharedViewportRenderProducer*>
    EditorSharedViewportRuntime::ensureRenderProducerOnRenderThread() {
        assert(isOnRenderThread());
        if (context_ == std::nullopt) {
            return std::unexpected{
                vulkanError("Cannot create shared viewport render producer without a context")};
        }

        if (renderProducer_) {
            return &*renderProducer_;
        }

        auto producer = EditorSharedViewportRenderProducer::create(*context_);
        if (!producer) {
            return std::unexpected{std::move(producer.error())};
        }

        renderProducer_.emplace(std::move(*producer));
        frameClock_.reset();
        ++producersCreated_;
        return &*renderProducer_;
    }

    EditorSharedViewportRenderFrameResult
    EditorSharedViewportRuntime::renderSceneViewFrameOnRenderThread(
        const RenderFramePacket& packet) {
        assert(isOnRenderThread());
        ++renderThreadDispatches_;
        if (shutdownRequested_) {
            return renderFrameUnavailable(vulkanError("Shared viewport runtime has shut down"));
        }

        if (!pollRetiringPacketsOnRenderThread()) {
            return renderFrameFailure(
                vulkanError("Shared viewport packet retirement failed; runtime is shutting down"));
        }
        if (outstandingLegacyPackets_ >= kMaxOutstandingLegacyPackets) {
            ++packetBackpressureHits_;
            return renderFrameBackpressure();
        }
        const std::optional<std::size_t> frameResourceIndex =
            availableFrameResourceIndexOnRenderThread();
        if (!frameResourceIndex) {
            ++packetBackpressureHits_;
            return renderFrameBackpressure();
        }

        auto ensured = ensureSharedContextStorage(context_);
        if (!ensured) {
            return renderFrameFailure(std::move(ensured.error()));
        }

        auto producer = ensureRenderProducerOnRenderThread();
        if (!producer) {
            return renderFrameFailure(std::move(producer.error()));
        }

        const std::uint64_t frameIndex = ++nextFrameIndex_;
        const auto sampledAt = EditorSharedViewportFrameClock::Clock::now();
        const BasicRenderViewFrameParams frameParams =
            frameClock_.frameParams(frameIndex, sampledAt);
        auto state =
            (*producer)->renderSceneViewFrame(frameParams, packet.view(), *frameResourceIndex);
        if (!state) {
            return renderFrameFailure(std::move(state.error()));
        }
        frameClock_.markRendered(sampledAt);

        EditorSharedViewportPacketState* statePtr = state->get();
        outstandingPackets_.insert(statePtr);
        outstandingPacketCount_.store(outstandingPackets_.size(), std::memory_order_release);
        ++outstandingLegacyPackets_;
        ++framesRendered_;
        ++packetsCreated_;
        [[maybe_unused]] EditorSharedViewportPacketState* const releasedState = state->release();
        return statePtr->toPresentPacket();
    }

    EditorSharedViewportRenderFrameResult
    EditorSharedViewportRuntime::createPresentSlotOnRenderThread(const RenderFramePacket& packet) {
        assert(isOnRenderThread());
        ++renderThreadDispatches_;
        if (shutdownRequested_) {
            return renderFrameUnavailable(vulkanError("Shared viewport runtime has shut down"));
        }

        if (!pollRetiringPacketsOnRenderThread()) {
            return renderFrameFailure(
                vulkanError("Shared viewport packet retirement failed; runtime is shutting down"));
        }
        const std::optional<std::size_t> frameResourceIndex =
            availableFrameResourceIndexOnRenderThread();
        if (!frameResourceIndex) {
            ++packetBackpressureHits_;
            return renderFrameBackpressure();
        }

        auto ensured = ensureSharedContextStorage(context_);
        if (!ensured) {
            return renderFrameFailure(std::move(ensured.error()));
        }
        auto producer = ensureRenderProducerOnRenderThread();
        if (!producer) {
            return renderFrameFailure(std::move(producer.error()));
        }
        auto retired = retireCompletedPresentSlotsOnRenderThread();
        if (!retired) {
            return renderFrameFailure(std::move(retired.error()));
        }

        const std::uint64_t frameIndex = ++nextFrameIndex_;
        const auto sampledAt = EditorSharedViewportFrameClock::Clock::now();
        const BasicRenderViewFrameParams frameParams =
            frameClock_.frameParams(frameIndex, sampledAt);
        auto state =
            (*producer)->createPresentSlot(frameParams, packet.view(), *frameResourceIndex);
        if (!state) {
            return renderFrameFailure(std::move(state.error()));
        }
        frameClock_.markRendered(sampledAt);

        EditorSharedViewportPacketState* statePtr = state->get();
        outstandingPackets_.insert(statePtr);
        outstandingPacketCount_.store(outstandingPackets_.size(), std::memory_order_release);
        ++framesRendered_;
        ++packetsCreated_;
        [[maybe_unused]] EditorSharedViewportPacketState* const releasedState = state->release();
        return statePtr->toPresentPacket();
    }

    EditorSharedViewportRenderFrameResult
    EditorSharedViewportRuntime::renderPresentSlotOnRenderThread(void* nativeSlot,
                                                                 const RenderFramePacket& packet) {
        assert(isOnRenderThread());
        ++renderThreadDispatches_;
        if (shutdownRequested_) {
            return renderFrameUnavailable(vulkanError("Shared viewport runtime has shut down"));
        }

        if (!pollRetiringPacketsOnRenderThread()) {
            return renderFrameFailure(
                vulkanError("Shared viewport packet retirement failed; runtime is shutting down"));
        }
        auto* state = static_cast<EditorSharedViewportPacketState*>(nativeSlot);
        if (!outstandingPackets_.contains(state) || !state->reusable) {
            return renderFrameFailure(
                vulkanError("Shared viewport present slot is not owned by the runtime"));
        }
        auto producer = ensureRenderProducerOnRenderThread();
        if (!producer) {
            return renderFrameFailure(std::move(producer.error()));
        }
        auto retired = retireCompletedPresentSlotsOnRenderThread();
        if (!retired) {
            return renderFrameFailure(std::move(retired.error()));
        }
        if (state->submitted) {
            ++packetBackpressureHits_;
            return renderFrameBackpressure();
        }

        const std::uint64_t frameIndex = ++nextFrameIndex_;
        const auto sampledAt = EditorSharedViewportFrameClock::Clock::now();
        const BasicRenderViewFrameParams frameParams =
            frameClock_.frameParams(frameIndex, sampledAt);
        auto rendered = (*producer)->renderPresentSlot(*state, packet.view(), frameParams);
        if (!rendered) {
            return renderFrameFailure(std::move(rendered.error()));
        }
        frameClock_.markRendered(sampledAt);

        ++framesRendered_;
        return state->toPresentPacket();
    }

    asharia::Result<void> EditorSharedViewportRuntime::releasePresentPacketOnRenderThread(
        void* nativePacket, EditorSharedViewportPresentCompletionKind completionKind) {
        assert(isOnRenderThread());
        ++renderThreadDispatches_;
        auto* packetState = static_cast<EditorSharedViewportPacketState*>(nativePacket);
        if (outstandingPackets_.erase(packetState) == 0U) {
            return {};
        }
        outstandingPacketCount_.store(outstandingPackets_.size(), std::memory_order_release);

        if (!packetState->reusable && outstandingLegacyPackets_ != 0U) {
            --outstandingLegacyPackets_;
        }

        std::unique_ptr<EditorSharedViewportPacketState> state{packetState};
        if (completionKind == EditorSharedViewportPresentCompletionKind::ConsumerAccessed) {
            const VkQueue graphicsQueue = context_ ? context_->graphicsQueue() : VK_NULL_HANDLE;
            auto consumerWait = state->submitConsumerReleaseWait(graphicsQueue);
            if (!consumerWait) {
                asharia::Error error = std::move(consumerWait.error());
                logError(error.message);
                state->abandonPendingGpuWork();
                retainRetiringPacketOnRenderThread(std::move(state), true);
                return std::unexpected{std::move(error)};
            }
        }

        auto retired = state->retireCompletedGpuWork();
        if (!retired) {
            asharia::Error error = std::move(retired.error());
            logError(error.message);
            if (state->hasPendingGpuWork()) {
                state->abandonPendingGpuWork();
                retainRetiringPacketOnRenderThread(std::move(state), true);
            }
            return std::unexpected{std::move(error)};
        }
        if (!*retired) {
            retainRetiringPacketOnRenderThread(std::move(state), false);
        }
        return {};
    }

    asharia::Result<void> EditorSharedViewportRuntime::retireCompletedPresentSlotsOnRenderThread() {
        assert(isOnRenderThread());
        for (EditorSharedViewportPacketState* state : outstandingPackets_) {
            if (!state->reusable) {
                continue;
            }
            auto retired = state->retireCompletedGpuWork();
            if (!retired) {
                return std::unexpected{std::move(retired.error())};
            }
        }
        return {};
    }

    bool EditorSharedViewportRuntime::pollRetiringPacketsOnRenderThread() {
        assert(isOnRenderThread());
        bool healthy = true;
        for (RetiringPacket& packet : retiringPackets_) {
            if (!packet.state || packet.quarantined) {
                continue;
            }

            auto retired = packet.state->retireCompletedGpuWork();
            if (!retired) {
                healthy = false;
                logError(retired.error().message);
                shutdownRequestedByCaller_.store(true, std::memory_order_release);
                if (packet.state->hasPendingGpuWork()) {
                    packet.state->abandonPendingGpuWork();
                    packet.quarantined = true;
                    terminalQuarantine_ = true;
                } else {
                    packet.state.reset();
                }
                continue;
            }
            if (*retired) {
                packet.state.reset();
            }
        }
        return healthy;
    }

    void EditorSharedViewportRuntime::retainRetiringPacketOnRenderThread(
        std::unique_ptr<EditorSharedViewportPacketState> state, bool quarantined) {
        assert(isOnRenderThread());
        for (RetiringPacket& packet : retiringPackets_) {
            if (!packet.state) {
                packet.state = std::move(state);
                packet.quarantined = quarantined;
                if (quarantined) {
                    terminalQuarantine_ = true;
                    shutdownRequestedByCaller_.store(true, std::memory_order_release);
                }
                return;
            }
        }

        logError("Shared viewport retirement exceeded its fixed packet capacity; resources were "
                 "quarantined for process lifetime.");
        state->abandonPendingGpuWork();
        terminalQuarantine_ = true;
        shutdownRequestedByCaller_.store(true, std::memory_order_release);
        // Destruction is unsafe because GPU completion is unknown. The process
        // lifetime runtime intentionally keeps the Vulkan context alive too.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        [[maybe_unused]] EditorSharedViewportPacketState* const quarantinedState = state.release();
    }

    std::size_t EditorSharedViewportRuntime::retiringPacketCountOnRenderThread() const {
        assert(isOnRenderThread());
        std::size_t count = 0U;
        for (const RetiringPacket& packet : retiringPackets_) {
            if (packet.state) {
                ++count;
            }
        }
        return count;
    }

    std::optional<std::size_t>
    EditorSharedViewportRuntime::availableFrameResourceIndexOnRenderThread() const {
        assert(isOnRenderThread());
        if (terminalQuarantine_ ||
            outstandingPackets_.size() + retiringPacketCountOnRenderThread() >=
                kMaxOutstandingPackets) {
            return std::nullopt;
        }

        std::array<bool, kMaxOutstandingPackets> used{};
        for (const EditorSharedViewportPacketState* state : outstandingPackets_) {
            if (!state->frameResources) {
                return std::nullopt;
            }
            const std::size_t index = state->frameResources->index();
            if (index >= used.size()) {
                return std::nullopt;
            }
            used.at(index) = true;
        }
        for (const RetiringPacket& packet : retiringPackets_) {
            if (!packet.state) {
                continue;
            }
            const auto& frameResources = packet.state->frameResources;
            if (!frameResources) {
                return std::nullopt;
            }
            const std::size_t index = frameResources->index();
            if (index >= used.size()) {
                return std::nullopt;
            }
            used.at(index) = true;
        }

        for (std::size_t index = 0U; index < used.size(); ++index) {
            if (!used.at(index)) {
                return index;
            }
        }
        return std::nullopt;
    }

    bool EditorSharedViewportRuntime::hasPollableRetirementOnRenderThread() const {
        assert(isOnRenderThread());
        return std::ranges::any_of(retiringPackets_,
                                   [](const RetiringPacket& packet) {
                                       return packet.state && !packet.quarantined;
                                   }) ||
               std::ranges::any_of(outstandingPackets_,
                                   [](const EditorSharedViewportPacketState* state) {
                                       return state->reusable && state->hasPendingGpuWork();
                                   });
    }

    bool EditorSharedViewportRuntime::hasQuarantinedRetirementOnRenderThread() const {
        assert(isOnRenderThread());
        return terminalQuarantine_ ||
               std::ranges::any_of(retiringPackets_, [](const RetiringPacket& packet) {
                   return packet.state && packet.quarantined;
               });
    }

    bool EditorSharedViewportRuntime::processStreamCompletionsOnRenderThread(StreamState& stream) {
        assert(isOnRenderThread());
        std::lock_guard lock{stream.mutex};
        for (StreamSlot& slot : stream.slots) {
            if (slot.phase != StreamSlotPhase::Completing || slot.nativeSlot == nullptr) {
                continue;
            }

            auto* state = static_cast<EditorSharedViewportPacketState*>(slot.nativeSlot);
            slot.consumerAccessed =
                slot.completionKind == EditorSharedViewportPresentCompletionKind::ConsumerAccessed;
            if (!slot.consumerAccessed) {
                // The compositor never received this frame, so no matching
                // consumer-done signal will arrive before the next reuse.
                state->waitForCompositionRelease = false;
            }
            slot.phase = StreamSlotPhase::Available;
            return true;
        }
        return false;
    }

    bool EditorSharedViewportRuntime::processStreamCloseOnRenderThread(StreamState& stream) {
        assert(isOnRenderThread());
        void* nativeSlot{};
        EditorSharedViewportPresentCompletionKind completionKind{
            EditorSharedViewportPresentCompletionKind::NotSubmittedToConsumer};
        std::size_t slotIndex{};
        {
            std::lock_guard lock{stream.mutex};
            if (!stream.closeRequested || stream.closed || stream.faulted ||
                stream.renderExecuting) {
                return false;
            }

            if (stream.readyFrame) {
                StreamSlot& slot = stream.slots.at(stream.readyFrame->slotIndex);
                if (slot.nativeSlot != nullptr && slot.phase == StreamSlotPhase::Ready) {
                    static_cast<EditorSharedViewportPacketState*>(slot.nativeSlot)
                        ->waitForCompositionRelease = false;
                    slot.consumerAccessed = false;
                    slot.phase = StreamSlotPhase::Available;
                }
                stream.readyFrame.reset();
                return true;
            }

            for (std::size_t index = 0U; index < stream.slots.size(); ++index) {
                StreamSlot& slot = stream.slots.at(index);
                if (slot.nativeSlot == nullptr || slot.phase == StreamSlotPhase::Retired) {
                    continue;
                }
                if (slot.phase == StreamSlotPhase::Presented ||
                    slot.phase == StreamSlotPhase::Completing ||
                    (slot.importExposed && !slot.importReleased)) {
                    continue;
                }

                nativeSlot = slot.nativeSlot;
                completionKind = EditorSharedViewportPresentCompletionKind::NotSubmittedToConsumer;
                if (slot.consumerAccessed) {
                    // The caller releases slot imports only after Avalonia's surface-update task
                    // completed and the imported image/semaphore wrappers were disposed. That API
                    // contract explicitly permits destroying the image at this point, so a second
                    // native wait on the compositor signal is both redundant and capable of
                    // deadlocking an old-size stream during interactive resize.
                    static_cast<EditorSharedViewportPacketState*>(slot.nativeSlot)
                        ->waitForCompositionRelease = false;
                }
                slotIndex = index;
                slot.phase = StreamSlotPhase::Retired;
                break;
            }

            if (nativeSlot == nullptr) {
                const bool allRetired =
                    std::ranges::all_of(stream.slots, [](const StreamSlot& slot) {
                        return slot.nativeSlot == nullptr || slot.phase == StreamSlotPhase::Retired;
                    });
                if (allRetired) {
                    stream.closed = true;
                    ++stream.stateRevision;
                    stream.stateChanged.notify_all();
                    return true;
                }
                return false;
            }
        }

        auto released = releasePresentPacketOnRenderThread(nativeSlot, completionKind);
        std::lock_guard lock{stream.mutex};
        StreamSlot& slot = stream.slots.at(slotIndex);
        if (!released) {
            logError(released.error().message);
            stream.faulted = true;
            ++stream.stateRevision;
            stream.stateChanged.notify_all();
            return true;
        }
        slot.nativeSlot = nullptr;
        if (std::ranges::all_of(stream.slots, [](const StreamSlot& item) {
                return item.nativeSlot == nullptr || item.phase == StreamSlotPhase::Retired;
            })) {
            stream.closed = true;
            ++stream.stateRevision;
            stream.stateChanged.notify_all();
        }
        return true;
    }

    bool EditorSharedViewportRuntime::renderPendingStreamFrameOnRenderThread(StreamState& stream) {
        assert(isOnRenderThread());
        std::optional<RenderFramePacket> packet;
        void* nativeSlot{};
        std::size_t slotIndex{};
        bool createSlot{};
        {
            std::lock_guard lock{stream.mutex};
            if (stream.closeRequested || stream.closed || stream.faulted ||
                stream.renderExecuting || stream.readyFrame || !stream.pendingLatest) {
                return false;
            }

            std::size_t reusableSlotCount{};
            for (std::size_t index = 0U; index < stream.slots.size(); ++index) {
                StreamSlot& slot = stream.slots.at(index);
                if (slot.phase != StreamSlotPhase::Available || slot.nativeSlot == nullptr) {
                    continue;
                }
                auto* state = static_cast<EditorSharedViewportPacketState*>(slot.nativeSlot);
                if (state->submitted || state->consumerReleasePending) {
                    continue;
                }
                if (nativeSlot == nullptr) {
                    nativeSlot = slot.nativeSlot;
                    slotIndex = index;
                }
                ++reusableSlotCount;
            }

            // Match Avalonia's external swapchain invariant: only resume reuse when at least two
            // completed same-size images are available. Reusing the sole completed image can
            // serialize producer and compositor ownership and lock the UI pipeline.
            if (reusableSlotCount < 2U) {
                nativeSlot = nullptr;
                if (stream.slots.size() >= kMaxStreamSlots) {
                    return false;
                }
                createSlot = true;
                slotIndex = stream.slots.size();
            }

            packet.emplace(std::move(*stream.pendingLatest));
            stream.pendingLatest.reset();
            stream.renderExecuting = true;
        }

        EditorSharedViewportRenderFrameResult rendered =
            createSlot ? createPresentSlotOnRenderThread(*packet)
                       : renderPresentSlotOnRenderThread(nativeSlot, *packet);

        std::lock_guard lock{stream.mutex};
        stream.renderExecuting = false;
        if (!rendered) {
            if (rendered.error().kind == EditorSharedViewportRenderFrameErrorKind::Backpressure) {
                if (!stream.pendingLatest) {
                    stream.pendingLatest.emplace(std::move(*packet));
                } else {
                    ++stream.coalescedRequests;
                }
                return false;
            }

            logError(rendered.error().error.message);
            stream.faulted = true;
            ++stream.stateRevision;
            stream.stateChanged.notify_all();
            return true;
        }

        if (createSlot) {
            stream.slots.push_back(StreamSlot{
                .nativeSlot = rendered->nativePacket,
                .phase = StreamSlotPhase::Ready,
                .completionKind = EditorSharedViewportPresentCompletionKind::NotSubmittedToConsumer,
                .importExposed = false,
                .importReleased = true,
                .consumerAccessed = false,
                .requestSequence = packet->requestSequence,
            });
        } else {
            StreamSlot& slot = stream.slots.at(slotIndex);
            slot.phase = StreamSlotPhase::Ready;
            slot.completionKind = EditorSharedViewportPresentCompletionKind::NotSubmittedToConsumer;
            slot.consumerAccessed = false;
            slot.requestSequence = packet->requestSequence;
        }
        stream.readyFrame = StreamReadyFrame{
            .slotIndex = slotIndex,
            .frame =
                EditorSharedViewportReadyFrame{
                    .present = *rendered,
                    .sessionId = packet->sessionId,
                    .targetId = packet->targetId,
                    .targetRevision = packet->sceneRevision,
                    .requestSequence = packet->requestSequence,
                    .viewStateRevision = packet->viewStateRevision,
                    .kind = packet->kind,
                    .logicalExtent = packet->logicalExtent,
                    .sceneMeshReceipt =
                        static_cast<EditorSharedViewportPacketState*>(rendered->nativePacket)
                            ->sceneMeshReceipt,
                },
        };
        ++stream.renderedFrames;
        ++stream.stateRevision;
        stream.stateChanged.notify_all();
        return true;
    }

    bool EditorSharedViewportRuntime::dispatchOneStreamWorkOnRenderThread() {
        assert(isOnRenderThread());
        struct ScheduledStream final {
            EditorSharedViewportStreamId streamId{};
            std::shared_ptr<StreamState> state;
        };

        std::vector<ScheduledStream> streams;
        {
            std::lock_guard lock{streamsMutex_};
            streams.reserve(streams_.size());
            for (const auto& [streamId, stream] : streams_) {
                streams.push_back(ScheduledStream{.streamId = streamId, .state = stream});
            }
        }
        std::ranges::sort(streams, {}, &ScheduledStream::streamId);

        auto retired = retireCompletedPresentSlotsOnRenderThread();
        if (!retired) {
            logError(retired.error().message);
            shutdownRequestedByCaller_.store(true, std::memory_order_release);
            return false;
        }

        const bool canAllocateSlot = availableFrameResourceIndexOnRenderThread().has_value();
        return detail::dispatchOneStableRoundRobin(
            std::span{streams}, lastDispatchedStreamId_,
            [](const ScheduledStream& stream) { return stream.streamId; },
            [this](ScheduledStream& stream) {
                return processStreamCompletionsOnRenderThread(*stream.state) ||
                       processStreamCloseOnRenderThread(*stream.state);
            },
            [this, canAllocateSlot](ScheduledStream& stream) {
                {
                    std::lock_guard lock{stream.state->mutex};
                    if (!streamHasWorkLocked(*stream.state, canAllocateSlot)) {
                        return false;
                    }
                }
                return renderPendingStreamFrameOnRenderThread(*stream.state);
            });
    }

    bool EditorSharedViewportRuntime::streamHasWorkLocked(const StreamState& stream,
                                                          bool canAllocateSlot) {
        if (stream.faulted || stream.closed) {
            return false;
        }
        if (std::ranges::any_of(stream.slots, [](const StreamSlot& slot) {
                return slot.phase == StreamSlotPhase::Completing;
            })) {
            return true;
        }
        if (stream.closeRequested) {
            const bool allSlotsRetired =
                std::ranges::all_of(stream.slots, [](const StreamSlot& slot) {
                    return slot.nativeSlot == nullptr || slot.phase == StreamSlotPhase::Retired;
                });
            const bool hasRetirableSlot =
                std::ranges::any_of(stream.slots, [](const StreamSlot& slot) {
                    return slot.nativeSlot != nullptr && slot.phase != StreamSlotPhase::Presented &&
                           slot.phase != StreamSlotPhase::Completing &&
                           (!slot.importExposed || slot.importReleased);
                });
            return stream.slots.empty() || stream.readyFrame || allSlotsRetired || hasRetirableSlot;
        }
        if (stream.readyFrame || !stream.pendingLatest) {
            return false;
        }
        const auto reusableSlotCount =
            std::ranges::count_if(stream.slots, [](const StreamSlot& slot) {
                if (slot.phase != StreamSlotPhase::Available || slot.nativeSlot == nullptr) {
                    return false;
                }
                const auto* state =
                    static_cast<const EditorSharedViewportPacketState*>(slot.nativeSlot);
                return !state->submitted && !state->consumerReleasePending;
            });
        return reusableSlotCount >= 2 || (stream.slots.size() < kMaxStreamSlots && canAllocateSlot);
    }

    bool EditorSharedViewportRuntime::hasStreamWork() const {
        assert(isOnRenderThread());
        const bool canAllocateSlot = availableFrameResourceIndexOnRenderThread().has_value();
        std::vector<std::shared_ptr<StreamState>> streams;
        {
            std::lock_guard lock{streamsMutex_};
            streams.reserve(streams_.size());
            for (const auto& [streamId, stream] : streams_) {
                static_cast<void>(streamId);
                streams.push_back(stream);
            }
        }

        return std::ranges::any_of(streams,
                                   [canAllocateSlot](const std::shared_ptr<StreamState>& stream) {
                                       std::lock_guard lock{stream->mutex};
                                       return streamHasWorkLocked(*stream, canAllocateSlot);
                                   });
    }

    void EditorSharedViewportRuntime::beginShutdownOnRenderThread() {
        assert(isOnRenderThread());
        if (shutdownRequested_) {
            return;
        }

        shutdownRequested_ = true;
        lifecycle_.store(EditorSharedViewportRuntimeLifecycle::Draining, std::memory_order_release);
        lifecycleChanged_.notify_all();
    }

    bool EditorSharedViewportRuntime::tryFinishShutdownOnRenderThread() {
        assert(isOnRenderThread());
        if (!shutdownRequested_) {
            return false;
        }

        if (!outstandingPackets_.empty()) {
            return false;
        }
        const bool quarantined = hasQuarantinedRetirementOnRenderThread();
        if (!quarantined && retiringPacketCountOnRenderThread() != 0U) {
            return false;
        }
        {
            std::lock_guard lock{queueMutex_};
            if (!releaseQueue_.empty() || !controlQueue_.empty() || !renderQueue_.empty()) {
                return false;
            }
            // Admission closes under the same mutex used by enqueueReleaseWork.
            // A stale or duplicate late release then fails synchronously instead
            // of entering a queue after its owner has committed to exit.
            releaseAdmissionClosed_ = true;
        }
        if (quarantined) {
            lifecycle_.store(EditorSharedViewportRuntimeLifecycle::Faulted,
                             std::memory_order_release);
            lifecycleChanged_.notify_all();
            return true;
        }

        // Shutdown-only destruction stays on the same owner as creation,
        // recording, submission, fence polling, and packet retirement.
        renderProducer_.reset();
        context_.reset();
        nextFrameIndex_ = 0U;
        lifecycle_.store(EditorSharedViewportRuntimeLifecycle::Stopped, std::memory_order_release);
        lifecycleChanged_.notify_all();
        return true;
    }

    void EditorSharedViewportRuntime::publishRuntimeStatsOnRenderThread() {
        assert(isOnRenderThread());
        EditorSharedViewportRenderProducerStats producerStats{};
        if (renderProducer_) {
            producerStats = renderProducer_->stats();
        }

        std::size_t queuedRenderCommands = 0U;
        {
            std::lock_guard lock{queueMutex_};
            queuedRenderCommands = renderQueue_.size();
        }
        const EditorSharedViewportRuntimeLifecycle state =
            lifecycle_.load(std::memory_order_acquire);
        EditorSharedViewportRuntimeStats snapshot{
            .framesRendered = framesRendered_,
            .producersCreated = producersCreated_,
            .packetsCreated = packetsCreated_,
            .externalImagesAcquired = producerStats.externalImagesAcquired,
            .externalImagesCreated = producerStats.externalImagesCreated,
            .externalImagesReused = producerStats.externalImagesReused,
            .externalImagesReleased = producerStats.externalImagesReleased,
            .externalImagesAvailable = producerStats.externalImagesAvailable,
            .externalImagesLeased = producerStats.externalImagesLeased,
            .frameEpochsSubmitted = producerStats.frameEpochsSubmitted,
            .frameEpochsCompleted = producerStats.frameEpochsCompleted,
            .frameEpochsPending = producerStats.frameEpochsPending,
            .rendererCreations = producerStats.rendererCreations,
            .packetBackpressureHits = packetBackpressureHits_,
            .sceneFramesRendered = producerStats.sceneFramesRendered,
            .gameFramesRendered = producerStats.gameFramesRendered,
            .previewFramesRendered = producerStats.previewFramesRendered,
            .lastSceneRevision = producerStats.lastSceneRevision,
            .lastRequestSequence = producerStats.lastRequestSequence,
            .lastSessionId = producerStats.lastSessionId,
            .lastTargetId = producerStats.lastTargetId,
            .lastRenderKind = producerStats.lastRenderKind,
            .lastRenderExtent = producerStats.lastRenderExtent,
            .lastDebugProxyCount = producerStats.lastDebugProxyCount,
            .lastDebugWorldLineCount = producerStats.lastDebugWorldLineCount,
            .lastWorldGridEnabled = producerStats.lastWorldGridEnabled,
            .maxOutstandingPackets = kMaxOutstandingPackets,
            .outstandingPackets = outstandingPackets_.size(),
            .hasContext = context_.has_value(),
            .hasRenderProducer = renderProducer_.has_value(),
            .shutdownRequested = shutdownRequested_,
            .renderQueueBackpressureHits =
                renderQueueBackpressureHits_.load(std::memory_order_relaxed),
            .renderThreadDispatches = renderThreadDispatches_,
            .maxQueuedRenderCommands = kMaxQueuedRenderCommands,
            .maxObservedQueuedRenderCommands =
                maxObservedQueuedRenderCommands_.load(std::memory_order_relaxed),
            .queuedRenderCommands = queuedRenderCommands,
            .renderThreadId = renderThreadId_,
            .lifecycle = state,
            .renderThreadRunning = state == EditorSharedViewportRuntimeLifecycle::Running ||
                                   state == EditorSharedViewportRuntimeLifecycle::Draining,
            .renderThreadJoined = false,
        };
        std::lock_guard lock{publishedStateMutex_};
        publishedStats_ = snapshot;
    }

    bool EditorSharedViewportRuntime::enqueueRenderWork(std::packaged_task<void()> work) {
        std::lock_guard lock{queueMutex_};
        const EditorSharedViewportRuntimeLifecycle state =
            lifecycle_.load(std::memory_order_acquire);
        if (shutdownRequestedByCaller_.load(std::memory_order_acquire) || isTerminal(state)) {
            return false;
        }
        if (renderQueue_.size() >= kMaxQueuedRenderCommands) {
            renderQueueBackpressureHits_.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }

        renderQueue_.push_back(std::move(work));
        const std::size_t depth = renderQueue_.size();
        std::size_t observed = maxObservedQueuedRenderCommands_.load(std::memory_order_relaxed);
        while (observed < depth && !maxObservedQueuedRenderCommands_.compare_exchange_weak(
                                       observed, depth, std::memory_order_relaxed)) {
        }
        queueReady_.notify_one();
        return true;
    }

    bool EditorSharedViewportRuntime::enqueueControlWork(std::packaged_task<void()> work) {
        std::unique_lock lock{queueMutex_};
        queueSpaceAvailable_.wait(lock, [this] {
            return controlQueue_.size() < kMaxQueuedControlCommands ||
                   shutdownRequestedByCaller_.load(std::memory_order_acquire) ||
                   isTerminal(lifecycle_.load(std::memory_order_acquire));
        });
        if (shutdownRequestedByCaller_.load(std::memory_order_acquire) ||
            isTerminal(lifecycle_.load(std::memory_order_acquire))) {
            return false;
        }

        controlQueue_.push_back(std::move(work));
        lock.unlock();
        queueReady_.notify_one();
        return true;
    }

    bool EditorSharedViewportRuntime::enqueueReleaseWork(std::packaged_task<void()> work) {
        std::unique_lock lock{queueMutex_};
        queueSpaceAvailable_.wait(lock, [this] {
            return releaseQueue_.size() < kMaxQueuedReleaseCommands || releaseAdmissionClosed_ ||
                   isTerminal(lifecycle_.load(std::memory_order_acquire));
        });
        if (releaseAdmissionClosed_ || isTerminal(lifecycle_.load(std::memory_order_acquire))) {
            return false;
        }

        releaseQueue_.push_back(std::move(work));
        lock.unlock();
        queueReady_.notify_one();
        return true;
    }

    bool EditorSharedViewportRuntime::isOnRenderThread() const noexcept {
        return sharedViewportRenderThreadOwner() == this;
    }

    void EditorSharedViewportRuntime::renderThreadMain() {
        sharedViewportRenderThreadOwner() = this;
        renderThreadId_ = std::this_thread::get_id();
        try {
            renderThreadLoop();
        } catch (...) {
            logError("Shared viewport render thread failed; Vulkan resources were quarantined.");
            shutdownRequestedByCaller_.store(true, std::memory_order_release);
            shutdownRequested_ = true;
            terminalQuarantine_ = true;
            lifecycle_.store(EditorSharedViewportRuntimeLifecycle::Faulted,
                             std::memory_order_release);

            std::array<std::deque<std::packaged_task<void()>>, 3> abandonedWork;
            {
                std::lock_guard lock{queueMutex_};
                abandonedWork = {
                    std::move(releaseQueue_),
                    std::move(controlQueue_),
                    std::move(renderQueue_),
                };
            }
            for (auto& queue : abandonedWork) {
                while (!queue.empty()) {
                    std::packaged_task<void()> work = std::move(queue.front());
                    queue.pop_front();
                    if (work.valid()) {
                        work();
                    }
                }
            }

            publishRuntimeStatsOnRenderThread();
            queueSpaceAvailable_.notify_all();
            lifecycleChanged_.notify_all();
        }
        {
            std::lock_guard registryLock{streamsMutex_};
            for (auto& [id, stream] : streams_) {
                std::lock_guard streamLock{stream->mutex};
                if (!stream->closed) {
                    stream->faulted = true;
                    ++stream->stateRevision;
                    stream->stateChanged.notify_all();
                }
            }
        }
        sharedViewportRenderThreadOwner() = nullptr;
    }

    void EditorSharedViewportRuntime::renderThreadLoop() {
        lifecycle_.store(EditorSharedViewportRuntimeLifecycle::Running, std::memory_order_release);
        publishRuntimeStatsOnRenderThread();
        lifecycleChanged_.notify_all();

        for (;;) {
            if (shutdownRequestedByCaller_.load(std::memory_order_acquire) && !shutdownRequested_) {
                beginShutdownOnRenderThread();
            }
            if (shutdownRequested_ && tryFinishShutdownOnRenderThread()) {
                publishRuntimeStatsOnRenderThread();
                break;
            }

            std::packaged_task<void()> work = waitForNextWorkOnRenderThread();

            if (shutdownRequestedByCaller_.load(std::memory_order_acquire) && !shutdownRequested_) {
                beginShutdownOnRenderThread();
            }
            if (work.valid()) {
                work();
            }

            [[maybe_unused]] const bool streamWorkDispatched =
                dispatchOneStreamWorkOnRenderThread();

            [[maybe_unused]] const bool retirementHealthy = pollRetiringPacketsOnRenderThread();
            if (shutdownRequested_) {
                [[maybe_unused]] const bool finished = tryFinishShutdownOnRenderThread();
            }
            publishRuntimeStatsOnRenderThread();

            if (isTerminal(lifecycle_.load(std::memory_order_acquire))) {
                break;
            }
        }

        queueSpaceAvailable_.notify_all();
        lifecycleChanged_.notify_all();
    }

    std::packaged_task<void()> EditorSharedViewportRuntime::waitForNextWorkOnRenderThread() {
        assert(isOnRenderThread());
        std::packaged_task<void()> work;
        {
            std::unique_lock lock{queueMutex_};
            const auto ready = [this] {
                return !releaseQueue_.empty() || !controlQueue_.empty() || !renderQueue_.empty() ||
                       hasStreamWork() ||
                       (shutdownRequestedByCaller_.load(std::memory_order_acquire) &&
                        !shutdownRequested_);
            };
            if (!ready()) {
                if (hasPollableRetirementOnRenderThread()) {
                    queueReady_.wait_for(lock, std::chrono::milliseconds{1}, ready);
                } else {
                    queueReady_.wait(lock, ready);
                }
            }

            if (!releaseQueue_.empty()) {
                work = std::move(releaseQueue_.front());
                releaseQueue_.pop_front();
            } else if (!controlQueue_.empty()) {
                work = std::move(controlQueue_.front());
                controlQueue_.pop_front();
            } else if (!renderQueue_.empty()) {
                work = std::move(renderQueue_.front());
                renderQueue_.pop_front();
            }
        }
        queueSpaceAvailable_.notify_all();
        return work;
    }

    asharia::Result<void> EditorSharedViewportRuntime::ensureRenderThreadStarted() {
        std::lock_guard lock{threadMutex_};
        if (renderThread_.joinable()) {
            return {};
        }

        const EditorSharedViewportRuntimeLifecycle state =
            lifecycle_.load(std::memory_order_acquire);
        if (state != EditorSharedViewportRuntimeLifecycle::Starting) {
            return std::unexpected{vulkanError("Shared viewport render thread is not available")};
        }

        try {
            renderThread_ = std::thread{[this] { renderThreadMain(); }};
            return {};
        } catch (const std::system_error& exception) {
            shutdownRequestedByCaller_.store(true, std::memory_order_release);
            lifecycle_.store(EditorSharedViewportRuntimeLifecycle::Faulted,
                             std::memory_order_release);
            {
                std::lock_guard publishedLock{publishedStateMutex_};
                publishedStats_.shutdownRequested = true;
                publishedStats_.lifecycle = EditorSharedViewportRuntimeLifecycle::Faulted;
                publishedStats_.renderThreadRunning = false;
                publishedStats_.renderThreadJoined = true;
            }
            lifecycleChanged_.notify_all();
            return std::unexpected{vulkanError(
                std::string{"Failed to start shared viewport render thread: "} + exception.what())};
        }
    }

    void EditorSharedViewportRuntime::joinRenderThreadIfTerminal() {
        if (!isTerminal(lifecycle_.load(std::memory_order_acquire))) {
            return;
        }

        std::lock_guard lock{threadMutex_};
        if (renderThread_.joinable() && renderThread_.get_id() != std::this_thread::get_id()) {
            renderThread_.join();
        }
    }

} // namespace asharia::editor
