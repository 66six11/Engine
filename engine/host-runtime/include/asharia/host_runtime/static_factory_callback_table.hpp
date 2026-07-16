#pragma once

#include <memory>

#include "asharia/host_runtime/static_factory_registration_snapshot.hpp"

namespace asharia::host_runtime {

    class StaticFactoryCallbackTableBuilder;
    class StaticFactoryCallbackTablePrivateAccessV1;
    struct StaticFactoryCallbackTableStorageV1;

    class StaticFactoryCallbackTableV1 final {
    public:
        StaticFactoryCallbackTableV1(StaticFactoryCallbackTableV1&&) noexcept = default;
        StaticFactoryCallbackTableV1& operator=(StaticFactoryCallbackTableV1&&) = delete;

        StaticFactoryCallbackTableV1(const StaticFactoryCallbackTableV1&) = delete;
        StaticFactoryCallbackTableV1& operator=(const StaticFactoryCallbackTableV1&) = delete;

        [[nodiscard]] const StaticFactoryRegistrationSnapshotV1&
        registrationSnapshot() const noexcept;

    private:
        explicit StaticFactoryCallbackTableV1(
            std::shared_ptr<const StaticFactoryCallbackTableStorageV1> storage) noexcept;

        std::shared_ptr<const StaticFactoryCallbackTableStorageV1> storage_;

        friend class StaticFactoryCallbackTableBuilder;
        friend class StaticFactoryCallbackTablePrivateAccessV1;
    };

} // namespace asharia::host_runtime
