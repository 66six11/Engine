#pragma once

#include <utility>
#include <vector>

#include "asharia/host_runtime/static_factory_callbacks.hpp"
#include "asharia/host_runtime/static_factory_registration_snapshot.hpp"

namespace asharia::host_runtime {

    struct StaticFactoryCallbackTableStorageV1 final {
        StaticFactoryCallbackTableStorageV1(
            StaticFactoryRegistrationSnapshotV1 snapshotValue,
            std::vector<StaticFactoryCallbacksV1> callbacksValue) noexcept
            : snapshot(std::move(snapshotValue)), callbacks(std::move(callbacksValue)) {}

        StaticFactoryRegistrationSnapshotV1 snapshot;
        std::vector<StaticFactoryCallbacksV1> callbacks;
    };

} // namespace asharia::host_runtime
