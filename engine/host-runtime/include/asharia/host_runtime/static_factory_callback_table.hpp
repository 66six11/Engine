#pragma once

#include <vector>

#include "asharia/host_runtime/static_factory_callbacks.hpp"
#include "asharia/host_runtime/static_factory_registration_snapshot.hpp"

namespace asharia::host_runtime {

    class StaticFactoryCallbackTableBuilder;

    class StaticFactoryCallbackTableV1 final {
    public:
        StaticFactoryCallbackTableV1(StaticFactoryCallbackTableV1&&) noexcept = default;
        StaticFactoryCallbackTableV1& operator=(StaticFactoryCallbackTableV1&&) = delete;

        StaticFactoryCallbackTableV1(const StaticFactoryCallbackTableV1&) = delete;
        StaticFactoryCallbackTableV1& operator=(const StaticFactoryCallbackTableV1&) = delete;

        [[nodiscard]] const StaticFactoryRegistrationSnapshotV1&
        registrationSnapshot() const noexcept;

    private:
        StaticFactoryCallbackTableV1(StaticFactoryRegistrationSnapshotV1 snapshot,
                                     std::vector<StaticFactoryCallbacksV1> callbacks) noexcept;

        StaticFactoryRegistrationSnapshotV1 snapshot_;
        std::vector<StaticFactoryCallbacksV1> callbacks_;

        friend class StaticFactoryCallbackTableBuilder;
    };

} // namespace asharia::host_runtime
