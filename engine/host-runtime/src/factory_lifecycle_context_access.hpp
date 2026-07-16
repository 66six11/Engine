#pragma once

#include <span>

#include "asharia/host_runtime/factory_lifecycle_contexts.hpp"

namespace asharia::host_runtime {

    class FactoryLifecycleContextAccessV1 final {
    public:
        [[nodiscard]] static FactoryCreateContextV1
        create(FactoryAttributionViewV1 attribution,
               std::span<const FactoryDependencyViewV1> dependencies) noexcept {
            return FactoryCreateContextV1{attribution, dependencies};
        }

        [[nodiscard]] static FactoryActivateContextV1
        activate(FactoryAttributionViewV1 attribution) noexcept {
            return FactoryActivateContextV1{attribution};
        }

        [[nodiscard]] static FactoryQuiesceContextV1
        quiesce(FactoryAttributionViewV1 attribution) noexcept {
            return FactoryQuiesceContextV1{attribution};
        }

        [[nodiscard]] static FactoryDeactivateContextV1
        deactivate(FactoryAttributionViewV1 attribution) noexcept {
            return FactoryDeactivateContextV1{attribution};
        }
    };

} // namespace asharia::host_runtime
