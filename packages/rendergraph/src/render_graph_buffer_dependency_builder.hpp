#pragma once

#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rendergraph/render_graph_compile.hpp"

#include "render_graph_dependency_builder.hpp"

namespace asharia::rendergraph_internal {

    [[nodiscard]] Result<void>
    buildBufferDependencies(const DependencyBuildInputs& inputs,
                            std::vector<RenderGraphPassDependency>& dependencies);

} // namespace asharia::rendergraph_internal
