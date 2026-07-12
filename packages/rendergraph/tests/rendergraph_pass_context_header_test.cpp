#include "asharia/rendergraph/render_graph_pass_context.hpp"

namespace asharia::rendergraph_header_tests {

    void touchPassContextHeader() {
        RenderGraphPassContext context;
        context.name = "HeaderPassContext";
        RenderGraphPassCallback callback = [](RenderGraphPassContext) -> Result<void> {
            return {};
        };
        [[maybe_unused]] const Result<void> callbackResult = callback(context);
    }

} // namespace asharia::rendergraph_header_tests
