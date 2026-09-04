#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"

#include "editor_shared_viewport_render_producer.hpp"

namespace asharia::editor {

    enum class EditorSharedViewportRenderFrameErrorKind {
        Unavailable,
        RenderFailed,
        Backpressure,
    };

    enum class EditorSharedViewportPresentCompletionKind {
        NotSubmittedToConsumer,
        ConsumerAccessed,
    };

    struct EditorSharedViewportRenderFrameError {
        EditorSharedViewportRenderFrameErrorKind kind{
            EditorSharedViewportRenderFrameErrorKind::RenderFailed};
        asharia::Error error;
    };

    using EditorSharedViewportRenderFrameResult =
        std::expected<EditorSharedViewportPresentPacket, EditorSharedViewportRenderFrameError>;

    using EditorSharedViewportStreamId = std::uint64_t;

    class EditorSharedViewportFrameClock final {
    public:
        using Clock = std::chrono::steady_clock;

        explicit EditorSharedViewportFrameClock(Clock::time_point epoch = Clock::now()) noexcept
            : epoch_(epoch) {}

        void reset(Clock::time_point epoch = Clock::now()) noexcept {
            epoch_ = epoch;
            lastRenderedAt_.reset();
        }
        [[nodiscard]] BasicRenderViewFrameParams
        frameParams(std::uint64_t frameIndex, Clock::time_point now = Clock::now()) const noexcept {
            const Clock::time_point sampledAt = std::max(now, epoch_);
            const Clock::time_point previous =
                lastRenderedAt_ ? std::min(*lastRenderedAt_, sampledAt) : sampledAt;
            return BasicRenderViewFrameParams{
                .frameIndex = frameIndex,
                .timeSeconds = std::chrono::duration<float>{sampledAt - epoch_}.count(),
                .deltaSeconds = std::chrono::duration<float>{sampledAt - previous}.count(),
                .renderScale = 1.0F,
            };
        }
        void markRendered(Clock::time_point now = Clock::now()) noexcept {
            const Clock::time_point sampledAt = std::max(now, epoch_);
            lastRenderedAt_ = lastRenderedAt_ ? std::max(*lastRenderedAt_, sampledAt) : sampledAt;
        }

    private:
        Clock::time_point epoch_;
        std::optional<Clock::time_point> lastRenderedAt_;
    };

    struct EditorSharedViewportReadyFrame {
        EditorSharedViewportPresentPacket present;
        std::array<std::uint64_t, 2> sessionId{};
        std::array<std::uint64_t, 2> targetId{};
        std::uint64_t targetRevision{};
        std::uint64_t requestSequence{};
        std::uint64_t viewStateRevision{};
        EditorViewportKind kind{EditorViewportKind::Scene};
        EditorExtent2D logicalExtent;
        EditorSharedViewportSceneMeshReceipt sceneMeshReceipt;
    };

    enum class EditorSharedViewportStreamLifecycle {
        Open,
        Closing,
        Closed,
        Faulted,
    };

    struct EditorSharedViewportStreamSnapshot {
        EditorSharedViewportStreamLifecycle lifecycle{EditorSharedViewportStreamLifecycle::Open};
        bool hasPendingLatest{};
        bool hasReadyFrame{};
        bool renderExecuting{};
        std::size_t slotCount{};
        std::size_t presentedSlotCount{};
        std::uint64_t submittedRequests{};
        std::uint64_t coalescedRequests{};
        std::uint64_t renderedFrames{};
    };

    struct EditorSharedViewportDeviceSnapshot {
        std::uint32_t vendorId{};
        std::uint32_t deviceId{};
        bool fillModeNonSolid{};
        asharia::VulkanDeviceIdentity identity;
    };

