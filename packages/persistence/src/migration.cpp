#include "asharia/persistence/migration.hpp"

#include <algorithm>
#include <expected>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/core/error.hpp"

namespace asharia::persistence {
    namespace {

        struct DiagnosticField {
            std::string key;
            std::string value;

            DiagnosticField(const char* fieldKey, std::string_view fieldValue)
                : key{fieldKey}, value{fieldValue} {}
        };

        [[nodiscard]] Error migrationError(std::string message,
                                           std::initializer_list<DiagnosticField> details = {}) {
            bool wroteHeader = false;
            for (const DiagnosticField& detail : details) {
                if (detail.value.empty()) {
                    continue;
                }
                if (!wroteHeader) {
                    message += " [";
                    wroteHeader = true;
                } else {
                    message += "; ";
                }
                message += detail.key;
                message += '=';
                message += detail.value;
            }
            if (wroteHeader) {
                message += ']';
            }
            return Error{ErrorDomain::Persistence, 0, std::move(message)};
        }

    } // namespace

    VoidResult MigrationRegistry::registerMigration(schema::TypeId type, std::uint32_t fromVersion,
                                                    std::uint32_t toVersion, MigrationFn migrate) {
        if (!type || fromVersion == 0U || toVersion == 0U || fromVersion >= toVersion ||
            migrate == nullptr) {
            return std::unexpected{
                migrationError("Invalid persistence migration rule.",
                               {{"operation", "register"},
                                {"type", type.stableName},
                                {"fromVersion", std::to_string(fromVersion)},
                                {"toVersion", std::to_string(toVersion)},
                                {"expected", "type and increasing version migration"},
                                {"actual", "invalid rule"}})};
        }

        const auto duplicate = std::ranges::find_if(rules_, [&](const MigrationRule& rule) {
            return rule.type == type && rule.fromVersion == fromVersion;
        });
        if (duplicate != rules_.end()) {
            return std::unexpected{migrationError("Duplicate persistence migration rule.",
                                                  {{"operation", "register"},
                                                   {"type", type.stableName},
                                                   {"fromVersion", std::to_string(fromVersion)},
                                                   {"toVersion", std::to_string(toVersion)},
                                                   {"expected", "unique migration step"},
                                                   {"actual", "duplicate"}})};
        }

        rules_.push_back(MigrationRule{
            .type = std::move(type),
            .fromVersion = fromVersion,
            .toVersion = toVersion,
            .migrate = migrate,
        });
        return {};
    }

    Result<archive::ArchiveValue> MigrationRegistry::migrateObject(
        const schema::TypeId& type, std::string_view typeName, std::string_view objectPath,
        std::string_view archivePath, MigrationScenario scenario, std::uint32_t fromVersion,
        std::uint32_t toVersion, const archive::ArchiveValue& input) const {
        if (fromVersion == toVersion) {
            return input;
        }
        if (fromVersion == 0U || fromVersion > toVersion) {
            return std::unexpected{migrationError("Invalid persistence migration version range.",
                                                  {{"operation", "migrate"},
                                                   {"objectPath", objectPath},
                                                   {"type", typeName},
                                                   {"fromVersion", std::to_string(fromVersion)},
                                                   {"toVersion", std::to_string(toVersion)},
                                                   {"expected", "increasing version range"},
                                                   {"actual", "invalid range"}})};
        }

        archive::ArchiveValue current = input;
        std::uint32_t currentVersion = fromVersion;
        while (currentVersion < toVersion) {
            const MigrationRule* rule = findMigration(type, currentVersion);
            if (rule == nullptr) {
                return std::unexpected{
                    migrationError("Missing persistence migration.",
                                   {{"operation", "migrate"},
                                    {"objectPath", objectPath},
                                    {"type", typeName},
                                    {"fromVersion", std::to_string(currentVersion)},
                                    {"toVersion", std::to_string(toVersion)},
                                    {"expected", "registered migration step"},
                                    {"actual", "missing"}})};
            }
            archive::ArchiveValue next;
            MigrationContext context{
                .type = type,
                .typeName = typeName,
                .objectPath = objectPath,
                .archivePath = archivePath,
                .scenario = scenario,
                .fromVersion = rule->fromVersion,
                .toVersion = rule->toVersion,
                .input = &current,
                .output = &next,
            };
            auto migrated = rule->migrate(context);
            if (!migrated) {
                return std::unexpected{std::move(migrated.error())};
            }
            current = std::move(next);
            currentVersion = rule->toVersion;
        }
        return current;
    }

    const MigrationRule* MigrationRegistry::findMigration(const schema::TypeId& type,
                                                          std::uint32_t fromVersion) const {
        const auto found = std::ranges::find_if(rules_, [&](const MigrationRule& rule) {
            return rule.type == type && rule.fromVersion == fromVersion;
        });
        return found == rules_.end() ? nullptr : &*found;
    }

} // namespace asharia::persistence
