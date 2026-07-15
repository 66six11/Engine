#pragma once

#include "asharia/host_runtime/static_factory_registration.hpp"

namespace asharia::host_runtime {

    class StaticFactoryRegistrationState;

    class StaticFactoryCallbackTableBuilder final {
    public:
        [[nodiscard]] static StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
        build(const StaticFactoryRegistrationState& state) noexcept;
    };

} // namespace asharia::host_runtime
