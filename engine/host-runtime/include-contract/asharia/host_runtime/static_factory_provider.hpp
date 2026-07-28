#pragma once

#include <span>
#include <string_view>

#include "asharia/host_runtime/static_contribution_contract.hpp"
#include "asharia/host_runtime/static_factory_callbacks.hpp"

namespace asharia::host_runtime {

    class StaticFactoryRegistrationState;

    class StaticFactoryRegistrar final {
    public:
        void registerFactory(
            std::string_view localFactoryId, StaticFactoryCallbacksV1 callbacks,
            std::span<const StaticContributionBindingV2> availableContributions) noexcept;

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

    using StaticFactoryProviderV4 = void (*)(StaticFactoryRegistrar& registrar) noexcept;

} // namespace asharia::host_runtime
