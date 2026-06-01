#pragma once

#include <span>
#include <vector>

#include "asharia/rendergraph/render_graph_compile.hpp"

namespace asharia {
    class RenderGraphSchemaRegistry;
}

namespace asharia::rendergraph_internal {

    struct Pass;

    [[nodiscard]] bool passAllowsCulling(const Pass& pass,
                                         const RenderGraphSchemaRegistry* schemaRegistry);

    [[nodiscard]] bool passHasSideEffects(const Pass& pass,
                                          const RenderGraphSchemaRegistry* schemaRegistry);

    [[nodiscard]] std::vector<RenderGraphImageHandle>
    imageHandles(std::span<const RenderGraphImageSlot> slots);

    [[nodiscard]] std::vector<RenderGraphBufferHandle>
    bufferHandles(std::span<const RenderGraphBufferSlot> slots);

    [[nodiscard]] bool passReadsImage(const Pass& pass, RenderGraphImageHandle image);

    [[nodiscard]] bool passWritesImage(const Pass& pass, RenderGraphImageHandle image);

    [[nodiscard]] bool passReadsBuffer(const Pass& pass, RenderGraphBufferHandle buffer);

    [[nodiscard]] bool passWritesBuffer(const Pass& pass, RenderGraphBufferHandle buffer);

    [[nodiscard]] bool passUsesImage(const RenderGraphCompiledPass& pass,
                                     RenderGraphImageHandle image);

    [[nodiscard]] bool imageUsedByCompiledPasses(std::span<const RenderGraphCompiledPass> passes,
                                                 RenderGraphImageHandle image);

    [[nodiscard]] bool passUsesBuffer(const RenderGraphCompiledPass& pass,
                                      RenderGraphBufferHandle buffer);

    [[nodiscard]] bool bufferUsedByCompiledPasses(std::span<const RenderGraphCompiledPass> passes,
                                                  RenderGraphBufferHandle buffer);

    [[nodiscard]] RenderGraphImageTransition makeTransition(RenderGraphImageHandle imageHandle,
                                                            const RenderGraphImageDesc& image,
                                                            RenderGraphImageAccess oldAccess,
                                                            RenderGraphImageAccess newAccess);

    [[nodiscard]] RenderGraphBufferTransition makeTransition(RenderGraphBufferHandle bufferHandle,
                                                             const RenderGraphBufferDesc& buffer,
                                                             RenderGraphBufferAccess oldAccess,
                                                             RenderGraphBufferAccess newAccess);

} // namespace asharia::rendergraph_internal
