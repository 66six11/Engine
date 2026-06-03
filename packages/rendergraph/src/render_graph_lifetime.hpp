#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rendergraph/render_graph_compile.hpp"

#include "render_graph_pass.hpp"

namespace asharia::rendergraph_internal {

    [[nodiscard]] Result<RenderGraphTransientImageAllocation>
    makeTransientAllocation(std::span<const RenderGraphImageDesc> images, std::size_t imageIndex,
                            std::span<const RenderGraphCompiledPass> passes,
                            RenderGraphImageAccess finalAccess);

    [[nodiscard]] Result<RenderGraphTransientBufferAllocation> makeTransientBufferAllocation(
        std::span<const RenderGraphBufferDesc> buffers, std::size_t bufferIndex,
        std::span<const RenderGraphCompiledPass> passes, RenderGraphBufferAccess finalAccess);

    [[nodiscard]] bool bufferUsedByDeclaredPasses(std::span<const Pass> passes,
                                                  RenderGraphBufferHandle buffer);

    [[nodiscard]] bool imageUsedByDeclaredPasses(std::span<const Pass> passes,
                                                 RenderGraphImageHandle image);

    [[nodiscard]] Result<void>
    transitionImages(std::span<const RenderGraphImageDesc> images,
                     std::span<const RenderGraphImageSlot> imageSlots,
                     RenderGraphImageAccess requiredAccess,
                     std::vector<RenderGraphImageAccess>& currentAccesses,
                     RenderGraphCompiledPass& compiledPass);

    [[nodiscard]] Result<void>
    transitionBuffers(std::span<const RenderGraphBufferDesc> buffers,
                      std::span<const RenderGraphBufferSlot> bufferSlots,
                      RenderGraphBufferAccess requiredAccess,
                      std::vector<RenderGraphBufferAccess>& currentAccesses,
                      RenderGraphCompiledPass& compiledPass);

} // namespace asharia::rendergraph_internal
