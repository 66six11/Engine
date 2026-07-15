#pragma once

#include <string_view>

#include "asharia/host_runtime/static_factory_callbacks.hpp"

namespace asharia::host_runtime {

    class StaticFactoryRegistrationState;

    class StaticFactoryRegistrar final {
    public:
        void registerFactory(std::string_view localFactoryId,
                             StaticFactoryCallbacksV1 callbacks) noexcept;

    private:
        explicit StaticFactoryRegistrar(StaticFactoryRegistrationState& state) noexcept
            : state_(&state) {}

        ~StaticFactoryRegistrar() = default;

        StaticFactoryRegistrar(const StaticFactoryRegistrar&) = delete;
        StaticFactoryRegistrar& operator=(const StaticFactoryRegistrar&) = delete;
        StaticFactoryRegistrar(StaticFactoryRegistrar&&) = delete;
        StaticFactoryRegistrar& operator=(StaticFactoryRegistrar&&) = delete;

        StaticFactoryRegistrationState* state_{};

        friend class StaticFactoryRegistrationState;
    };

    using StaticFactoryProviderV2 = void (*)(StaticFactoryRegistrar& registrar) noexcept;

} // namespace asharia::host_runtime
