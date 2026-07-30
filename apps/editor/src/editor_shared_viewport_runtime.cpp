#include "editor_shared_viewport_runtime.hpp"

#include <array>
#include <expected>
#include <mutex>
#include <optional>
#include <utility>

#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace asharia::editor {
    namespace {

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
        renderFrameBackpressure() {
            return std::unexpected{EditorSharedViewportRenderFrameError{
                .kind = EditorSharedViewportRenderFrameErrorKind::Backpressure,
                .error = vulkanError("Shared viewport present resources are still in use"),
            }};
        }

    } // namespace

    EditorSharedViewportRuntime& EditorSharedViewportRuntime::instance() {
        // The small owner is process-lifetime storage. Normal shutdown still
        // releases the producer and Vulkan context explicitly. Keeping the
        // owner itself alive makes terminal quarantine safe: the CRT cannot
        // destroy a device while an unconfirmed external consumer still owns
        // one of its packet resources.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        static auto* const runtime = new EditorSharedViewportRuntime;
        return *runtime;
    }

    asharia::Result<const asharia::VulkanContext*> EditorSharedViewportRuntime::ensureContext() {
        std::lock_guard lock{mutex_};
        if (shutdownRequested_) {
            return std::unexpected{vulkanError("Shared viewport runtime has shut down")};
        }

        auto ensured = ensureSharedContextStorage(context_);
        if (!ensured) {
            return std::unexpected{std::move(ensured.error())};
        }

        return &*context_;
    }

    asharia::Result<EditorSharedViewportRenderProducer*>
    EditorSharedViewportRuntime::ensureRenderProducerLocked() {
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
        ++producersCreated_;
        return &*renderProducer_;
    }

    EditorSharedViewportRenderFrameResult
    EditorSharedViewportRuntime::renderSceneViewFrame(EditorSharedViewportPresentDesc desc) {
        if (desc.extent.width == 0 || desc.extent.height == 0) {
            return renderFrameFailure(
                vulkanError("Cannot render a shared viewport frame for an empty extent"));
        }

        std::lock_guard lock{mutex_};
        if (shutdownRequested_) {
            return renderFrameFailure(vulkanError("Shared viewport runtime has shut down"));
        }

        if (outstandingLegacyPackets_ >= kMaxOutstandingLegacyPackets) {
            ++packetBackpressureHits_;
            return renderFrameBackpressure();
        }
        const std::optional<std::size_t> frameResourceIndex = availableFrameResourceIndexLocked();
        if (!frameResourceIndex) {
            ++packetBackpressureHits_;
            return renderFrameBackpressure();
        }

        auto ensured = ensureSharedContextStorage(context_);
        if (!ensured) {
            return renderFrameFailure(std::move(ensured.error()));
        }

        auto producer = ensureRenderProducerLocked();
        if (!producer) {
            return renderFrameFailure(std::move(producer.error()));
        }

        const std::uint64_t frameIndex = ++nextFrameIndex_;
        auto state = (*producer)->renderSceneViewFrame(frameIndex, desc, *frameResourceIndex);
        if (!state) {
            return renderFrameFailure(std::move(state.error()));
        }

        EditorSharedViewportPacketState* statePtr = state->get();
        outstandingPackets_.insert(statePtr);
        ++outstandingLegacyPackets_;
        ++framesRendered_;
        ++packetsCreated_;
        [[maybe_unused]] EditorSharedViewportPacketState* const releasedState = state->release();
        return statePtr->toPresentPacket();
    }

    EditorSharedViewportRenderFrameResult
    EditorSharedViewportRuntime::createPresentSlot(EditorSharedViewportPresentDesc desc) {
        if (desc.extent.width == 0 || desc.extent.height == 0) {
            return renderFrameFailure(
                vulkanError("Cannot create a shared viewport present slot for an empty extent"));
        }

        std::lock_guard lock{mutex_};
        if (shutdownRequested_) {
            return renderFrameFailure(vulkanError("Shared viewport runtime has shut down"));
        }

        const std::optional<std::size_t> frameResourceIndex = availableFrameResourceIndexLocked();
        if (!frameResourceIndex) {
            ++packetBackpressureHits_;
            return renderFrameBackpressure();
        }

        auto ensured = ensureSharedContextStorage(context_);
        if (!ensured) {
            return renderFrameFailure(std::move(ensured.error()));
        }
        auto producer = ensureRenderProducerLocked();
        if (!producer) {
            return renderFrameFailure(std::move(producer.error()));
        }
        auto retired = retireCompletedPresentSlotsLocked();
        if (!retired) {
            return renderFrameFailure(std::move(retired.error()));
        }

        const std::uint64_t frameIndex = ++nextFrameIndex_;
        auto state = (*producer)->createPresentSlot(frameIndex, desc, *frameResourceIndex);
        if (!state) {
            return renderFrameFailure(std::move(state.error()));
        }

        EditorSharedViewportPacketState* statePtr = state->get();
        outstandingPackets_.insert(statePtr);
        ++framesRendered_;
        ++packetsCreated_;
        [[maybe_unused]] EditorSharedViewportPacketState* const releasedState = state->release();
        return statePtr->toPresentPacket();
    }

    EditorSharedViewportRenderFrameResult
    EditorSharedViewportRuntime::renderPresentSlot(void* nativeSlot,
                                                   EditorSharedViewportPresentDesc desc) {
        if (nativeSlot == nullptr) {
            return renderFrameFailure(
                vulkanError("Cannot render a null shared viewport present slot"));
        }

        std::lock_guard lock{mutex_};
        if (shutdownRequested_) {
            return renderFrameFailure(vulkanError("Shared viewport runtime has shut down"));
        }

        auto* state = static_cast<EditorSharedViewportPacketState*>(nativeSlot);
        if (!outstandingPackets_.contains(state) || !state->reusable) {
            return renderFrameFailure(
                vulkanError("Shared viewport present slot is not owned by the runtime"));
        }
        auto producer = ensureRenderProducerLocked();
        if (!producer) {
            return renderFrameFailure(std::move(producer.error()));
        }
        auto retired = retireCompletedPresentSlotsLocked();
        if (!retired) {
            return renderFrameFailure(std::move(retired.error()));
        }
        if (state->submitted) {
            ++packetBackpressureHits_;
            return renderFrameBackpressure();
        }

        const std::uint64_t frameIndex = ++nextFrameIndex_;
        auto rendered = (*producer)->renderPresentSlot(*state, desc, frameIndex);
        if (!rendered) {
            return renderFrameFailure(std::move(rendered.error()));
        }

        ++framesRendered_;
        return state->toPresentPacket();
    }

    void EditorSharedViewportRuntime::releasePresentPacket(void* nativePacket) {
        if (nativePacket == nullptr) {
            return;
        }

        auto* packetState = static_cast<EditorSharedViewportPacketState*>(nativePacket);
        std::unique_ptr<EditorSharedViewportPacketState> state;
        std::optional<std::size_t> releasingFrameResourceIndex;
        {
            std::lock_guard lock{mutex_};
            if (outstandingPackets_.erase(packetState) == 0U) {
                return;
            }

            if (packetState->frameResources) {
                const std::size_t index = packetState->frameResources->index();
                if (index < releasingFrameResourceIndices_.size()) {
                    releasingFrameResourceIndex = index;
                    releasingFrameResourceIndices_.at(index) = true;
                }
            }
            if (!packetState->reusable) {
                --outstandingLegacyPackets_;
            }
            ++releasingPacketCount_;
            state.reset(packetState);
        }

        state.reset();

        std::optional<asharia::VulkanContext> contextToDestroy;
        {
            std::lock_guard lock{mutex_};
            if (releasingFrameResourceIndex) {
                releasingFrameResourceIndices_.at(*releasingFrameResourceIndex) = false;
            }
            --releasingPacketCount_;
            contextToDestroy = takeContextForShutdownIfIdleLocked();
        }
    }

    asharia::Result<void> EditorSharedViewportRuntime::retireCompletedPresentSlotsLocked() {
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

    std::optional<std::size_t>
    EditorSharedViewportRuntime::availableFrameResourceIndexLocked() const {
        if (outstandingPackets_.size() >= kMaxOutstandingPackets) {
            return std::nullopt;
        }

        std::array<bool, kMaxOutstandingPackets> used = releasingFrameResourceIndices_;
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

        for (std::size_t index = 0U; index < used.size(); ++index) {
            if (!used.at(index)) {
                return index;
            }
        }
        return std::nullopt;
    }

    void EditorSharedViewportRuntime::shutdown() {
        std::optional<asharia::VulkanContext> contextToDestroy;
        {
            std::lock_guard lock{mutex_};
            shutdownRequested_ = true;
            contextToDestroy = takeContextForShutdownIfIdleLocked();
        }
    }

    EditorSharedViewportRuntimeStats EditorSharedViewportRuntime::stats() const {
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
            .frameEpochsSubmitted = producerStats.frameEpochsSubmitted,
            .frameEpochsCompleted = producerStats.frameEpochsCompleted,
            .frameEpochsPending = producerStats.frameEpochsPending,
            .rendererCreations = producerStats.rendererCreations,
            .packetBackpressureHits = packetBackpressureHits_,
            .sceneFramesRendered = producerStats.sceneFramesRendered,
            .lastSceneRevision = producerStats.lastSceneRevision,
            .maxOutstandingPackets = kMaxOutstandingPackets,
            .outstandingPackets = outstandingPackets_.size(),
            .hasContext = context_.has_value(),
            .hasRenderProducer = renderProducer_.has_value(),
            .shutdownRequested = shutdownRequested_,
        };
    }

    std::optional<asharia::VulkanContext>
    EditorSharedViewportRuntime::takeContextForShutdownIfIdleLocked() {
        if (!shutdownRequested_ || !outstandingPackets_.empty() || releasingPacketCount_ != 0U ||
            !context_) {
            return std::nullopt;
        }

        renderProducer_.reset();

        std::optional<asharia::VulkanContext> contextToDestroy;
        contextToDestroy.emplace(std::move(*context_));
        context_.reset();
        nextFrameIndex_ = 0U;
        return contextToDestroy;
    }

} // namespace asharia::editor
