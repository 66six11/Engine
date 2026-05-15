#pragma once

#include <string_view>

#include "asharia/archive/archive_value.hpp"
#include "asharia/core/result.hpp"
#include "asharia/cpp_binding/binding_registry.hpp"
#include "asharia/persistence/migration.hpp"
#include "asharia/schema/schema_registry.hpp"

namespace asharia::persistence {

    enum class UnknownFieldPolicy {
        Error,
        Drop,
        Preserve,
    };

    enum class MissingFieldPolicy {
        Error,
        KeepConstructedValue,
        UseDefault,
    };

    struct PersistencePolicy {
        bool includeTypeHeader{true};
        UnknownFieldPolicy unknownFields{UnknownFieldPolicy::Error};
        MissingFieldPolicy missingFields{MissingFieldPolicy::UseDefault};
        const MigrationRegistry* migrations{};
        std::string_view archivePath;
        MigrationScenario migrationScenario{MigrationScenario::Unspecified};
    };

    [[nodiscard]] Error persistenceError(std::string message);

    [[nodiscard]] Result<archive::ArchiveValue>
    saveObject(const schema::SchemaRegistry& schemas, const cpp_binding::BindingRegistry& bindings,
               const schema::TypeId& type, const void* object,
               const PersistencePolicy& policy = {});

    [[nodiscard]] VoidResult loadObject(const schema::SchemaRegistry& schemas,
                                        const cpp_binding::BindingRegistry& bindings,
                                        const schema::TypeId& type,
                                        const archive::ArchiveValue& value, void* object,
                                        const PersistencePolicy& policy = {});

} // namespace asharia::persistence
