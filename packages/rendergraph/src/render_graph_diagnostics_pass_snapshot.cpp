#include "render_graph_diagnostics_pass_snapshot.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <utility>

#include "asharia/rendergraph/render_graph_diagnostics.hpp"

#include "render_graph_debug_names.hpp"
#include "render_graph_declaration_view.hpp"

namespace asharia::rendergraph_internal {

    namespace {

        [[nodiscard]] inline std::string imageName(const RenderGraphDeclarationView& declarations,
                                                   RenderGraphImageHandle image) {
            if (image.index < declarations.images.size()) {
                return declarations.images[image.index].name;
            }
            return {};
        }

        [[nodiscard]] inline std::string bufferName(const RenderGraphDeclarationView& declarations,
                                                    RenderGraphBufferHandle buffer) {
            if (buffer.index < declarations.buffers.size()) {
                return declarations.buffers[buffer.index].name;
            }
            return {};
        }

        inline void appendImageAccessEdges(const RenderGraphDeclarationView& declarations,
                                           RenderGraphDiagnosticsSnapshot& snapshot,
                                           std::size_t passIndex,
                                           const RenderGraphCompiledPass& pass,
                                           RenderGraphSlotAccess access,
                                           std::span<const RenderGraphImageSlot> slots) {
            for (const RenderGraphImageSlot& slot : slots) {
                snapshot.accessEdges.push_back(RenderGraphDiagnosticsAccessEdge{
                    .passIndex = passIndex,
                    .declarationIndex = pass.declarationIndex,
                    .passName = pass.name,
                    .resourceKind = RenderGraphResourceKind::Image,
                    .resourceIndex = slot.image.index,
                    .resourceName = imageName(declarations, slot.image),
                    .slotName = slot.name,
                    .access = access,
                    .shaderStage = slot.shaderStage,
                });
            }
        }

        inline void appendBufferAccessEdges(const RenderGraphDeclarationView& declarations,
                                            RenderGraphDiagnosticsSnapshot& snapshot,
                                            std::size_t passIndex,
                                            const RenderGraphCompiledPass& pass,
                                            RenderGraphSlotAccess access,
                                            std::span<const RenderGraphBufferSlot> slots) {
            for (const RenderGraphBufferSlot& slot : slots) {
                snapshot.accessEdges.push_back(RenderGraphDiagnosticsAccessEdge{
                    .passIndex = passIndex,
                    .declarationIndex = pass.declarationIndex,
                    .passName = pass.name,
                    .resourceKind = RenderGraphResourceKind::Buffer,
                    .resourceIndex = slot.buffer.index,
                    .resourceName = bufferName(declarations, slot.buffer),
                    .slotName = slot.name,
                    .access = access,
                    .shaderStage = slot.shaderStage,
                });
            }
        }

        inline void appendCommandNodes(RenderGraphDiagnosticsSnapshot& snapshot,
                                       std::size_t passIndex, const RenderGraphCompiledPass& pass) {
            for (std::size_t commandIndex = 0; commandIndex < pass.commands.size();
                 ++commandIndex) {
                const RenderGraphCommand& command = pass.commands[commandIndex];
                snapshot.commands.push_back(RenderGraphDiagnosticsCommandNode{
                    .passIndex = passIndex,
                    .declarationIndex = pass.declarationIndex,
                    .commandIndex = commandIndex,
                    .passName = pass.name,
                    .kind = command.kind,
                    .detail = commandDetail(command),
                });
            }
        }

        inline void appendImageTransitionNode(RenderGraphDiagnosticsSnapshot& snapshot,
                                              RenderGraphDiagnosticsTransitionPhase phase,
                                              std::size_t passIndex, std::size_t declarationIndex,
                                              std::string passName,
                                              const RenderGraphImageTransition& transition) {
            snapshot.transitions.push_back(RenderGraphDiagnosticsTransition{
                .phase = phase,
                .passIndex = passIndex,
                .declarationIndex = declarationIndex,
                .passName = std::move(passName),
                .resourceKind = RenderGraphResourceKind::Image,
                .resourceIndex = transition.image.index,
                .resourceName = transition.imageName,
                .oldImageAccess =
                    RenderGraphImageAccess{
                        .state = transition.oldState,
                        .shaderStage = transition.oldShaderStage,
                    },
                .newImageAccess =
                    RenderGraphImageAccess{
                        .state = transition.newState,
                        .shaderStage = transition.newShaderStage,
                    },
            });
        }

