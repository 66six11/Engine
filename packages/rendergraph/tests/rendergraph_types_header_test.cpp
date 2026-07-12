#include "asharia/rendergraph/render_graph_types.hpp"

namespace asharia::rendergraph_header_tests {

    void touchTypesHeader() {
        [[maybe_unused]] const RenderGraphImageDesc image{
            .name = "HeaderImage",
            .format = RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = RenderGraphExtent2D{.width = 1, .height = 1},
            .finalState = RenderGraphImageState::Present,
        };
        [[maybe_unused]] const RenderGraphBufferDesc buffer{
            .name = "HeaderBuffer",
            .byteSize = 4,
            .finalState = RenderGraphBufferState::HostRead,
        };
        [[maybe_unused]] const RenderGraphCommand command{
            .kind = RenderGraphCommandKind::FillBuffer,
            .name = "HeaderBuffer",
            .secondaryName = {},
            .floatValues = {},
            .intValue = 0,
            .uintValues = {42, 0, 0},
        };
    }

} // namespace asharia::rendergraph_header_tests
