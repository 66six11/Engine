#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "asharia/archive/archive_value.hpp"
#include "asharia/core/result.hpp"
#include "asharia/schema/ids.hpp"

namespace asharia::persistence {

    enum class MigrationScenario {
        Unspecified,
        SceneLoad,
        AssetLoad,
        EditorRepair,
    };

    struct MigrationContext {
        schema::TypeId type{};
        std::string_view typeName;
        std::string_view objectPath;
        std::string_view archivePath;
        MigrationScenario scenario{MigrationScenario::Unspecified};
        std::uint32_t fromVersion{};
        std::uint32_t toVersion{};
        const archive::ArchiveValue* input{};
        archive::ArchiveValue* output{};
    };

    using MigrationFn = VoidResult (*)(MigrationContext&);

    struct MigrationRule {
        schema::TypeId type{};
        std::uint32_t fromVersion{};
        std::uint32_t toVersion{};
        MigrationFn migrate{};
    };

    class MigrationRegistry {
    public:
        [[nodiscard]] VoidResult registerMigration(schema::TypeId type, std::uint32_t fromVersion,
                                                   std::uint32_t toVersion, MigrationFn migrate);

        [[nodiscard]] Result<archive::ArchiveValue>
        migrateObject(const schema::TypeId& type, std::string_view typeName,
                      std::string_view objectPath, std::string_view archivePath,
                      MigrationScenario scenario, std::uint32_t fromVersion,
                      std::uint32_t toVersion, const archive::ArchiveValue& input) const;

        [[nodiscard]] const MigrationRule* findMigration(const schema::TypeId& type,
                                                         std::uint32_t fromVersion) const;

    private:
        std::vector<MigrationRule> rules_;
    };

} // namespace asharia::persistence
