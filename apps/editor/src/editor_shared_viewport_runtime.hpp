#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <optional>
#include <unordered_set>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"

#include "editor_shared_viewport_render_producer.hpp"

namespace asharia::editor {

    enum class EditorSharedViewportRenderFrameErrorKind {
        RenderFailed,
        Backpressure,
    };

    struct EditorSharedViewportRenderFrameError {
        EditorSharedViewportRenderFrameErrorKind kind{
            EditorSharedViewportRenderFrameErrorKind::RenderFailed};
        asharia::Error error;
    };

    using EditorSharedViewportRenderFrameResult =
        std::expected<EditorSharedViewportPresentPacket, EditorSharedViewportRenderFrameError>;

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
        std::uint64_t frameEpochsSubmitted{};
        std::uint64_t frameEpochsCompleted{};
        std::uint64_t frameEpochsPending{};
        std::uint64_t rendererCreations{};
        std::uint64_t packetBackpressureHits{};
        std::size_t maxOutstandingPackets{};
        std::size_t outstandingPackets{};
        bool hasContext{};
        bool hasRenderProducer{};
        bool shutdownRequested{};
    };

    class EditorSharedViewportRuntime final {
    public:
        [[nodiscard]] static EditorSharedViewportRuntime& instance();
        [[nodiscard]] asharia::Result<const asharia::VulkanContext*> ensureContext();
        [[nodiscard]] EditorSharedViewportRenderFrameResult
        renderSceneViewFrame(EditorSharedViewportPresentDesc desc);
        void releasePresentPacket(void* nativePacket);
        void shutdown();
        [[nodiscard]] EditorSharedViewportRuntimeStats stats() const;

    private:
        [[nodiscard]] asharia::Result<EditorSharedViewportRenderProducer*>
        ensureRenderProducerLocked();
        [[nodiscard]] std::optional<asharia::VulkanContext>
        takeContextForShutdownIfIdleLocked();

        static constexpr std::size_t kMaxOutstandingPackets = 1U;

        mutable std::mutex mutex_;
        std::optional<asharia::VulkanContext> context_;
        std::optional<EditorSharedViewportRenderProducer> renderProducer_;
        std::unordered_set<EditorSharedViewportPacketState*> outstandingPackets_;
        std::size_t releasingPacketCount_{};
        std::uint64_t producersCreated_{};
        std::uint64_t framesRendered_{};
        std::uint64_t packetsCreated_{};
        std::uint64_t packetBackpressureHits_{};
        std::uint64_t nextFrameIndex_{};
        bool shutdownRequested_{};
    };

} // namespace asharia::editor
