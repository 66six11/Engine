#include "asharia/rendergraph/render_graph.hpp"

namespace asharia::rendergraph_header_tests {

    void touchAggregateHeader() {
        RenderGraph graph;
        const auto image = graph.createTransientImage(RenderGraphImageDesc{
            .name = "AggregateHeaderImage",
            .format = RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = RenderGraphExtent2D{.width = 1, .height = 1},
        });
        graph.addPass("AggregateHeaderPass").writeColor("target", image);
    }

} // namespace asharia::rendergraph_header_tests
