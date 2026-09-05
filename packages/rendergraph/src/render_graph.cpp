#include <memory>

#include "render_graph_internal.hpp"

namespace asharia {
    RenderGraph::RenderGraph() : impl_(std::make_unique<Impl>()) {}

    RenderGraph::~RenderGraph() = default;

    RenderGraph::RenderGraph(const RenderGraph& other)
        : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_) : std::make_unique<Impl>()) {
        impl_->owner_ = std::make_shared<const std::byte>();
    }

    RenderGraph& RenderGraph::operator=(const RenderGraph& other) {
        if (this != &other) {
            RenderGraph copy{other};
            impl_.swap(copy.impl_);
        }
        return *this;
    }

    RenderGraph::RenderGraph(RenderGraph&& other) noexcept = default;

    RenderGraph& RenderGraph::operator=(RenderGraph&& other) noexcept = default;

} // namespace asharia
