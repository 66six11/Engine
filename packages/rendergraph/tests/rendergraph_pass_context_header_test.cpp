#include "asharia/rendergraph/render_graph_pass_context.hpp"

namespace asharia::rendergraph_header_tests {

    void touchPassContextHeader() {
        RenderGraphPassContext context;
        context.name = "HeaderPassContext";
        RenderGraphPassCallback callback = [](RenderGraphPassContext) -> Result<void> {
            return {};
        };
        (void)callback(context);
    }

} // namespace asharia::rendergraph_header_tests
