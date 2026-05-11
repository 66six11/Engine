#include "asharia/serialization/migration.hpp"

#include <algorithm>
#include <expected>
#include <limits>
#include <string>
#include <utility>

#include "asharia/core/error.hpp"

namespace asharia::serialization {
    namespace {

        [[nodiscard]] Error migrationError(std::string message) {
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
                migrationError("Cannot register serialization migration for an invalid type id.")};
        }
        if (fromVersion == 0 || toVersion == 0) {
            return std::unexpected{migrationError(
                "Cannot register serialization migration with a zero schema version.")};
        }
        if (fromVersion == std::numeric_limits<std::uint32_t>::max() ||
            toVersion != fromVersion + 1U) {
            return std::unexpected{migrationError(
                "Serialization migrations must advance exactly one schema version.")};
        }
        if (migrate == nullptr) {
            return std::unexpected{
                migrationError("Cannot register null serialization migration function.")};
        }
        if (findMigration(type, fromVersion) != nullptr) {
            return std::unexpected{migrationError("Duplicate serialization migration for type id " +
                                                  typeIdLabel(type) + " from version " +
                                                  std::to_string(fromVersion) + ".")};
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
            return std::unexpected{migrationError("Cannot migrate type id " + typeIdLabel(type) +
                                                  " backward from " + std::to_string(fromVersion) +
                                                  " to " + std::to_string(toVersion) + ".")};
        }

        ArchiveValue current = input;
        std::uint32_t currentVersion = fromVersion;
        while (currentVersion < toVersion) {
            const MigrationRule* rule = findMigration(type, currentVersion);
            if (rule == nullptr) {
                return std::unexpected{migrationError(
                    "Missing serialization migration for type id " + typeIdLabel(type) +
                    " from version " + std::to_string(currentVersion) + " to " +
                    std::to_string(currentVersion + 1U) + ".")};
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
                    std::to_string(rule->toVersion) + ": " + migrated.error().message)};
            }
            if (output.kind != ArchiveValueKind::Object) {
                return std::unexpected{migrationError(
                    "Serialization migration for type id " + typeIdLabel(type) + " from version " +
                    std::to_string(currentVersion) + " did not produce an object archive.")};
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
