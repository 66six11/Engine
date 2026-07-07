#include "editor_shared_viewport_runtime.hpp"

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
                .enableValidation = true,
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

    } // namespace

    EditorSharedViewportRuntime& EditorSharedViewportRuntime::instance() {
        static EditorSharedViewportRuntime runtime;
        return runtime;
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

    asharia::Result<EditorSharedViewportPresentPacket>
    EditorSharedViewportRuntime::renderSceneViewFrame(EditorSharedViewportPresentDesc desc) {
        if (desc.extent.width == 0 || desc.extent.height == 0) {
            return std::unexpected{
                vulkanError("Cannot render a shared viewport frame for an empty extent")};
        }

        std::lock_guard lock{mutex_};
        if (shutdownRequested_) {
            return std::unexpected{vulkanError("Shared viewport runtime has shut down")};
        }

        auto ensured = ensureSharedContextStorage(context_);
        if (!ensured) {
            return std::unexpected{std::move(ensured.error())};
        }

        auto producer = ensureRenderProducerLocked();
        if (!producer) {
            return std::unexpected{std::move(producer.error())};
        }

        const std::uint64_t frameIndex = ++nextFrameIndex_;
        auto state = (*producer)->renderSceneViewFrame(desc, frameIndex);
        if (!state) {
            return std::unexpected{std::move(state.error())};
        }

        EditorSharedViewportPacketState* statePtr = state->get();
        outstandingPackets_.insert(statePtr);
        ++framesRendered_;
        ++packetsCreated_;
        [[maybe_unused]] EditorSharedViewportPacketState* const releasedState =
            state->release();
        return statePtr->toPresentPacket();
    }

    void EditorSharedViewportRuntime::releasePresentPacket(void* nativePacket) {
        if (nativePacket == nullptr) {
            return;
        }

        auto* packetState = static_cast<EditorSharedViewportPacketState*>(nativePacket);
        std::unique_ptr<EditorSharedViewportPacketState> state;
        {
            std::lock_guard lock{mutex_};
            if (outstandingPackets_.erase(packetState) == 0U) {
                return;
            }

            ++releasingPacketCount_;
            state.reset(packetState);
        }

        state.reset();

        std::optional<asharia::VulkanContext> contextToDestroy;
        {
            std::lock_guard lock{mutex_};
            --releasingPacketCount_;
            contextToDestroy = takeContextForShutdownIfIdleLocked();
        }
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