        inline void appendBufferTransitionNode(RenderGraphDiagnosticsSnapshot& snapshot,
                                               RenderGraphDiagnosticsTransitionPhase phase,
                                               std::size_t passIndex, std::size_t declarationIndex,
                                               std::string passName,
                                               const RenderGraphBufferTransition& transition) {
            snapshot.transitions.push_back(RenderGraphDiagnosticsTransition{
                .phase = phase,
                .passIndex = passIndex,
                .declarationIndex = declarationIndex,
                .passName = std::move(passName),
                .resourceKind = RenderGraphResourceKind::Buffer,
                .resourceIndex = transition.buffer.index,
                .resourceName = transition.bufferName,
                .oldBufferAccess =
                    RenderGraphBufferAccess{
                        .state = transition.oldState,
                        .shaderStage = transition.oldShaderStage,
                    },
                .newBufferAccess =
                    RenderGraphBufferAccess{
                        .state = transition.newState,
                        .shaderStage = transition.newShaderStage,
                    },
            });
        }

        inline void appendBeforeTransitionNodes(RenderGraphDiagnosticsSnapshot& snapshot,
                                                std::size_t passIndex,
                                                const RenderGraphCompiledPass& pass) {
            for (const RenderGraphImageTransition& transition : pass.transitionsBefore) {
                appendImageTransitionNode(snapshot,
                                          RenderGraphDiagnosticsTransitionPhase::BeforePass,
                                          passIndex, pass.declarationIndex, pass.name, transition);
            }
            for (const RenderGraphBufferTransition& transition : pass.bufferTransitionsBefore) {
                appendBufferTransitionNode(snapshot,
                                           RenderGraphDiagnosticsTransitionPhase::BeforePass,
                                           passIndex, pass.declarationIndex, pass.name, transition);
            }
        }

    } // namespace

    void appendDiagnosticsPassNodes(const RenderGraphDeclarationView& declarations,
                                    const RenderGraphCompileResult& compiled,
                                    RenderGraphDiagnosticsSnapshot& snapshot) {
        for (std::size_t passIndex = 0; passIndex < compiled.passes.size(); ++passIndex) {
            const RenderGraphCompiledPass& pass = compiled.passes[passIndex];
            snapshot.passes.push_back(RenderGraphDiagnosticsPassNode{
                .passIndex = passIndex,
                .declarationIndex = pass.declarationIndex,
                .name = pass.name,
                .type = pass.type,
                .paramsType = pass.paramsType,
                .allowCulling = pass.allowCulling,
                .hasSideEffects = pass.hasSideEffects,
                .commandCount = pass.commands.size(),
                .imageTransitionCount = pass.transitionsBefore.size(),
                .bufferTransitionCount = pass.bufferTransitionsBefore.size(),
            });

            appendCommandNodes(snapshot, passIndex, pass);

            appendImageAccessEdges(declarations, snapshot, passIndex, pass,
                                   RenderGraphSlotAccess::ColorWrite, pass.colorWriteSlots);
            appendImageAccessEdges(declarations, snapshot, passIndex, pass,
                                   RenderGraphSlotAccess::ShaderRead, pass.shaderReadSlots);
            appendImageAccessEdges(declarations, snapshot, passIndex, pass,
                                   RenderGraphSlotAccess::DepthAttachmentRead, pass.depthReadSlots);
            appendImageAccessEdges(declarations, snapshot, passIndex, pass,
                                   RenderGraphSlotAccess::DepthAttachmentWrite,
                                   pass.depthWriteSlots);
            appendImageAccessEdges(declarations, snapshot, passIndex, pass,
                                   RenderGraphSlotAccess::DepthSampledRead,
                                   pass.depthSampledReadSlots);
            appendImageAccessEdges(declarations, snapshot, passIndex, pass,
                                   RenderGraphSlotAccess::TransferRead, pass.transferReadSlots);
            appendImageAccessEdges(declarations, snapshot, passIndex, pass,
                                   RenderGraphSlotAccess::TransferWrite, pass.transferWriteSlots);
            appendBufferAccessEdges(declarations, snapshot, passIndex, pass,
                                    RenderGraphSlotAccess::BufferShaderRead, pass.bufferReadSlots);
            appendBufferAccessEdges(declarations, snapshot, passIndex, pass,
                                    RenderGraphSlotAccess::BufferTransferRead,
                                    pass.bufferTransferReadSlots);
            appendBufferAccessEdges(declarations, snapshot, passIndex, pass,
                                    RenderGraphSlotAccess::BufferTransferWrite,
                                    pass.bufferWriteSlots);
            appendBufferAccessEdges(declarations, snapshot, passIndex, pass,
                                    RenderGraphSlotAccess::BufferStorageReadWrite,
                                    pass.bufferStorageReadWriteSlots);

            appendBeforeTransitionNodes(snapshot, passIndex, pass);
        }
    }

} // namespace asharia::rendergraph_internal
