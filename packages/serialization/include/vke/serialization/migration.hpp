#pragma once

#include <cstdint>

#include "vke/core/result.hpp"
#include "vke/reflection/ids.hpp"
#include "vke/serialization/archive_value.hpp"

namespace vke::serialization {

    struct MigrationContext {
        reflection::TypeId type{};
        std::uint32_t fromVersion{};
        std::uint32_t toVersion{};
        const ArchiveValue* input{};
        ArchiveValue* output{};
    };

    using MigrationFn = VoidResult (*)(MigrationContext&);

} // namespace vke::serialization
