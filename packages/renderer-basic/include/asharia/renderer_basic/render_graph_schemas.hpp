#pragma once

#include <array>
#include <cstdint>

#include "asharia/renderer_basic/draw_item.hpp"
#include "asharia/rendergraph/render_graph_execution.hpp"

namespace asharia {

    inline constexpr char kBasicTransferClearPassType[] = "builtin.transfer-clear";
    inline constexpr char kBasicTransferClearParamsType[] = "builtin.transfer-clear.params";
    inline constexpr char kBasicDynamicClearPassType[] = "builtin.dynamic-clear";
    inline constexpr char kBasicDynamicClearParamsType[] = "builtin.dynamic-clear.params";
    inline constexpr char kBasicTransientPresentPassType[] = "builtin.transient-present";
    inline constexpr char kBasicTransientPresentParamsType[] = "builtin.transient-present.params";
    inline constexpr char kBasicRasterTrianglePassType[] = "builtin.raster-triangle";
    inline constexpr char kBasicRasterTriangleParamsType[] = "builtin.raster-triangle.params";
    inline constexpr char kBasicRasterDepthTrianglePassType[] = "builtin.raster-depth-triangle";
    inline constexpr char kBasicRasterDepthTriangleParamsType[] =
        "builtin.raster-depth-triangle.params";
    inline constexpr char kBasicRasterMesh3DPassType[] = "builtin.raster-mesh3d";
    inline constexpr char kBasicRasterMesh3DParamsType[] = "builtin.raster-mesh3d.params";
    inline constexpr char kBasicRasterMrtPassType[] = "builtin.raster-mrt";
    inline constexpr char kBasicRasterMrtParamsType[] = "builtin.raster-mrt.params";
    inline constexpr char kBasicRasterFullscreenPassType[] = "builtin.raster-fullscreen";
    inline constexpr char kBasicRasterFullscreenParamsType[] = "builtin.raster-fullscreen.params";
    inline constexpr char kBasicRenderViewWorldGridPassType[] = "builtin.render-view-world-grid";
    inline constexpr char kBasicRenderViewWorldGridParamsType[] =
        "builtin.render-view-world-grid.params";
    inline constexpr char kBasicRenderViewSceneInputsPassType[] =
        "builtin.render-view-scene-inputs";
    inline constexpr char kBasicRenderViewSceneInputsParamsType[] =
        "builtin.render-view-scene-inputs.params";
    inline constexpr char kBasicRenderViewOverlayPassType[] = "builtin.render-view-overlay";
    inline constexpr char kBasicRenderViewOverlayParamsType[] =
        "builtin.render-view-overlay.params";
    inline constexpr char kBasicRasterDrawListPassType[] = "builtin.raster-draw-list";
    inline constexpr char kBasicRasterDrawListParamsType[] = "builtin.raster-draw-list.params";
    inline constexpr char kBasicComputeDispatchPassType[] = "builtin.compute-dispatch";
    inline constexpr char kBasicComputeDispatchParamsType[] = "builtin.compute-dispatch.params";
    inline constexpr char kBasicComputeReadbackPassType[] = "builtin.compute-readback";
    inline constexpr char kBasicTransferFillBufferPassType[] = "builtin.transfer-fill-buffer";
    inline constexpr char kBasicTransferFillBufferParamsType[] =
        "builtin.transfer-fill-buffer.params";
    inline constexpr char kBasicDebugImageCopyPassType[] = "builtin.debug-image-copy";

    struct BasicTransferClearParams {
        std::array<float, 4> color{};
    };

    struct BasicFullscreenParams {
        std::array<float, 4> tint{};
    };

    struct BasicRenderViewOverlayParams {
        std::array<float, 4> cameraPositionNear{};
        std::array<float, 4> frameTimeScale{};
        std::uint32_t debugWorldLineCount{};
        std::uint32_t viewKind{};
        std::uint32_t overlayEnabled{};
        std::uint32_t reserved{};
    };

    struct BasicRenderViewWorldGridParams {
        std::array<float, 4> cameraPositionNear{};
        std::array<float, 4> viewportFade{};
        std::array<float, 4> gridSettings{};
        std::array<float, 4> gridLodSettings{};
        std::array<float, 4> gridColor{};
        std::uint32_t viewKind{};
        std::uint32_t enabled{};
        std::uint32_t reserved0{};
        std::uint32_t reserved1{};
    };

    struct BasicRenderViewSceneInputsParams {
        std::uint32_t drawItemCount{};
        std::uint32_t viewKind{};
        std::uint32_t reserved0{};
        std::uint32_t reserved1{};
    };

    struct BasicDrawListParams {
        std::uint32_t drawCount{};
    };

    struct BasicTransferFillBufferParams {
        std::uint32_t value{};
    };

    struct BasicComputeDispatchParams {
        std::uint32_t groupCountX{};
        std::uint32_t groupCountY{};
        std::uint32_t groupCountZ{};
    };

