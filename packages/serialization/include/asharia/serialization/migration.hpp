#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/reflection/ids.hpp"
#include "asharia/serialization/archive_value.hpp"

namespace asharia::serialization {

    enum class MigrationScenario {
        Unspecified,
        SceneLoad,
        AssetLoad,
        EditorRepair,
    };

    struct MigrationContext {
        reflection::TypeId type{};
        std::string_view typeName;
        std::string_view objectPath;
        std::string_view archivePath;
        MigrationScenario scenario{MigrationScenario::Unspecified};
        std::uint32_t fromVersion{};
        std::uint32_t toVersion{};
        const ArchiveValue* input{};
        ArchiveValue* output{};
    };

    using MigrationFn = VoidResult (*)(MigrationContext&);

    struct MigrationRule {
        reflection::TypeId type{};
        std::uint32_t fromVersion{};
        std::uint32_t toVersion{};
        MigrationFn migrate{};
    };

    class MigrationRegistry {
    public:
        [[nodiscard]] VoidResult registerMigration(reflection::TypeId type,
                                                   std::uint32_t fromVersion,
                                                   std::uint32_t toVersion, MigrationFn migrate);

        [[nodiscard]] Result<ArchiveValue>
        migrateObject(reflection::TypeId type, std::string_view typeName,
                      std::string_view objectPath, std::string_view archivePath,
                      MigrationScenario scenario, std::uint32_t fromVersion,
                      std::uint32_t toVersion, const ArchiveValue& input) const;

        [[nodiscard]] const MigrationRule* findMigration(reflection::TypeId type,
                                                         std::uint32_t fromVersion) const;

    private:
        std::vector<MigrationRule> rules_;
    };

} // namespace asharia::serialization
