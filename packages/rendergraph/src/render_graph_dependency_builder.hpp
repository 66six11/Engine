#pragma once

#include <span>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rendergraph/render_graph_compile.hpp"

#include "render_graph_pass.hpp"

namespace asharia::rendergraph_internal {

    struct DependencyBuildInputs {
        std::span<const RenderGraphImageDesc> images;
        std::span<const RenderGraphBufferDesc> buffers;
        std::span<const Pass> passes;
    };

    [[nodiscard]] Result<std::vector<RenderGraphPassDependency>>
    buildDependencies(DependencyBuildInputs inputs);

} // namespace asharia::rendergraph_internal
