#include <string>
#include <utility>

#include "render_graph_internal.hpp"

namespace asharia {

    namespace {

        [[nodiscard]] rendergraph_internal::Pass makePass(std::string name, std::string type) {
            return rendergraph_internal::Pass{
                .name = std::move(name),
                .type = std::move(type),
                .paramsType = {},
                .paramsData = {},
                .colorWriteSlots = {},
                .shaderReadSlots = {},
                .depthReadSlots = {},
                .depthWriteSlots = {},
                .depthSampledReadSlots = {},
                .transferReadSlots = {},
                .transferWriteSlots = {},
                .bufferReadSlots = {},
                .bufferTransferReadSlots = {},
                .bufferWriteSlots = {},
                .bufferStorageReadWriteSlots = {},
                .commands = {},
                .allowCulling = {},
                .hasSideEffects = {},
                .callback = {},
            };
        }

    } // namespace

    RenderGraph::PassBuilder RenderGraph::addPass(std::string name) {
        impl_->passes_.push_back(makePass(std::move(name), {}));
        return PassBuilder{*this, impl_->passes_.size() - 1};
    }

    RenderGraph::PassBuilder RenderGraph::addPass(std::string name, std::string type) {
        impl_->passes_.push_back(makePass(std::move(name), std::move(type)));
        return PassBuilder{*this, impl_->passes_.size() - 1};
    }

} // namespace asharia
