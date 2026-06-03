#pragma once

#include "asharia/core/result.hpp"
#include "asharia/rendergraph/render_graph_compile.hpp"

#include "render_graph_declaration_view.hpp"

namespace asharia {
    class RenderGraphExecutorRegistry;
    class RenderGraphSchemaRegistry;
} // namespace asharia

namespace asharia::rendergraph_internal {

    [[nodiscard]] Result<RenderGraphCompileResult>
    compileRenderGraph(RenderGraphDeclarationView declarations,
                       const RenderGraphSchemaRegistry* schemaRegistry);

    [[nodiscard]] Result<void>
    executeRenderGraph(RenderGraphDeclarationView declarations,
                       const RenderGraphCompileResult& compiled,
                       const RenderGraphExecutorRegistry* executorRegistry);

} // namespace asharia::rendergraph_internal
