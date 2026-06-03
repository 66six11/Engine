#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "asharia/rendergraph/render_graph_compile.hpp"
#include "asharia/rendergraph/render_graph_types.hpp"

namespace asharia {

    enum class RenderGraphDiagnosticsTransitionPhase {
        BeforePass,
        Final,
    };

    struct RenderGraphDiagnosticsPassNode {
        std::size_t passIndex{};
        std::size_t declarationIndex{};
        std::string name;
        std::string type;
        std::string paramsType;
        bool allowCulling{};
        bool hasSideEffects{};
        std::size_t commandCount{};
        std::size_t imageTransitionCount{};
        std::size_t bufferTransitionCount{};
    };

    struct RenderGraphDiagnosticsCommandNode {
        std::size_t passIndex{};
        std::size_t declarationIndex{};
        std::size_t commandIndex{};
        std::string passName;
        RenderGraphCommandKind kind{RenderGraphCommandKind::SetShader};
        std::string detail;
    };

    struct RenderGraphDiagnosticsResourceNode {
        RenderGraphResourceKind kind{RenderGraphResourceKind::Image};
        std::uint32_t resourceIndex{};
        std::string name;
        RenderGraphImageLifetime imageLifetime{RenderGraphImageLifetime::Imported};
        RenderGraphImageFormat imageFormat{RenderGraphImageFormat::Undefined};
        RenderGraphExtent2D imageExtent{};
        RenderGraphImageAccess imageInitialAccess{};
        RenderGraphImageAccess imageFinalAccess{};
        RenderGraphBufferLifetime bufferLifetime{RenderGraphBufferLifetime::Imported};
        std::uint64_t bufferByteSize{};
        RenderGraphBufferAccess bufferInitialAccess{};
        RenderGraphBufferAccess bufferFinalAccess{};
    };

    struct RenderGraphDiagnosticsAccessEdge {
        std::size_t passIndex{};
        std::size_t declarationIndex{};
        std::string passName;
        RenderGraphResourceKind resourceKind{RenderGraphResourceKind::Image};
        std::uint32_t resourceIndex{};
        std::string resourceName;
        std::string slotName;
        RenderGraphSlotAccess access{RenderGraphSlotAccess::ColorWrite};
        RenderGraphShaderStage shaderStage{RenderGraphShaderStage::None};
    };

    struct RenderGraphDiagnosticsDependencyEdge {
        std::size_t fromPassIndex{};
        std::size_t toPassIndex{};
        std::size_t fromDeclarationIndex{};
        std::size_t toDeclarationIndex{};
        RenderGraphResourceKind resourceKind{RenderGraphResourceKind::Image};
        std::uint32_t resourceIndex{};
        std::string resourceName;
        std::string reason;
    };

    struct RenderGraphDiagnosticsTransition {
        RenderGraphDiagnosticsTransitionPhase phase{
            RenderGraphDiagnosticsTransitionPhase::BeforePass};
        std::size_t passIndex{};
        std::size_t declarationIndex{};
        std::string passName;
        RenderGraphResourceKind resourceKind{RenderGraphResourceKind::Image};
        std::uint32_t resourceIndex{};
        std::string resourceName;
        RenderGraphImageAccess oldImageAccess{};
        RenderGraphImageAccess newImageAccess{};
        RenderGraphBufferAccess oldBufferAccess{};
        RenderGraphBufferAccess newBufferAccess{};
    };

    struct RenderGraphDiagnosticsSnapshot {
        std::size_t declaredPassCount{};
        std::size_t declaredImageCount{};
        std::size_t declaredBufferCount{};
        std::vector<RenderGraphDiagnosticsPassNode> passes;
        std::vector<RenderGraphDiagnosticsCommandNode> commands;
        std::vector<RenderGraphDiagnosticsResourceNode> resources;
        std::vector<RenderGraphDiagnosticsAccessEdge> accessEdges;
        std::vector<RenderGraphDiagnosticsDependencyEdge> dependencyEdges;
        std::vector<RenderGraphDiagnosticsTransition> transitions;
        std::vector<RenderGraphCulledPass> culledPasses;
        std::vector<RenderGraphTransientImageAllocation> transientImages;
        std::vector<RenderGraphTransientBufferAllocation> transientBuffers;
    };

} // namespace asharia
