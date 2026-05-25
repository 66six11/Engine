#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/rendergraph/render_graph_types.hpp"

namespace asharia {

    struct RenderGraphCompiledPass {
        std::string name;
        std::string type;
        std::string paramsType;
        std::size_t declarationIndex{};
        bool allowCulling{};
        bool hasSideEffects{};
        std::vector<std::byte> paramsData;
        std::vector<RenderGraphCommand> commands;
        std::vector<RenderGraphImageTransition> transitionsBefore;
        std::vector<RenderGraphImageHandle> colorWrites;
        std::vector<RenderGraphImageHandle> shaderReads;
        std::vector<RenderGraphImageHandle> depthReads;
        std::vector<RenderGraphImageHandle> depthWrites;
        std::vector<RenderGraphImageHandle> depthSampledReads;
        std::vector<RenderGraphImageHandle> transferReads;
        std::vector<RenderGraphImageHandle> transferWrites;
        std::vector<RenderGraphBufferHandle> bufferReads;
        std::vector<RenderGraphBufferHandle> bufferTransferReads;
        std::vector<RenderGraphBufferHandle> bufferWrites;
        std::vector<RenderGraphBufferHandle> bufferStorageReadWrites;
        std::vector<RenderGraphImageSlot> colorWriteSlots;
        std::vector<RenderGraphImageSlot> shaderReadSlots;
        std::vector<RenderGraphImageSlot> depthReadSlots;
        std::vector<RenderGraphImageSlot> depthWriteSlots;
        std::vector<RenderGraphImageSlot> depthSampledReadSlots;
        std::vector<RenderGraphImageSlot> transferReadSlots;
        std::vector<RenderGraphImageSlot> transferWriteSlots;
        std::vector<RenderGraphBufferSlot> bufferReadSlots;
        std::vector<RenderGraphBufferSlot> bufferTransferReadSlots;
        std::vector<RenderGraphBufferSlot> bufferWriteSlots;
        std::vector<RenderGraphBufferSlot> bufferStorageReadWriteSlots;
        std::vector<RenderGraphBufferTransition> bufferTransitionsBefore;
    };

    enum class RenderGraphResourceKind {
        Image,
        Buffer,
    };

    struct RenderGraphPassDependency {
        std::size_t fromDeclarationIndex{};
        std::size_t toDeclarationIndex{};
        RenderGraphResourceKind resourceKind{RenderGraphResourceKind::Image};
        RenderGraphImageHandle image{};
        RenderGraphBufferHandle buffer{};
        std::string imageName;
        std::string bufferName;
        std::string reason;
    };

    struct RenderGraphCulledPass {
        std::size_t declarationIndex{};
        std::string name;
        std::string type;
        std::string reason;
    };

    struct RenderGraphTransientImageAllocation {
        RenderGraphImageHandle image{};
        std::string imageName;
        RenderGraphImageFormat format{RenderGraphImageFormat::Undefined};
        RenderGraphExtent2D extent{};
        std::size_t firstPassIndex{};
        std::size_t lastPassIndex{};
        RenderGraphImageState finalState{RenderGraphImageState::Undefined};
        RenderGraphShaderStage finalShaderStage{RenderGraphShaderStage::None};
    };

    struct RenderGraphTransientBufferAllocation {
        RenderGraphBufferHandle buffer{};
        std::string bufferName;
        std::uint64_t byteSize{};
        std::size_t firstPassIndex{};
        std::size_t lastPassIndex{};
        RenderGraphBufferState finalState{RenderGraphBufferState::Undefined};
        RenderGraphShaderStage finalShaderStage{RenderGraphShaderStage::None};
    };

    struct RenderGraphCompileResult {
        std::size_t declaredPassCount{};
        std::size_t declaredImageCount{};
        std::size_t declaredBufferCount{};
        std::vector<RenderGraphCompiledPass> passes;
        std::vector<RenderGraphPassDependency> dependencies;
        std::vector<RenderGraphCulledPass> culledPasses;
        std::vector<RenderGraphTransientImageAllocation> transientImages;
        std::vector<RenderGraphTransientBufferAllocation> transientBuffers;
        std::vector<RenderGraphImageTransition> finalTransitions;
        std::vector<RenderGraphBufferTransition> finalBufferTransitions;
    };

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