    enum class EditorSharedViewportRuntimeLifecycle {
        Starting,
        Running,
        Draining,
        Stopped,
        Faulted,
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
        VkExtent2D lastRenderExtent{};
        std::uint32_t lastDebugProxyCount{};
        std::uint64_t lastDebugWorldLineCount{};
        bool lastWorldGridEnabled{};
        std::size_t maxOutstandingPackets{};
        std::size_t outstandingPackets{};
        bool hasContext{};
        bool hasRenderProducer{};
        bool shutdownRequested{};
        std::uint64_t renderQueueBackpressureHits{};
        std::uint64_t renderThreadDispatches{};
        std::size_t maxQueuedRenderCommands{};
        std::size_t maxObservedQueuedRenderCommands{};
        std::size_t queuedRenderCommands{};
        std::thread::id renderThreadId;
        EditorSharedViewportRuntimeLifecycle lifecycle{
            EditorSharedViewportRuntimeLifecycle::Starting};
        bool renderThreadRunning{};
        bool renderThreadJoined{};
    };

    class EditorSharedViewportRuntime final {
    public:
        [[nodiscard]] static EditorSharedViewportRuntime& instance();
        [[nodiscard]] asharia::Result<EditorSharedViewportDeviceSnapshot> ensureDeviceSnapshot();
        [[nodiscard]] asharia::Result<EditorSharedViewportStreamId>
        openStream(bool supportsWireframe);
        [[nodiscard]] asharia::Result<void> validateSceneRasterMode(
            EditorSharedViewportStreamId streamId,
            EditorSharedViewportSceneRasterMode rasterMode);
        [[nodiscard]] asharia::Result<void> submitLatest(EditorSharedViewportStreamId streamId,
                                                         EditorSharedViewportPresentDesc desc);
        [[nodiscard]] asharia::Result<std::optional<EditorSharedViewportReadyFrame>>
        tryTakeReady(EditorSharedViewportStreamId streamId);
        [[nodiscard]] asharia::Result<void>
        completeFrame(EditorSharedViewportStreamId streamId, void* nativeSlot,
                      EditorSharedViewportPresentCompletionKind completionKind);
        [[nodiscard]] asharia::Result<void> releaseSlotImport(EditorSharedViewportStreamId streamId,
                                                              void* nativeSlot);
        [[nodiscard]] asharia::Result<void>
        requestCloseStream(EditorSharedViewportStreamId streamId);
        [[nodiscard]] asharia::Result<EditorSharedViewportStreamSnapshot>
        pollStream(EditorSharedViewportStreamId streamId);
        [[nodiscard]] asharia::Result<void>
        destroyClosedStream(EditorSharedViewportStreamId streamId);
        [[nodiscard]] EditorSharedViewportRenderFrameResult
        renderSceneViewFrame(EditorSharedViewportPresentDesc desc);
        [[nodiscard]] EditorSharedViewportRenderFrameResult
        createPresentSlot(EditorSharedViewportPresentDesc desc);
        [[nodiscard]] EditorSharedViewportRenderFrameResult
        renderPresentSlot(void* nativeSlot, EditorSharedViewportPresentDesc desc);
        [[nodiscard]] asharia::Result<void>
        releasePresentPacket(void* nativePacket,
                             EditorSharedViewportPresentCompletionKind completionKind);
        void shutdown();
        [[nodiscard]] EditorSharedViewportRuntimeStats stats();

