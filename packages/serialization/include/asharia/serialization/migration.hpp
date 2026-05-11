#pragma once

#include <cstdint>

#include "asharia/core/result.hpp"
#include "asharia/reflection/ids.hpp"
#include "asharia/serialization/archive_value.hpp"

namespace asharia::serialization {

    struct MigrationContext {
        reflection::TypeId type{};
        std::uint32_t fromVersion{};
        std::uint32_t toVersion{};
        const ArchiveValue* input{};
        ArchiveValue* output{};
    };

    using MigrationFn = VoidResult (*)(MigrationContext&);

} // namespace asharia::serialization
