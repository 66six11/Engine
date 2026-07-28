#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "asharia/host_runtime/static_contribution_contract.hpp"
#include "asharia/host_runtime/static_factory_callbacks.hpp"
#include "asharia/host_runtime/static_factory_registration_snapshot.hpp"

namespace asharia::host_runtime {

    struct StaticContributionRuntimeBindingV1 final {
        std::size_t registrationIndex{};
        std::size_t contributionIndex{};
        const void* typeKey{};
        detail::ErasedStaticContributionPayloadAccessorV1 payloadAccessor{};
    };

    struct StaticFactoryCallbackTableStorageV1 final {
        StaticFactoryCallbackTableStorageV1(StaticFactoryRegistrationSnapshotV2 snapshotValue,
                                            std::vector<StaticFactoryCallbacksV1> callbacksValue,
                                            std::vector<StaticContributionRuntimeBindingV1>
                                                contributionRuntimeBindingsValue) noexcept
            : snapshot(std::move(snapshotValue)), callbacks(std::move(callbacksValue)),
              contributionRuntimeBindings(std::move(contributionRuntimeBindingsValue)) {}

        StaticFactoryRegistrationSnapshotV2 snapshot;
        std::vector<StaticFactoryCallbacksV1> callbacks;
        std::vector<StaticContributionRuntimeBindingV1> contributionRuntimeBindings;
    };

} // namespace asharia::host_runtime
