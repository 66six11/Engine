#include "render_graph_debug_tables.hpp"

#include <string>

#include "render_graph_debug_detail_tables.hpp"
#include "render_graph_debug_summary_tables.hpp"

namespace asharia::rendergraph_internal {

    [[nodiscard]] std::string formatDebugTables(const RenderGraphDeclarationView& declarations,
                                                const RenderGraphCompileResult& compiled) {
        std::string output;
        appendResourceTables(declarations, output);
        appendPassTable(declarations, compiled, output);
        appendDependencyTable(declarations, compiled, output);
        appendCulledPassTable(compiled, output);
        appendSlotTable(declarations, compiled, output);
        appendCommandTable(compiled, output);
        appendTransitionTable(declarations, compiled, output);
        appendTransientTables(declarations, compiled, output);
        return output;
    }

} // namespace asharia::rendergraph_internal
