#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
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

    struct EditorSharedViewportDeviceSnapshot {
        std::uint32_t vendorId{};
        std::uint32_t deviceId{};
        asharia::VulkanDeviceIdentity identity;
    };

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
        std::uint64_t sceneFramesRendered{};
        std::uint64_t gameFramesRendered{};
        std::uint64_t previewFramesRendered{};
        std::uint64_t lastSceneRevision{};
        std::uint64_t lastRequestSequence{};
        std::array<std::uint64_t, 2> lastSessionId{};
        std::array<std::uint64_t, 2> lastTargetId{};
        EditorViewportKind lastRenderKind{EditorViewportKind::Scene};
        std::uint32_t lastDebugProxyCount{};
        std::uint64_t lastDebugWorldLineCount{};
        bool lastWorldGridEnabled{};
        std::size_t maxOutstandingPackets{};
        std::size_t outstandingPackets{};
        bool hasContext{};
        bool hasRenderProducer{};
        bool shutdownRequested{};
    };

    class EditorSharedViewportRuntime final {
    public:
        [[nodiscard]] static EditorSharedViewportRuntime& instance();
        [[nodiscard]] asharia::Result<EditorSharedViewportDeviceSnapshot> ensureDeviceSnapshot();
        [[nodiscard]] EditorSharedViewportRenderFrameResult
        renderSceneViewFrame(EditorSharedViewportPresentDesc desc);
        [[nodiscard]] EditorSharedViewportRenderFrameResult
        createPresentSlot(EditorSharedViewportPresentDesc desc);
        [[nodiscard]] EditorSharedViewportRenderFrameResult
        renderPresentSlot(void* nativeSlot, EditorSharedViewportPresentDesc desc);
        void releasePresentPacket(void* nativePacket);
        void shutdown();
        [[nodiscard]] EditorSharedViewportRuntimeStats stats();

    private:
        struct RetiringPacket {
            std::unique_ptr<EditorSharedViewportPacketState> state;
            bool quarantined{};
        };

        [[nodiscard]] asharia::Result<EditorSharedViewportRenderProducer*>
        ensureRenderProducerLocked();
        [[nodiscard]] std::optional<asharia::VulkanContext> takeContextForShutdownIfIdleLocked();
        [[nodiscard]] asharia::Result<void> retireCompletedPresentSlotsLocked();
        void pollRetiringPacketsLocked();
        void retainRetiringPacketLocked(std::unique_ptr<EditorSharedViewportPacketState> state,
                                        bool quarantined);
        [[nodiscard]] std::size_t retiringPacketCountLocked() const;
        [[nodiscard]] std::optional<std::size_t> availableFrameResourceIndexLocked() const;

        static constexpr std::size_t kMaxOutstandingPackets = 4U;
        static constexpr std::size_t kMaxOutstandingLegacyPackets = 1U;

        mutable std::mutex mutex_;
        std::optional<asharia::VulkanContext> context_;
        std::optional<EditorSharedViewportRenderProducer> renderProducer_;
        std::unordered_set<EditorSharedViewportPacketState*> outstandingPackets_;
        std::array<RetiringPacket, kMaxOutstandingPackets> retiringPackets_;
        std::size_t outstandingLegacyPackets_{};
        std::uint64_t producersCreated_{};
        std::uint64_t framesRendered_{};
        std::uint64_t packetsCreated_{};
        std::uint64_t packetBackpressureHits_{};
        std::uint64_t nextFrameIndex_{};
        bool shutdownRequested_{};
    };

} // namespace asharia::editor
