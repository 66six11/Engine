#include "asharia/rendergraph/render_graph_execution.hpp"

namespace asharia::rendergraph_header_tests {

    void touchExecutionHeader() {
        RenderGraphSchemaRegistry schemas;
        schemas.registerSchema(RenderGraphPassSchema{
            .type = "HeaderExecutionPass",
            .paramsType = {},
            .resourceSlots = {},
            .allowedCommands = {},
            .allowCulling = false,
            .hasSideEffects = false,
        });
        (void)schemas.find("HeaderExecutionPass");

        RenderGraphExecutorRegistry executors;
        executors.registerExecutor("HeaderExecutionPass",
                                   [](RenderGraphPassContext) -> Result<void> { return {}; });
        (void)executors.find("HeaderExecutionPass");
    }

} // namespace asharia::rendergraph_header_tests
