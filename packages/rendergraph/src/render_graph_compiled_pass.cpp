#include "render_graph_compiled_pass.hpp"

#include <utility>

#include "render_graph_lifetime.hpp"
#include "render_graph_pass.hpp"
#include "render_graph_pass_queries.hpp"

namespace asharia::rendergraph_internal {

    RenderGraphCompiledPass makeCompiledPass(const Pass& pass, std::size_t declarationIndex,
                                             const RenderGraphSchemaRegistry* schemaRegistry) {
        const bool allowPassCulling = passAllowsCulling(pass, schemaRegistry);
        const bool passHasSideEffectsValue = passHasSideEffects(pass, schemaRegistry);

        return RenderGraphCompiledPass{
            .name = pass.name,
            .type = pass.type,
            .paramsType = pass.paramsType,
            .declarationIndex = declarationIndex,
            .allowCulling = allowPassCulling,
            .hasSideEffects = passHasSideEffectsValue,
            .paramsData = pass.paramsData,
            .commands = pass.commands,
            .transitionsBefore = {},
            .colorWrites = imageHandles(pass.colorWriteSlots),
            .shaderReads = imageHandles(pass.shaderReadSlots),
            .depthReads = imageHandles(pass.depthReadSlots),
            .depthWrites = imageHandles(pass.depthWriteSlots),
            .depthSampledReads = imageHandles(pass.depthSampledReadSlots),
            .transferReads = imageHandles(pass.transferReadSlots),
            .transferWrites = imageHandles(pass.transferWriteSlots),
            .bufferReads = bufferHandles(pass.bufferReadSlots),
            .bufferTransferReads = bufferHandles(pass.bufferTransferReadSlots),
            .bufferWrites = bufferHandles(pass.bufferWriteSlots),
            .bufferStorageReadWrites = bufferHandles(pass.bufferStorageReadWriteSlots),
            .colorWriteSlots = pass.colorWriteSlots,
            .shaderReadSlots = pass.shaderReadSlots,
            .depthReadSlots = pass.depthReadSlots,
            .depthWriteSlots = pass.depthWriteSlots,
            .depthSampledReadSlots = pass.depthSampledReadSlots,
            .transferReadSlots = pass.transferReadSlots,
            .transferWriteSlots = pass.transferWriteSlots,
            .bufferReadSlots = pass.bufferReadSlots,
            .bufferTransferReadSlots = pass.bufferTransferReadSlots,
            .bufferWriteSlots = pass.bufferWriteSlots,
            .bufferStorageReadWriteSlots = pass.bufferStorageReadWriteSlots,
            .bufferTransitionsBefore = {},
        };
    }

