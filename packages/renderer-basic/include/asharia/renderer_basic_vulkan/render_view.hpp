#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/rendergraph/render_graph_diagnostics.hpp"

namespace asharia {

    struct BasicOffscreenViewportTarget {
        VkImage image{VK_NULL_HANDLE};
        VkImageView imageView{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkImageLayout sampledLayout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    };

    enum class BasicRenderViewTargetFinalUsage {
        Present,
        SampledTexture,
    };

    struct BasicRenderViewTarget {
        VkImage image{VK_NULL_HANDLE};
        VkImageView imageView{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
        BasicRenderViewTargetFinalUsage finalUsage{BasicRenderViewTargetFinalUsage::Present};
    };

    enum class BasicRenderViewKind {
        Game,
        Scene,
        Preview,
    };

    using BasicRenderViewMatrix4x4 = std::array<float, 16>;

    [[nodiscard]] constexpr BasicRenderViewMatrix4x4 basicRenderViewIdentityMatrix() {
        return BasicRenderViewMatrix4x4{
            1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
        };
    }

    struct BasicRenderViewCamera {
        BasicRenderViewMatrix4x4 view{basicRenderViewIdentityMatrix()};
        BasicRenderViewMatrix4x4 projection{basicRenderViewIdentityMatrix()};
        BasicRenderViewMatrix4x4 viewProjection{basicRenderViewIdentityMatrix()};
        std::array<float, 3> position{};
        float nearPlane{0.1F};
        float farPlane{1000.0F};
    };

    struct BasicRenderViewFrameParams {
        std::uint64_t frameIndex{};
        float timeSeconds{};
        float deltaSeconds{};
        float renderScale{1.0F};
    };

    struct BasicDebugWorldLine {
        std::array<float, 3> start{};
        std::array<float, 3> end{};
        std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
    };

    enum class BasicRenderViewOverlayColorLoadOp {
        LoadSceneColor,
        Clear,
    };

    enum class BasicRenderViewOverlayColorStoreOp {
        Store,
        Discard,
    };

    enum class BasicRenderViewOverlayBlendMode {
        AlphaBlend,
        Additive,
    };

    struct BasicRenderViewWorldGridDesc {
        bool enabled{};
        float planeY{};
        float minorSpacing{1.0F};
        float majorSpacing{10.0F};
        float fadeStart{32.0F};
        float fadeEnd{128.0F};
        float opacity{1.0F};
    };

    struct BasicRenderViewOverlayDesc {
        bool enabled{};
        BasicRenderViewOverlayColorLoadOp colorLoadOp{
            BasicRenderViewOverlayColorLoadOp::LoadSceneColor};
        BasicRenderViewOverlayColorStoreOp colorStoreOp{BasicRenderViewOverlayColorStoreOp::Store};
        BasicRenderViewOverlayBlendMode blendMode{BasicRenderViewOverlayBlendMode::AlphaBlend};
        BasicRenderViewWorldGridDesc worldGrid;
        std::span<const BasicDebugWorldLine> debugWorldLines{};
    };

    struct BasicRenderViewOverlayDiagnostics {
        bool enabled{};
        BasicRenderViewOverlayColorLoadOp colorLoadOp{
            BasicRenderViewOverlayColorLoadOp::LoadSceneColor};
        BasicRenderViewOverlayColorStoreOp colorStoreOp{BasicRenderViewOverlayColorStoreOp::Store};
        BasicRenderViewOverlayBlendMode blendMode{BasicRenderViewOverlayBlendMode::AlphaBlend};
        bool worldGridEnabled{};
        std::uint64_t debugWorldLineCount{};
    };

    enum class BasicRenderViewExecutionEventKind {
        BeginPass,
        EndPass,
        ClearColor,
        Draw,
        DrawIndexed,
        DrawFullscreenTriangle,
        Dispatch,
        CopyImage,
        RenderViewInput,
    };

    struct BasicRenderViewExecutionEventId {
        std::uint64_t value{};

        [[nodiscard]] friend bool operator==(BasicRenderViewExecutionEventId,
                                             BasicRenderViewExecutionEventId) = default;
    };

    struct BasicRenderViewDrawEvent {
        std::uint32_t vertexCount{};
        std::uint32_t indexCount{};
        std::uint32_t instanceCount{};
        std::uint32_t firstVertex{};
        std::uint32_t firstIndex{};
        std::int32_t vertexOffset{};
        std::uint32_t firstInstance{};
    };

    struct BasicRenderViewDispatchEvent {
        std::uint32_t groupCountX{};
        std::uint32_t groupCountY{};
        std::uint32_t groupCountZ{};
    };

    struct BasicRenderViewExecutionEvent {
        BasicRenderViewExecutionEventId id;
        BasicRenderViewExecutionEventKind kind{BasicRenderViewExecutionEventKind::BeginPass};
        std::size_t passIndex{};
        std::size_t declarationIndex{};
        std::optional<std::size_t> commandIndex;
        std::string passName;
        std::string label;
        BasicRenderViewDrawEvent draw;
        BasicRenderViewDispatchEvent dispatch;
        std::optional<std::uint32_t> sourceImageResourceIndex;
        std::optional<std::uint32_t> targetImageResourceIndex;
    };

    struct BasicRenderViewDiagnostics {
        std::string viewName;
        BasicRenderViewKind viewKind{BasicRenderViewKind::Game};
        BasicRenderViewCamera camera;
        BasicRenderViewFrameParams frameParams;
        BasicRenderViewOverlayDiagnostics overlay;
        RenderGraphDiagnosticsSnapshot renderGraph;
        std::vector<BasicRenderViewExecutionEvent> executionEvents;
    };

    enum class BasicDebugPreviewStatus {
        NotRequested,
        Available,
        Unavailable,
    };

    struct BasicDebugPreviewResult {
        BasicDebugPreviewStatus status{BasicDebugPreviewStatus::NotRequested};
        std::uint32_t sourceImageResourceIndex{};
        std::optional<std::size_t> copiedAfterPassIndex;
        std::string message;
        std::uint64_t copiesRecorded{};
    };

    struct BasicDebugPreviewRequest {
        std::uint32_t sourceImageResourceIndex{};
        std::optional<std::size_t> afterPassIndex;
        BasicRenderViewTarget target{};
        BasicDebugPreviewResult* result{};
    };

    struct BasicRenderViewDesc {
        BasicRenderViewTarget target{};
        BasicRenderViewKind viewKind{BasicRenderViewKind::Game};
        BasicRenderViewCamera camera;
        BasicRenderViewFrameParams frameParams;
        BasicRenderViewOverlayDesc overlay;
        std::string_view viewName{"RenderView"};
        BasicRenderViewDiagnostics* diagnostics{};
        BasicDebugPreviewRequest* debugPreview{};
    };

} // namespace asharia
