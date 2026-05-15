#pragma once

#include <string_view>

#include "asharia/core/result.hpp"
#include "asharia/reflection/type_registry.hpp"
#include "asharia/serialization/archive_value.hpp"
#include "asharia/serialization/migration.hpp"
#include "asharia/serialization/storage_attributes.hpp"

namespace asharia::serialization {

    enum class UnknownFieldPolicy {
        Error,
        Ignore,
        Preserve,
    };

    enum class MissingFieldPolicy {
        Error,
        KeepConstructedValue,
        UseDefault,
    };

    struct SerializationPolicy {
        bool includeTypeHeader{true};
        UnknownFieldPolicy unknownFields{UnknownFieldPolicy::Error};
        MissingFieldPolicy missingFields{MissingFieldPolicy::UseDefault};
        const MigrationRegistry* migrations{};
        std::string_view archivePath;
        MigrationScenario migrationScenario{MigrationScenario::Unspecified};
    };

    [[nodiscard]] Result<ArchiveValue> serializeObject(const reflection::TypeRegistry& registry,
                                                       reflection::TypeId type, const void* object,
                                                       const SerializationPolicy& policy = {});

    [[nodiscard]] VoidResult deserializeObject(const reflection::TypeRegistry& registry,
                                               reflection::TypeId type, const ArchiveValue& value,
                                               void* object,
                                               const SerializationPolicy& policy = {});

} // namespace asharia::serialization