    // NOLINTBEGIN(readability-function-cognitive-complexity)
    Result<void>
    appendCompiledPassTransitions(std::span<const RenderGraphImageDesc> images,
                                  std::span<const RenderGraphBufferDesc> buffers,
                                  std::vector<RenderGraphImageAccess>& currentAccesses,
                                  std::vector<RenderGraphBufferAccess>& currentBufferAccesses,
                                  RenderGraphCompiledPass& compiledPass) {
        auto colorTransitions =
            transitionImages(images, compiledPass.colorWriteSlots,
                             RenderGraphImageAccess{
                                 .state = RenderGraphImageState::ColorAttachment,
                                 .shaderStage = RenderGraphShaderStage::None,
                             },
                             currentAccesses, compiledPass);
        if (!colorTransitions) {
            return std::unexpected{std::move(colorTransitions.error())};
        }

        auto shaderReadTransitions =
            transitionImages(images, compiledPass.shaderReadSlots,
                             RenderGraphImageAccess{
                                 .state = RenderGraphImageState::ShaderRead,
                                 .shaderStage = RenderGraphShaderStage::None,
                             },
                             currentAccesses, compiledPass);
        if (!shaderReadTransitions) {
            return std::unexpected{std::move(shaderReadTransitions.error())};
        }

        auto depthReadTransitions =
            transitionImages(images, compiledPass.depthReadSlots,
                             RenderGraphImageAccess{
                                 .state = RenderGraphImageState::DepthAttachmentRead,
                                 .shaderStage = RenderGraphShaderStage::None,
                             },
                             currentAccesses, compiledPass);
        if (!depthReadTransitions) {
            return std::unexpected{std::move(depthReadTransitions.error())};
        }

        auto depthWriteTransitions =
            transitionImages(images, compiledPass.depthWriteSlots,
                             RenderGraphImageAccess{
                                 .state = RenderGraphImageState::DepthAttachmentWrite,
                                 .shaderStage = RenderGraphShaderStage::None,
                             },
                             currentAccesses, compiledPass);
        if (!depthWriteTransitions) {
            return std::unexpected{std::move(depthWriteTransitions.error())};
        }

        auto depthSampledReadTransitions =
            transitionImages(images, compiledPass.depthSampledReadSlots,
                             RenderGraphImageAccess{
                                 .state = RenderGraphImageState::DepthSampledRead,
                                 .shaderStage = RenderGraphShaderStage::None,
                             },
                             currentAccesses, compiledPass);
        if (!depthSampledReadTransitions) {
            return std::unexpected{std::move(depthSampledReadTransitions.error())};
        }

        auto transferReadTransitions =
            transitionImages(images, compiledPass.transferReadSlots,
                             RenderGraphImageAccess{
                                 .state = RenderGraphImageState::TransferSrc,
                                 .shaderStage = RenderGraphShaderStage::None,
                             },
                             currentAccesses, compiledPass);
        if (!transferReadTransitions) {
            return std::unexpected{std::move(transferReadTransitions.error())};
        }

        auto transferTransitions = transitionImages(images, compiledPass.transferWriteSlots,
                                                    RenderGraphImageAccess{
                                                        .state = RenderGraphImageState::TransferDst,
                                                        .shaderStage = RenderGraphShaderStage::None,
                                                    },
                                                    currentAccesses, compiledPass);
        if (!transferTransitions) {
            return std::unexpected{std::move(transferTransitions.error())};
        }

        auto bufferWriteTransitions =
            transitionBuffers(buffers, compiledPass.bufferWriteSlots,
                              RenderGraphBufferAccess{
                                  .state = RenderGraphBufferState::TransferWrite,
                                  .shaderStage = RenderGraphShaderStage::None,
                              },
                              currentBufferAccesses, compiledPass);
        if (!bufferWriteTransitions) {
            return std::unexpected{std::move(bufferWriteTransitions.error())};
        }

        auto bufferTransferReadTransitions =
            transitionBuffers(buffers, compiledPass.bufferTransferReadSlots,
                              RenderGraphBufferAccess{
                                  .state = RenderGraphBufferState::TransferRead,
                                  .shaderStage = RenderGraphShaderStage::None,
                              },
                              currentBufferAccesses, compiledPass);
        if (!bufferTransferReadTransitions) {
            return std::unexpected{std::move(bufferTransferReadTransitions.error())};
        }

        auto bufferReadTransitions =
            transitionBuffers(buffers, compiledPass.bufferReadSlots,
                              RenderGraphBufferAccess{
                                  .state = RenderGraphBufferState::ShaderRead,
                                  .shaderStage = RenderGraphShaderStage::None,
                              },
                              currentBufferAccesses, compiledPass);
        if (!bufferReadTransitions) {
            return std::unexpected{std::move(bufferReadTransitions.error())};
        }

        auto bufferStorageReadWriteTransitions =
            transitionBuffers(buffers, compiledPass.bufferStorageReadWriteSlots,
                              RenderGraphBufferAccess{
                                  .state = RenderGraphBufferState::StorageReadWrite,
                                  .shaderStage = RenderGraphShaderStage::None,
                              },
                              currentBufferAccesses, compiledPass);
        if (!bufferStorageReadWriteTransitions) {
            return std::unexpected{std::move(bufferStorageReadWriteTransitions.error())};
        }

        return {};
    }
    // NOLINTEND(readability-function-cognitive-complexity)

} // namespace asharia::rendergraph_internal