    private:
        struct RenderFramePacket {
            std::string panelId;
            EditorViewportKind kind{EditorViewportKind::Scene};
            EditorExtent2D logicalExtent;
            EditorExtent2D allocationExtent;
            EditorSharedViewportExternalImageHandleFamily imageHandleFamily{
                EditorSharedViewportExternalImageHandleFamily::VulkanOpaqueNt};
            bool hasScene{};
            std::uint64_t sceneRevision{};
            std::array<std::uint64_t, 2> sessionId{};
            std::array<std::uint64_t, 2> targetId{};
            std::uint64_t requestSequence{};
            std::uint64_t viewStateRevision{};
            bool hasCamera{};
            EditorViewportCamera camera;
            std::vector<EditorSharedViewportDebugProxy> debugProxies;
            std::vector<EditorSharedViewportAuthoredMeshSnapshot> authoredMeshes;
            EditorSharedViewportSceneRasterMode sceneRasterMode{
                EditorSharedViewportSceneRasterMode::Solid};
            bool captureSceneMeshEvidence{};
            bool flashSentinelCorners{};
            bool hasSelectionOutline{};
            std::array<std::uint8_t, 16> selectedObjectId{};
            bool hasTranslateGizmo{};
            std::array<std::uint64_t, 2> translateGizmoObjectId{};
            std::array<float, 3> translateGizmoPosition{};
            EditorSharedViewportGizmoAxis translateGizmoHoveredAxis{
                EditorSharedViewportGizmoAxis::None};
            EditorSharedViewportGizmoAxis translateGizmoActiveAxis{
                EditorSharedViewportGizmoAxis::None};

            [[nodiscard]] static RenderFramePacket copyOf(EditorSharedViewportPresentDesc desc);
            [[nodiscard]] EditorSharedViewportPresentDesc view() const;
        };

        struct RetiringPacket {
            std::unique_ptr<EditorSharedViewportPacketState> state;
            bool quarantined{};
        };

        enum class StreamSlotPhase {
            Available,
            Ready,
            Presented,
            Completing,
            Retired,
        };

        struct StreamSlot {
            void* nativeSlot{};
            StreamSlotPhase phase{StreamSlotPhase::Available};
            EditorSharedViewportPresentCompletionKind completionKind{
                EditorSharedViewportPresentCompletionKind::NotSubmittedToConsumer};
            bool importExposed{};
            bool importReleased{true};
            bool consumerAccessed{};
            std::uint64_t requestSequence{};
        };

        struct StreamReadyFrame {
            std::size_t slotIndex{};
            EditorSharedViewportReadyFrame frame;
        };

        struct StreamState {
            mutable std::mutex mutex;
            std::optional<EditorExtent2D> allocationExtent;
            std::optional<RenderFramePacket> pendingLatest;
            std::optional<StreamReadyFrame> readyFrame;
            std::vector<StreamSlot> slots;
            bool renderExecuting{};
            bool closeRequested{};
            bool closed{};
            bool faulted{};
            bool supportsWireframe{};
            std::uint64_t submittedRequests{};
            std::uint64_t coalescedRequests{};
            std::uint64_t renderedFrames{};
        };

        EditorSharedViewportRuntime();

        [[nodiscard]] asharia::Result<EditorSharedViewportDeviceSnapshot>
        ensureDeviceSnapshotOnRenderThread();
        [[nodiscard]] EditorSharedViewportRenderFrameResult
        renderSceneViewFrameOnRenderThread(const RenderFramePacket& packet);
        [[nodiscard]] EditorSharedViewportRenderFrameResult
        createPresentSlotOnRenderThread(const RenderFramePacket& packet);
        [[nodiscard]] EditorSharedViewportRenderFrameResult
        renderPresentSlotOnRenderThread(void* nativeSlot, const RenderFramePacket& packet);
        [[nodiscard]] asharia::Result<void> releasePresentPacketOnRenderThread(
            void* nativePacket, EditorSharedViewportPresentCompletionKind completionKind);

