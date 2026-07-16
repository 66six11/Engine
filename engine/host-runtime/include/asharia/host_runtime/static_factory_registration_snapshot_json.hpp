#pragma once

#include <expected>
#include <string>

#include "asharia/host_runtime/static_factory_registration_snapshot.hpp"

namespace asharia::host_runtime {

    enum class StaticFactoryRegistrationSnapshotJsonRenderError {
        AllocationFailed,
        InvalidCardinality,
    };

    [[nodiscard]] std::expected<std::string, StaticFactoryRegistrationSnapshotJsonRenderError>
    renderStaticFactoryRegistrationSnapshotJson(
        const StaticFactoryRegistrationSnapshotV2& snapshot) noexcept;

} // namespace asharia::host_runtime
