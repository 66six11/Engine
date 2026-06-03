#pragma once

#include <string_view>

#include "asharia/rendergraph/render_graph_types.hpp"

namespace asharia {
    class RenderGraphSchemaRegistry;
}

namespace asharia::rendergraph_internal {

    [[nodiscard]] const RenderGraphPassSchema*
    findPassSchema(std::string_view type, const RenderGraphSchemaRegistry* schemaRegistry);

} // namespace asharia::rendergraph_internal