    inline void registerBasicTransferClearSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicTransferClearPassType,
            .paramsType = kBasicTransferClearParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::TransferWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {RenderGraphCommandKind::ClearColor},
        });
    }

    inline void registerBasicDynamicClearSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicDynamicClearPassType,
            .paramsType = kBasicDynamicClearParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {RenderGraphCommandKind::ClearColor},
        });
    }

    inline void registerBasicTransientPresentSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicTransientPresentPassType,
            .paramsType = kBasicTransientPresentParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::TransferWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {RenderGraphCommandKind::ClearColor},
        });
    }

    inline void registerBasicRasterTriangleSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicRasterTrianglePassType,
            .paramsType = kBasicRasterTriangleParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
    }

    inline void registerBasicRasterDepthTriangleSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicRasterDepthTrianglePassType,
            .paramsType = kBasicRasterDepthTriangleParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    RenderGraphResourceSlotSchema{
                        .name = "depth",
                        .access = RenderGraphSlotAccess::DepthAttachmentWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
    }

    inline void registerBasicRasterMesh3DSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicRasterMesh3DPassType,
            .paramsType = kBasicRasterMesh3DParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    RenderGraphResourceSlotSchema{
                        .name = "depth",
                        .access = RenderGraphSlotAccess::DepthAttachmentWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
    }

    inline void registerBasicRasterMrtSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicRasterMrtPassType,
            .paramsType = kBasicRasterMrtParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "color0",
                        .access = RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    RenderGraphResourceSlotSchema{
                        .name = "color1",
                        .access = RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {RenderGraphCommandKind::ClearColor},
        });
    }

    inline void registerBasicRasterFullscreenSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicRasterFullscreenPassType,
            .paramsType = kBasicRasterFullscreenParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands =
                {
                    RenderGraphCommandKind::SetShader,
                    RenderGraphCommandKind::SetTexture,
                    RenderGraphCommandKind::SetVec4,
                    RenderGraphCommandKind::DrawFullscreenTriangle,
                },
        });
    }

    inline void registerBasicRenderViewWorldGridSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicRenderViewWorldGridPassType,
            .paramsType = kBasicRenderViewWorldGridParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands =
                {
                    RenderGraphCommandKind::SetShader,
                    RenderGraphCommandKind::SetVec4,
                    RenderGraphCommandKind::DrawFullscreenTriangle,
                },
        });
    }

    inline void registerBasicRenderViewSceneInputsSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicRenderViewSceneInputsPassType,
            .paramsType = kBasicRenderViewSceneInputsParamsType,
            .resourceSlots = {},
            .allowedCommands = {RenderGraphCommandKind::SetInt},
            .allowCulling = false,
            .hasSideEffects = true,
        });
    }

    inline void registerBasicRenderViewOverlaySchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicRenderViewOverlayPassType,
            .paramsType = kBasicRenderViewOverlayParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands =
                {
                    RenderGraphCommandKind::SetShader,
                    RenderGraphCommandKind::SetVec4,
                    RenderGraphCommandKind::SetInt,
                },
        });
    }

    inline void registerBasicRasterDrawListSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicRasterDrawListPassType,
            .paramsType = kBasicRasterDrawListParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    RenderGraphResourceSlotSchema{
                        .name = "depth",
                        .access = RenderGraphSlotAccess::DepthAttachmentWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
    }

    inline void registerBasicTransferFillBufferSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicTransferFillBufferPassType,
            .paramsType = kBasicTransferFillBufferParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::BufferTransferWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {RenderGraphCommandKind::FillBuffer},
        });
    }

    inline void registerBasicComputeDispatchSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicComputeDispatchPassType,
            .paramsType = kBasicComputeDispatchParamsType,
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::BufferStorageReadWrite,
                        .shaderStage = RenderGraphShaderStage::Compute,
                        .optional = false,
                    },
                },
            .allowedCommands =
                {
                    RenderGraphCommandKind::SetShader,
                    RenderGraphCommandKind::Dispatch,
                },
        });
    }

    inline void registerBasicComputeReadbackSchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicComputeReadbackPassType,
            .paramsType = {},
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = RenderGraphSlotAccess::BufferTransferRead,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::BufferTransferWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
    }

    inline void registerBasicDebugImageCopySchema(RenderGraphSchemaRegistry& schemas) {
        schemas.registerSchema(RenderGraphPassSchema{
            .type = kBasicDebugImageCopyPassType,
            .paramsType = {},
            .resourceSlots =
                {
                    RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = RenderGraphSlotAccess::TransferRead,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = RenderGraphSlotAccess::TransferWrite,
                        .shaderStage = RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {RenderGraphCommandKind::CopyImage},
        });
    }

    [[nodiscard]] inline RenderGraphSchemaRegistry basicRenderGraphSchemaRegistry() {
        RenderGraphSchemaRegistry schemas;
        registerBasicTransferClearSchema(schemas);
        registerBasicDynamicClearSchema(schemas);
        registerBasicTransientPresentSchema(schemas);
        registerBasicRasterTriangleSchema(schemas);
        registerBasicRasterDepthTriangleSchema(schemas);
        registerBasicRasterMesh3DSchema(schemas);
        registerBasicRasterMrtSchema(schemas);
        registerBasicRasterFullscreenSchema(schemas);
        registerBasicRenderViewWorldGridSchema(schemas);
        registerBasicRenderViewSceneInputsSchema(schemas);
        registerBasicRenderViewOverlaySchema(schemas);
        registerBasicRasterDrawListSchema(schemas);
        registerBasicTransferFillBufferSchema(schemas);
        registerBasicComputeDispatchSchema(schemas);
        registerBasicComputeReadbackSchema(schemas);
        registerBasicDebugImageCopySchema(schemas);
        return schemas;
    }

} // namespace asharia
