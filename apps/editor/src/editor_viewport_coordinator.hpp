#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/renderer_basic_vulkan/basic_renderers.hpp"
#include "asharia/rhi_vulkan/vma_fwd.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/rhi_vulkan/vulkan_image.hpp"

#include "editor_render_graph_snapshot.hpp"
#include "editor_viewport.hpp"
#include "imgui_texture_registry.hpp"

namespace asharia::editor {

    class EditorFrameDebugger;

    struct EditorViewportFrameEpochs {
        std::uint64_t completedFrameEpoch{};
        std::uint64_t submittedFrameEpoch{};
    };

    struct EditorViewportCoordinatorStats {
        std::uint64_t renderTargetsRetired{};
        std::uint64_t renderTargetsDeferred{};
        std::uint64_t overlayFlagFramesRendered{};
        std::uint64_t overlayFlagTextureFramesAcquired{};
        std::uint64_t sceneViewOnlyFlagRequestsDiscarded{};
        std::uint64_t renderViewDiagnosticsFramesRecorded{};
        std::uint64_t repaintReasonFramesRecorded{};
        std::uint64_t idleSceneViewFramesSkipped{};
        std::uint64_t liveRenderGraphViewFrames{};
        std::uint64_t liveRenderGraphSnapshotFrames{};
        std::uint64_t frameDebugPreviewFramesRecorded{};
        std::uint64_t frameDebugPreviewUnavailableFrames{};
        std::uint64_t frameDebugPreviewTexturesPublished{};
        std::uint64_t lastRenderViewDiagnosticsPasses{};
        std::uint64_t lastRenderViewDiagnosticsResources{};
        std::uint64_t lastRenderViewDiagnosticsAccessEdges{};
        std::uint64_t lastRenderViewDiagnosticsDependencyEdges{};
        std::uint64_t lastRenderViewDiagnosticsTransitions{};
        std::uint64_t lastRenderViewDiagnosticsExecutionEvents{};
        asharia::BasicRenderViewKind lastRenderViewDiagnosticsKind{
            asharia::BasicRenderViewKind::Scene};
        std::uint64_t lastRenderViewDiagnosticsFrameIndex{};
        bool lastRenderViewDiagnosticsOverlayEnabled{};
        std::uint64_t lastRenderViewDiagnosticsDebugWorldLines{};
        std::array<float, 3> lastRenderViewDiagnosticsCameraPosition{};
        float lastRenderViewDiagnosticsCameraNearPlane{};
        float lastRenderViewDiagnosticsCameraFarPlane{};
        float lastRenderViewDiagnosticsCameraProjectionXScale{};
        float lastRenderViewDiagnosticsCameraProjectionYScale{};
        float lastRenderViewDiagnosticsCameraViewProjectionDepthScale{};
    };

    struct EditorRecordedRenderViewDiagnostics {
        EditorViewportKind kind{EditorViewportKind::Scene};
        EditorExtent2D requestedExtent;
        std::uint64_t submittedFrameEpoch{};
        asharia::BasicRenderViewDiagnostics diagnostics;
    };

    [[nodiscard]] EditorViewportFrameEpochs
    editorViewportFrameEpochs(const asharia::VulkanFrameLoop& frameLoop);

    class EditorViewportCoordinator final : public EditorViewportPanelHost,
                                            public EditorRenderGraphSnapshotProvider {
        struct ViewportTexture {
            ViewportTexture() = default;
            ViewportTexture(const ViewportTexture&) = delete;
            ViewportTexture& operator=(const ViewportTexture&) = delete;
            ViewportTexture(ViewportTexture&&) noexcept = default;
            ViewportTexture& operator=(ViewportTexture&&) noexcept = default;
            ~ViewportTexture() = default;

            [[nodiscard]] bool ready() const;
            void clearMetadata();

            asharia::VulkanRenderTarget target;
            VkFormat format{VK_FORMAT_UNDEFINED};
            VkExtent2D extent{};
            EditorId panelId;
            EditorViewportKind kind{EditorViewportKind::Scene};
            EditorExtent2D requestedExtent;
            EditorViewportOverlayFlags overlayFlags;
            asharia::BasicRenderViewDiagnostics diagnostics;
            std::uint64_t frameIndex{};
            bool rendered{false};
        };

    public:
        EditorViewportCoordinator() = default;
        EditorViewportCoordinator(const EditorViewportCoordinator&) = delete;
        EditorViewportCoordinator& operator=(const EditorViewportCoordinator&) = delete;
        EditorViewportCoordinator(EditorViewportCoordinator&&) = delete;
        EditorViewportCoordinator& operator=(EditorViewportCoordinator&&) = delete;
        ~EditorViewportCoordinator() override;

        [[nodiscard]] asharia::VoidResult create(const asharia::VulkanContext& context);
        void beginImguiFrame(EditorViewportFrameEpochs epochs);
        void requestViewport(EditorViewportRequest request) override;
        [[nodiscard]] std::optional<EditorViewportResult>
        acquireViewportTextureForDraw(std::string_view panelId) override;
        [[nodiscard]] asharia::Result<asharia::VulkanFrameRecordResult>
        recordRequestedViews(const asharia::VulkanFrameRecordContext& frame,
                             asharia::BasicFullscreenTextureRenderer& renderer,
                             bool recordRenderViews = true,
                             EditorViewportRepaintReasons repaintReasons = {});
        [[nodiscard]] asharia::Result<asharia::VulkanFrameRecordResult>
        recordFrameDebugPreview(const asharia::VulkanFrameRecordContext& frame,
                                asharia::BasicFullscreenTextureRenderer& renderer,
                                EditorFrameDebugger& frameDebugger);
        void shutdown();

        [[nodiscard]] bool hasPresentedViewportTexture() const;
        [[nodiscard]] VkExtent2D descriptorExtent() const;
        [[nodiscard]] std::uint64_t viewportFramesRendered() const;
        [[nodiscard]] std::uint64_t textureFramesSubmitted() const;
        [[nodiscard]] EditorViewportCoordinatorStats stats() const;
        [[nodiscard]] ImGuiTextureRegistryStats textureRegistryStats() const;
        [[nodiscard]] const std::optional<EditorRecordedRenderViewDiagnostics>&
        latestRecordedRenderViewDiagnostics() const;
        [[nodiscard]] std::optional<EditorRenderGraphSnapshot>
        latestLiveRenderGraphSnapshot() const override;
        void notifyLiveRenderGraphViewDrawn(bool snapshotVisible) override;

    private:
        void promotePendingTexture();
        [[nodiscard]] bool hasTextureToRelease() const;
        [[nodiscard]] asharia::VoidResult
        processRetiredTextures(const asharia::VulkanFrameRecordContext& frame);

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VkQueue queue_{VK_NULL_HANDLE};
        ImGuiTextureRegistry textureRegistry_;
        ViewportTexture presentedTexture_;
        ViewportTexture pendingTexture_;
        ViewportTexture debugReplayTexture_;
        ViewportTexture debugPreviewTexture_;
        std::vector<ViewportTexture> retiredTextures_;
        std::optional<EditorViewportRequest> requestedViewport_;
        std::optional<EditorRecordedRenderViewDiagnostics> latestRecordedDiagnostics_;
        EditorViewportCoordinatorStats stats_;
        std::uint64_t currentFrameSubmittedEpoch_{};
        std::uint64_t viewportFramesRendered_{0};
        std::uint64_t textureFramesSubmitted_{0};
    };

} // namespace asharia::editor
