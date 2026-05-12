#include "asharia/serialization/migration.hpp"

#include <algorithm>
#include <expected>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/core/error.hpp"

namespace asharia::serialization {
    namespace {

        struct DiagnosticField {
            std::string key;
            std::string value;

            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
            DiagnosticField(std::string_view fieldKey, std::string_view fieldValue)
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
            return Error{ErrorDomain::Serialization, 0, std::move(message)};
        }

        [[nodiscard]] std::string typeIdLabel(reflection::TypeId type) {
            return std::to_string(type.value);
        }

    } // namespace

    VoidResult MigrationRegistry::registerMigration(reflection::TypeId type,
                                                    std::uint32_t fromVersion,
                                                    std::uint32_t toVersion, MigrationFn migrate) {
        if (!type) {
            return std::unexpected{
                migrationError("Cannot register serialization migration for an invalid type id.",
                               {
                                   {"operation", "register"},
                                   {"typeId", typeIdLabel(type)},
                                   {"fromVersion", std::to_string(fromVersion)},
                                   {"toVersion", std::to_string(toVersion)},
                                   {"expected", "valid type id"},
                                   {"actual", "0"},
                               })};
        }
        if (fromVersion == 0 || toVersion == 0) {
            return std::unexpected{migrationError(
                "Cannot register serialization migration with a zero schema version.",
                {
                    {"operation", "register"},
                    {"typeId", typeIdLabel(type)},
                    {"fromVersion", std::to_string(fromVersion)},
                    {"toVersion", std::to_string(toVersion)},
                    {"expected", "nonzero schema versions"},
                    {"actual", "zero schema version"},
                })};
        }
        if (fromVersion == std::numeric_limits<std::uint32_t>::max() ||
            toVersion != fromVersion + 1U) {
            return std::unexpected{
                migrationError("Serialization migrations must advance exactly one schema version.",
                               {
                                   {"operation", "register"},
                                   {"typeId", typeIdLabel(type)},
                                   {"fromVersion", std::to_string(fromVersion)},
                                   {"toVersion", std::to_string(toVersion)},
                                   {"expected", "toVersion = fromVersion + 1"},
                                   {"actual", std::to_string(toVersion)},
                               })};
        }
        if (migrate == nullptr) {
            return std::unexpected{
                migrationError("Cannot register null serialization migration function.",
                               {
                                   {"operation", "register"},
                                   {"typeId", typeIdLabel(type)},
                                   {"fromVersion", std::to_string(fromVersion)},
                                   {"toVersion", std::to_string(toVersion)},
                                   {"expected", "migration function"},
                                   {"actual", "null"},
                               })};
        }
        if (findMigration(type, fromVersion) != nullptr) {
            return std::unexpected{migrationError("Duplicate serialization migration for type id " +
                                                      typeIdLabel(type) + " from version " +
                                                      std::to_string(fromVersion) + ".",
                                                  {
                                                      {"operation", "register"},
                                                      {"typeId", typeIdLabel(type)},
                                                      {"fromVersion", std::to_string(fromVersion)},
                                                      {"toVersion", std::to_string(toVersion)},
                                                      {"expected", "unique migration step"},
                                                      {"actual", "duplicate"},
                                                  })};
        }

        rules_.push_back(MigrationRule{
            .type = type,
            .fromVersion = fromVersion,
            .toVersion = toVersion,
            .migrate = migrate,
        });
        return {};
    }

    Result<ArchiveValue> MigrationRegistry::migrateObject(reflection::TypeId type,
                                                          std::uint32_t fromVersion,
                                                          std::uint32_t toVersion,
                                                          const ArchiveValue& input) const {
        if (fromVersion == toVersion) {
            return input;
        }
        if (fromVersion > toVersion) {
            return std::unexpected{migrationError(
                "Cannot migrate type id " + typeIdLabel(type) + " backward from " +
                    std::to_string(fromVersion) + " to " + std::to_string(toVersion) + ".",
                {
                    {"operation", "migrate"},
                    {"typeId", typeIdLabel(type)},
                    {"fromVersion", std::to_string(fromVersion)},
                    {"toVersion", std::to_string(toVersion)},
                    {"expected", "nondecreasing version"},
                    {"actual", "backward migration"},
                })};
        }

        ArchiveValue current = input;
        std::uint32_t currentVersion = fromVersion;
        while (currentVersion < toVersion) {
            const MigrationRule* rule = findMigration(type, currentVersion);
            if (rule == nullptr) {
                return std::unexpected{migrationError(
                    "Missing serialization migration for type id " + typeIdLabel(type) +
                        " from version " + std::to_string(currentVersion) + " to " +
                        std::to_string(currentVersion + 1U) + ".",
                    {
                        {"operation", "migrate"},
                        {"typeId", typeIdLabel(type)},
                        {"fromVersion", std::to_string(currentVersion)},
                        {"toVersion", std::to_string(currentVersion + 1U)},
                        {"expected", "registered migration step"},
                        {"actual", "missing"},
                    })};
            }

            ArchiveValue output;
            MigrationContext context{
                .type = type,
                .fromVersion = currentVersion,
                .toVersion = rule->toVersion,
                .input = &current,
                .output = &output,
            };
            auto migrated = rule->migrate(context);
            if (!migrated) {
                return std::unexpected{migrationError(
                    "Serialization migration failed for type id " + typeIdLabel(type) +
                        " from version " + std::to_string(currentVersion) + " to " +
                        std::to_string(rule->toVersion) + ": " + migrated.error().message,
                    {
                        {"operation", "migrate"},
                        {"typeId", typeIdLabel(type)},
                        {"fromVersion", std::to_string(currentVersion)},
                        {"toVersion", std::to_string(rule->toVersion)},
                        {"expected", "successful migration step"},
                        {"actual", "migration function failed"},
                    })};
            }
            if (output.kind != ArchiveValueKind::Object) {
                return std::unexpected{migrationError(
                    "Serialization migration for type id " + typeIdLabel(type) + " from version " +
                        std::to_string(currentVersion) + " did not produce an object archive.",
                    {
                        {"operation", "migrate"},
                        {"typeId", typeIdLabel(type)},
                        {"fromVersion", std::to_string(currentVersion)},
                        {"toVersion", std::to_string(rule->toVersion)},
                        {"expected", "object archive"},
                        {"actual", "non-object archive"},
                    })};
            }

            current = std::move(output);
            currentVersion = rule->toVersion;
        }

        return current;
    }

    const MigrationRule* MigrationRegistry::findMigration(reflection::TypeId type,
                                                          std::uint32_t fromVersion) const {
        const auto found =
            std::ranges::find_if(rules_, [type, fromVersion](const MigrationRule& rule) {
                return rule.type == type && rule.fromVersion == fromVersion;
            });
        return found == rules_.end() ? nullptr : &*found;
    }

} // namespace asharia::serialization
