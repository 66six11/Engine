#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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

} // namespace asharia
