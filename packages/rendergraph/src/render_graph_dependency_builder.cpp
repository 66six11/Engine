#include "render_graph_dependency_builder.hpp"

#include <utility>
#include <vector>

#include "render_graph_buffer_dependency_builder.hpp"
#include "render_graph_image_dependency_builder.hpp"

namespace asharia::rendergraph_internal {

    Result<std::vector<RenderGraphPassDependency>> buildDependencies(DependencyBuildInputs inputs) {
        std::vector<RenderGraphPassDependency> dependencies;

        auto imageDependencies = buildImageDependencies(inputs, dependencies);
        if (!imageDependencies) {
            return std::unexpected{std::move(imageDependencies.error())};
        }

        auto bufferDependencies = buildBufferDependencies(inputs, dependencies);
        if (!bufferDependencies) {
            return std::unexpected{std::move(bufferDependencies.error())};
        }

        return dependencies;
    }

} // namespace asharia::rendergraph_internal
