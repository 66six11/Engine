#include "render_graph_schema_queries.hpp"

#include "asharia/rendergraph/render_graph_execution.hpp"

namespace asharia::rendergraph_internal {

    const RenderGraphPassSchema* findPassSchema(std::string_view type,
                                                const RenderGraphSchemaRegistry* schemaRegistry) {
        if (schemaRegistry == nullptr || type.empty()) {
            return nullptr;
        }

        return schemaRegistry->find(type);
    }

} // namespace asharia::rendergraph_internal