        [[nodiscard]] asharia::Result<EditorSharedViewportRenderProducer*>
        ensureRenderProducerOnRenderThread();
        [[nodiscard]] asharia::Result<void> retireCompletedPresentSlotsOnRenderThread();
        [[nodiscard]] bool pollRetiringPacketsOnRenderThread();
        void
        retainRetiringPacketOnRenderThread(std::unique_ptr<EditorSharedViewportPacketState> state,
                                           bool quarantined);
        [[nodiscard]] std::size_t retiringPacketCountOnRenderThread() const;
        [[nodiscard]] std::optional<std::size_t> availableFrameResourceIndexOnRenderThread() const;
        [[nodiscard]] bool hasPollableRetirementOnRenderThread() const;
        [[nodiscard]] bool hasQuarantinedRetirementOnRenderThread() const;
        [[nodiscard]] bool tryFinishShutdownOnRenderThread();
        [[nodiscard]] bool dispatchOneStreamWorkOnRenderThread();
        [[nodiscard]] static bool streamHasWorkLocked(const StreamState& stream,
                                                      bool canAllocateSlot);
        [[nodiscard]] bool processStreamCompletionsOnRenderThread(StreamState& stream);
        [[nodiscard]] bool processStreamCloseOnRenderThread(StreamState& stream);
        [[nodiscard]] bool renderPendingStreamFrameOnRenderThread(StreamState& stream);
        [[nodiscard]] bool hasStreamWork() const;
        [[nodiscard]] std::shared_ptr<StreamState>
        findStream(EditorSharedViewportStreamId streamId) const;
        void beginShutdownOnRenderThread();
        void publishRuntimeStatsOnRenderThread();
        void renderThreadMain();
        void renderThreadLoop();
        [[nodiscard]] std::packaged_task<void()> waitForNextWorkOnRenderThread();
        [[nodiscard]] asharia::Result<void> ensureRenderThreadStarted();
        void joinRenderThreadIfTerminal();

        [[nodiscard]] bool enqueueRenderWork(std::packaged_task<void()> work);
        [[nodiscard]] bool enqueueControlWork(std::packaged_task<void()> work);
        [[nodiscard]] bool enqueueReleaseWork(std::packaged_task<void()> work);
        [[nodiscard]] bool isOnRenderThread() const noexcept;

        static constexpr std::size_t kMaxOutstandingPackets = 4U;
        static constexpr std::size_t kMaxOutstandingLegacyPackets = 1U;
        static constexpr std::size_t kMaxStreamSlots = 3U;
        static constexpr std::size_t kMaxQueuedRenderCommands = 4U;
        static constexpr std::size_t kMaxQueuedControlCommands = 4U;
        static constexpr std::size_t kMaxQueuedReleaseCommands = kMaxOutstandingPackets;

        mutable std::mutex queueMutex_;
        std::condition_variable queueReady_;
        std::condition_variable queueSpaceAvailable_;
        std::condition_variable lifecycleChanged_;
        std::deque<std::packaged_task<void()>> renderQueue_;
        std::deque<std::packaged_task<void()>> controlQueue_;
        std::deque<std::packaged_task<void()>> releaseQueue_;
        bool releaseAdmissionClosed_{};
        std::atomic<bool> shutdownRequestedByCaller_{};
        std::atomic<EditorSharedViewportRuntimeLifecycle> lifecycle_{
            EditorSharedViewportRuntimeLifecycle::Starting};
        std::atomic<std::uint64_t> renderQueueBackpressureHits_{};
        std::atomic<std::size_t> maxObservedQueuedRenderCommands_{};
        std::atomic<std::size_t> outstandingPacketCount_{};
        mutable std::mutex publishedStateMutex_;
        EditorSharedViewportRuntimeStats publishedStats_;
        std::optional<EditorSharedViewportDeviceSnapshot> publishedDeviceSnapshot_;
        mutable std::mutex threadMutex_;
        std::thread renderThread_;
        std::thread::id renderThreadId_;
        mutable std::mutex streamsMutex_;
        std::unordered_map<EditorSharedViewportStreamId, std::shared_ptr<StreamState>> streams_;
        std::atomic<EditorSharedViewportStreamId> nextStreamId_{1U};
        // Render-thread-only cursor. Registry iteration order must never define
        // which viewport receives the next owner-loop transition.
        EditorSharedViewportStreamId lastDispatchedStreamId_{};

        // These objects are render-thread-owned. Vulkan calls and object
        // destruction must never move back to a C ABI caller thread.
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
        EditorSharedViewportFrameClock frameClock_;
        std::uint64_t renderThreadDispatches_{};
        bool shutdownRequested_{};
        bool terminalQuarantine_{};
    };

} // namespace asharia::editor
