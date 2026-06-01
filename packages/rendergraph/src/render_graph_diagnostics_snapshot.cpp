#include "render_graph_diagnostics_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "asharia/rendergraph/render_graph_diagnostics.hpp"

#include "render_graph_declaration_view.hpp"
#include "render_graph_diagnostics_pass_snapshot.hpp"

namespace asharia::rendergraph_internal {

    namespace {

        [[nodiscard]] inline std::size_t compiledPassIndex(const RenderGraphCompileResult& compiled,
                                                           std::size_t declarationIndex) {
            for (std::size_t passIndex = 0; passIndex < compiled.passes.size(); ++passIndex) {
                if (compiled.passes[passIndex].declarationIndex == declarationIndex) {
                    return passIndex;
                }
            }
            return compiled.passes.size();
        }

        inline void appendResourceNodes(const RenderGraphDeclarationView& declarations,
                                        RenderGraphDiagnosticsSnapshot& snapshot) {
            for (std::size_t imageIndex = 0; imageIndex < declarations.images.size();
                 ++imageIndex) {
                const RenderGraphImageDesc& image = declarations.images[imageIndex];
                snapshot.resources.push_back(RenderGraphDiagnosticsResourceNode{
                    .kind = RenderGraphResourceKind::Image,
                    .resourceIndex = static_cast<std::uint32_t>(imageIndex),
                    .name = image.name,
                    .imageLifetime = image.lifetime,
                    .imageFormat = image.format,
                    .imageExtent = image.extent,
                    .imageInitialAccess =
                        RenderGraphImageAccess{
                            .state = image.initialState,
                            .shaderStage = image.initialShaderStage,
                        },
                    .imageFinalAccess =
                        RenderGraphImageAccess{
                            .state = image.finalState,
                            .shaderStage = image.finalShaderStage,
                        },
                });
            }

            for (std::size_t bufferIndex = 0; bufferIndex < declarations.buffers.size();
                 ++bufferIndex) {
                const RenderGraphBufferDesc& buffer = declarations.buffers[bufferIndex];
                snapshot.resources.push_back(RenderGraphDiagnosticsResourceNode{
                    .kind = RenderGraphResourceKind::Buffer,
                    .resourceIndex = static_cast<std::uint32_t>(bufferIndex),
                    .name = buffer.name,
                    .bufferLifetime = buffer.lifetime,
                    .bufferByteSize = buffer.byteSize,
                    .bufferInitialAccess =
                        RenderGraphBufferAccess{
                            .state = buffer.initialState,
                            .shaderStage = buffer.initialShaderStage,
                        },
                    .bufferFinalAccess =
                        RenderGraphBufferAccess{
                            .state = buffer.finalState,
                            .shaderStage = buffer.finalShaderStage,
                        },
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

        inline void appendDependencyNodes(const RenderGraphCompileResult& compiled,
                                          RenderGraphDiagnosticsSnapshot& snapshot) {
            for (const RenderGraphPassDependency& dependency : compiled.dependencies) {
                snapshot.dependencyEdges.push_back(RenderGraphDiagnosticsDependencyEdge{
                    .fromPassIndex = compiledPassIndex(compiled, dependency.fromDeclarationIndex),
                    .toPassIndex = compiledPassIndex(compiled, dependency.toDeclarationIndex),
                    .fromDeclarationIndex = dependency.fromDeclarationIndex,
                    .toDeclarationIndex = dependency.toDeclarationIndex,
                    .resourceKind = dependency.resourceKind,
                    .resourceIndex = dependency.resourceKind == RenderGraphResourceKind::Buffer
                                         ? dependency.buffer.index
                                         : dependency.image.index,
                    .resourceName = dependency.resourceKind == RenderGraphResourceKind::Buffer
                                        ? dependency.bufferName
                                        : dependency.imageName,
                    .reason = dependency.reason,
                });
            }
        }

        inline void appendFinalTransitionNodes(const RenderGraphCompileResult& compiled,
                                               RenderGraphDiagnosticsSnapshot& snapshot) {
            for (const RenderGraphImageTransition& transition : compiled.finalTransitions) {
                appendImageTransitionNode(snapshot, RenderGraphDiagnosticsTransitionPhase::Final,
                                          compiled.passes.size(), compiled.declaredPassCount, {},
                                          transition);
            }
            for (const RenderGraphBufferTransition& transition : compiled.finalBufferTransitions) {
                appendBufferTransitionNode(snapshot, RenderGraphDiagnosticsTransitionPhase::Final,
                                           compiled.passes.size(), compiled.declaredPassCount, {},
                                           transition);
            }
        }

    } // namespace

    [[nodiscard]] RenderGraphDiagnosticsSnapshot
    makeDiagnosticsSnapshot(const RenderGraphDeclarationView& declarations,
                            const RenderGraphCompileResult& compiled) {
        RenderGraphDiagnosticsSnapshot snapshot;
        snapshot.declaredPassCount = compiled.declaredPassCount;
        snapshot.declaredImageCount = compiled.declaredImageCount;
        snapshot.declaredBufferCount = compiled.declaredBufferCount;
        snapshot.passes.reserve(compiled.passes.size());
        snapshot.resources.reserve(declarations.images.size() + declarations.buffers.size());
        snapshot.dependencyEdges.reserve(compiled.dependencies.size());
        snapshot.transitions.reserve(compiled.finalTransitions.size() +
                                     compiled.finalBufferTransitions.size());
        snapshot.culledPasses = compiled.culledPasses;
        snapshot.transientImages = compiled.transientImages;
        snapshot.transientBuffers = compiled.transientBuffers;

        appendResourceNodes(declarations, snapshot);
        appendDiagnosticsPassNodes(declarations, compiled, snapshot);
        appendDependencyNodes(compiled, snapshot);
        appendFinalTransitionNodes(compiled, snapshot);

        return snapshot;
    }

} // namespace asharia::rendergraph_internal
