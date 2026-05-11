#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/core/error.hpp"
#include "asharia/reflection/type_builder.hpp"
#include "asharia/serialization/migration.hpp"
#include "asharia/serialization/serializer.hpp"
#include "asharia/serialization/text_archive.hpp"

namespace {

    constexpr std::string_view kSmokeVec3TypeName = "com.asharia.smoke.Vec3";
    constexpr std::string_view kSmokeQuatTypeName = "com.asharia.smoke.Quat";
    constexpr std::string_view kSmokeTransformTypeName = "com.asharia.smoke.Transform";
    constexpr std::string_view kSmokeMigratedTransformTypeName =
        "com.asharia.smoke.MigratedTransform";

    struct ReflectionSmokeVec3 {
        float x{};
        float y{};
        float z{};
    };

    struct ReflectionSmokeQuat {
        float x{};
        float y{};
        float z{};
        float w{1.0F};
    };

    struct ReflectionSmokeTransform {
        ReflectionSmokeVec3 position;
        ReflectionSmokeQuat rotation;
        ReflectionSmokeVec3 scale{.x = 1.0F, .y = 1.0F, .z = 1.0F};
        std::string debugName;
        float cachedMagnitude{};
        std::int32_t scriptCounter{};
    };

    struct ReflectionSmokeMigratedTransform {
        ReflectionSmokeVec3 position;
        ReflectionSmokeQuat rotation;
        ReflectionSmokeVec3 scale{.x = 1.0F, .y = 1.0F, .z = 1.0F};
        std::string debugName;
    };

    asharia::Error smokeSerializationError(std::string message) {
        return asharia::Error{asharia::ErrorDomain::Serialization, 0, std::move(message)};
    }

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    bool containsAll(std::string_view text, std::initializer_list<std::string_view> needles) {
        return std::ranges::all_of(needles, [text](std::string_view needle) {
            return text.find(needle) != std::string_view::npos;
        });
    }

    asharia::VoidResult registerReflectionSmokeTypes(asharia::reflection::TypeRegistry& registry) {
        using namespace asharia::reflection;

        auto builtins = registerBuiltinTypes(registry);
        if (!builtins) {
            return builtins;
        }

        const FieldFlagSet savedEditable = field_flags::serializableEditorRuntimeScript();
        const TypeId vec3Type = makeTypeId(kSmokeVec3TypeName);
        const TypeId quatType = makeTypeId(kSmokeQuatTypeName);

        auto vec3Registered = TypeBuilder<ReflectionSmokeVec3>(registry, kSmokeVec3TypeName)
                                  .kind(TypeKind::Struct)
                                  .field("x", &ReflectionSmokeVec3::x, savedEditable)
                                  .field("y", &ReflectionSmokeVec3::y, savedEditable)
                                  .field("z", &ReflectionSmokeVec3::z, savedEditable)
                                  .commit();
        if (!vec3Registered) {
            return vec3Registered;
        }

        auto quatRegistered = TypeBuilder<ReflectionSmokeQuat>(registry, kSmokeQuatTypeName)
                                  .kind(TypeKind::Struct)
                                  .field("x", &ReflectionSmokeQuat::x, savedEditable)
                                  .field("y", &ReflectionSmokeQuat::y, savedEditable)
                                  .field("z", &ReflectionSmokeQuat::z, savedEditable)
                                  .field("w", &ReflectionSmokeQuat::w, savedEditable)
                                  .commit();
        if (!quatRegistered) {
            return quatRegistered;
        }

        const FieldFlagSet editorOnly =
            FieldFlag::Serializable | FieldFlag::EditorVisible | FieldFlag::EditorOnly;
        const FieldFlagSet runtimeReadOnly =
            FieldFlag::EditorVisible | FieldFlag::RuntimeVisible | FieldFlag::ReadOnly;
        const FieldFlagSet scriptReadOnly =
            FieldFlag::RuntimeVisible | FieldFlag::ScriptVisible | FieldFlag::ReadOnly;

        auto transformRegistered =
            TypeBuilder<ReflectionSmokeTransform>(registry, kSmokeTransformTypeName)
                .kind(TypeKind::Component)
                .field("position", &ReflectionSmokeTransform::position, vec3Type, savedEditable)
                .field("rotation", &ReflectionSmokeTransform::rotation, quatType, savedEditable)
                .field("scale", &ReflectionSmokeTransform::scale, vec3Type, savedEditable)
                .field("debugName", &ReflectionSmokeTransform::debugName, editorOnly)
                .field("cachedMagnitude", &ReflectionSmokeTransform::cachedMagnitude,
                       runtimeReadOnly)
                .field("scriptCounter", &ReflectionSmokeTransform::scriptCounter, scriptReadOnly)
                .commit();
        if (!transformRegistered) {
            return transformRegistered;
        }

        return TypeBuilder<ReflectionSmokeMigratedTransform>(registry,
                                                             kSmokeMigratedTransformTypeName)
            .version(2)
            .kind(TypeKind::Component)
            .field("position", &ReflectionSmokeMigratedTransform::position, vec3Type, savedEditable)
            .field("rotation", &ReflectionSmokeMigratedTransform::rotation, quatType, savedEditable)
            .field("scale", &ReflectionSmokeMigratedTransform::scale, vec3Type, savedEditable)
            .field("debugName", &ReflectionSmokeMigratedTransform::debugName, editorOnly)
            .commit();
    }

    std::optional<asharia::reflection::TypeRegistry> makeReflectionSmokeRegistry() {
        asharia::reflection::TypeRegistry registry;
        auto registered = registerReflectionSmokeTypes(registry);
        if (!registered) {
            logFailure(registered.error().message);
            return std::nullopt;
        }

        auto frozen = registry.freeze();
        if (!frozen) {
            logFailure(frozen.error().message);
            return std::nullopt;
        }

        return registry;
    }

    asharia::serialization::ArchiveValue makeVec3Archive(float xValue, float yValue, float zValue) {
        using asharia::serialization::ArchiveMember;
        using asharia::serialization::ArchiveValue;

        return ArchiveValue::object({
            ArchiveMember{
                .key = "type",
                .value = ArchiveValue::string(std::string{kSmokeVec3TypeName}),
            },
            ArchiveMember{
                .key = "version",
                .value = ArchiveValue::integer(1),
            },
            ArchiveMember{
                .key = "fields",
                .value = ArchiveValue::object({
                    ArchiveMember{.key = "x", .value = ArchiveValue::floating(xValue)},
                    ArchiveMember{.key = "y", .value = ArchiveValue::floating(yValue)},
                    ArchiveMember{.key = "z", .value = ArchiveValue::floating(zValue)},
                }),
            },
        });
    }

    asharia::serialization::ArchiveValue
    makeQuatArchive(const asharia::serialization::ArchiveValue& zValue) {
        using asharia::serialization::ArchiveMember;
        using asharia::serialization::ArchiveValue;

        return ArchiveValue::object({
            ArchiveMember{
                .key = "type",
                .value = ArchiveValue::string(std::string{kSmokeQuatTypeName}),
            },
            ArchiveMember{
                .key = "version",
                .value = ArchiveValue::integer(1),
            },
            ArchiveMember{
                .key = "fields",
                .value = ArchiveValue::object({
                    ArchiveMember{.key = "x", .value = ArchiveValue::floating(0.0)},
                    ArchiveMember{.key = "y", .value = ArchiveValue::floating(0.0)},
                    ArchiveMember{.key = "z", .value = zValue},
                    ArchiveMember{.key = "w", .value = ArchiveValue::floating(1.0)},
                }),
            },
        });
    }

    asharia::serialization::ArchiveValue makeMigratedTransformV1Archive() {
        using asharia::serialization::ArchiveMember;
        using asharia::serialization::ArchiveValue;

        return ArchiveValue::object({
            ArchiveMember{
                .key = "type",
                .value = ArchiveValue::string(std::string{kSmokeMigratedTransformTypeName}),
            },
            ArchiveMember{
                .key = "version",
                .value = ArchiveValue::integer(1),
            },
            ArchiveMember{
                .key = "fields",
                .value = ArchiveValue::object({
                    ArchiveMember{
                        .key = "position",
                        .value = makeVec3Archive(1.0F, 2.0F, 3.0F),
                    },
                    ArchiveMember{
                        .key = "eulerRotation",
                        .value = makeVec3Archive(0.0F, 0.0F, 0.25F),
                    },
                    ArchiveMember{
                        .key = "debugName",
                        .value = ArchiveValue::string("migrated"),
                    },
                }),
            },
        });
    }

    asharia::VoidResult
    migrateSmokeTransformV1ToV2(asharia::serialization::MigrationContext& context) {
        using asharia::serialization::ArchiveMember;
        using asharia::serialization::ArchiveValue;

        const ArchiveValue* inputFields =
            context.input == nullptr ? nullptr : context.input->findMemberValue("fields");
        const ArchiveValue* position =
            inputFields == nullptr ? nullptr : inputFields->findMemberValue("position");
        const ArchiveValue* eulerRotation =
            inputFields == nullptr ? nullptr : inputFields->findMemberValue("eulerRotation");
        const ArchiveValue* debugName =
            inputFields == nullptr ? nullptr : inputFields->findMemberValue("debugName");
        const ArchiveValue* eulerFields =
            eulerRotation == nullptr ? nullptr : eulerRotation->findMemberValue("fields");
        const ArchiveValue* eulerZ =
            eulerFields == nullptr ? nullptr : eulerFields->findMemberValue("z");
        if (context.output == nullptr || position == nullptr || eulerZ == nullptr) {
            return std::unexpected{
                smokeSerializationError("Smoke migration is missing position or eulerRotation.z.")};
        }

        std::vector<ArchiveMember> migratedFields{
            ArchiveMember{.key = "position", .value = *position},
            ArchiveMember{.key = "rotation", .value = makeQuatArchive(*eulerZ)},
            ArchiveMember{.key = "scale", .value = makeVec3Archive(1.0F, 1.0F, 1.0F)},
        };
        if (debugName != nullptr) {
            migratedFields.push_back(ArchiveMember{.key = "debugName", .value = *debugName});
        }

        *context.output = ArchiveValue::object({
            ArchiveMember{
                .key = "type",
                .value = ArchiveValue::string(std::string{kSmokeMigratedTransformTypeName}),
            },
            ArchiveMember{
                .key = "version",
                .value = ArchiveValue::integer(context.toVersion),
            },
            ArchiveMember{
                .key = "fields",
                .value = ArchiveValue::object(std::move(migratedFields)),
            },
        });
        return {};
    }

    bool smokeRoundtrip() {
        auto registry = makeReflectionSmokeRegistry();
        if (!registry) {
            return false;
        }

        const asharia::reflection::TypeId transformType =
            asharia::reflection::makeTypeId(kSmokeTransformTypeName);
        const ReflectionSmokeTransform source{
            .position = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
            .rotation = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
            .scale = {.x = 2.0F, .y = 2.0F, .z = 2.0F},
            .debugName = "roundtrip",
            .cachedMagnitude = 99.0F,
            .scriptCounter = 12,
        };

        auto archive = asharia::serialization::serializeObject(*registry, transformType, &source);
        if (!archive) {
            logFailure(archive.error().message);
            return false;
        }

        auto firstText = asharia::serialization::writeTextArchive(*archive);
        if (!firstText) {
            logFailure(firstText.error().message);
            return false;
        }

        auto secondText = asharia::serialization::writeTextArchive(*archive);
        if (!secondText) {
            logFailure(secondText.error().message);
            return false;
        }

        if (*firstText != *secondText) {
            logFailure("Serialization roundtrip smoke produced nondeterministic text.");
            return false;
        }

        ReflectionSmokeTransform loaded{};
        auto loadedResult =
            asharia::serialization::deserializeObject(*registry, transformType, *archive, &loaded);
        if (!loadedResult) {
            logFailure(loadedResult.error().message);
            return false;
        }

        if (loaded.position.x != source.position.x || loaded.position.y != source.position.y ||
            loaded.position.z != source.position.z || loaded.rotation.w != source.rotation.w ||
            loaded.scale.x != source.scale.x || loaded.debugName != source.debugName ||
            loaded.cachedMagnitude != 0.0F || loaded.scriptCounter != 0) {
            logFailure("Serialization roundtrip smoke loaded unexpected values.");
            return false;
        }

        asharia::serialization::ArchiveValue badArchive = *archive;
        asharia::serialization::ArchiveValue* fields = badArchive.findMemberValue("fields");
        asharia::serialization::ArchiveValue* position =
            fields == nullptr ? nullptr : fields->findMemberValue("position");
        asharia::serialization::ArchiveValue* positionFields =
            position == nullptr ? nullptr : position->findMemberValue("fields");
        asharia::serialization::ArchiveValue* xValue =
            positionFields == nullptr ? nullptr : positionFields->findMemberValue("x");
        if (xValue == nullptr) {
            logFailure("Serialization roundtrip smoke could not edit the bad archive.");
            return false;
        }
        *xValue = asharia::serialization::ArchiveValue::string("wrong");

        ReflectionSmokeTransform rejected{};
        auto rejectedResult = asharia::serialization::deserializeObject(*registry, transformType,
                                                                        badArchive, &rejected);
        if (rejectedResult ||
            !containsAll(rejectedResult.error().message,
                         {
                             "operation=deserialize",
                             "objectPath=com.asharia.smoke.Transform.position.x",
                             "type=com.asharia.core.Float",
                             "expected=float",
                             "actual=string",
                         })) {
            logFailure("Serialization roundtrip smoke did not reject a bad field type.");
            return false;
        }

        asharia::serialization::ArchiveValue badVersionArchive = *archive;
        asharia::serialization::ArchiveValue* versionValue =
            badVersionArchive.findMemberValue("version");
        if (versionValue == nullptr) {
            logFailure("Serialization roundtrip smoke could not edit the archive version.");
            return false;
        }
        *versionValue = asharia::serialization::ArchiveValue::integer(2);

        ReflectionSmokeTransform rejectedVersion{};
        auto rejectedVersionResult = asharia::serialization::deserializeObject(
            *registry, transformType, badVersionArchive, &rejectedVersion);
        if (rejectedVersionResult ||
            !containsAll(rejectedVersionResult.error().message,
                         {
                             "operation=deserialize",
                             "objectPath=com.asharia.smoke.Transform",
                             "type=com.asharia.smoke.Transform",
                             "field=version",
                             "expected=migration policy",
                             "actual=no migration registry",
                             "version=2",
                         })) {
            logFailure("Serialization roundtrip smoke did not reject a bad type version.");
            return false;
        }

        std::cout << "Serialization roundtrip bytes: " << firstText->size() << '\n';
        return true;
    }

    bool smokeJsonArchive() {
        std::string escaped = "quote \" slash \\ newline \n carriage \r tab \t";
        escaped.push_back('\b');
        escaped.push_back('\f');

        std::string validUtf8 = "utf8 ";
        validUtf8.push_back(static_cast<char>(0xC3));
        validUtf8.push_back(static_cast<char>(0xA9));

        asharia::serialization::ArchiveValue archive =
            asharia::serialization::ArchiveValue::object({
                asharia::serialization::ArchiveMember{
                    .key = "escaped",
                    .value = asharia::serialization::ArchiveValue::string(escaped),
                },
                asharia::serialization::ArchiveMember{
                    .key = "validUtf8",
                    .value = asharia::serialization::ArchiveValue::string(validUtf8),
                },
                asharia::serialization::ArchiveMember{
                    .key = "array",
                    .value = asharia::serialization::ArchiveValue::array({
                        asharia::serialization::ArchiveValue::integer(7),
                        asharia::serialization::ArchiveValue::floating(0.25),
                        asharia::serialization::ArchiveValue::boolean(true),
                    }),
                },
            });

        auto firstText = asharia::serialization::writeTextArchive(archive);
        if (!firstText) {
            logFailure(firstText.error().message);
            return false;
        }

        auto secondText = asharia::serialization::writeTextArchive(archive);
        if (!secondText) {
            logFailure(secondText.error().message);
            return false;
        }

        if (*firstText != *secondText) {
            logFailure("JSON archive smoke produced nondeterministic output.");
            return false;
        }

        auto parsed = asharia::serialization::readTextArchive(*firstText);
        if (!parsed) {
            logFailure(parsed.error().message);
            return false;
        }

        const asharia::serialization::ArchiveValue* parsedEscaped =
            parsed->findMemberValue("escaped");
        const asharia::serialization::ArchiveValue* parsedUtf8 =
            parsed->findMemberValue("validUtf8");
        if (parsedEscaped == nullptr ||
            parsedEscaped->kind != asharia::serialization::ArchiveValueKind::String ||
            parsedEscaped->stringValue != escaped || parsedUtf8 == nullptr ||
            parsedUtf8->kind != asharia::serialization::ArchiveValueKind::String ||
            parsedUtf8->stringValue != validUtf8) {
            logFailure("JSON archive smoke failed to round-trip escaped strings.");
            return false;
        }

        auto duplicate = asharia::serialization::readTextArchive(R"({"field":1,"field":2})");
        if (duplicate || duplicate.error().message.find("duplicate key") == std::string::npos) {
            logFailure("JSON archive smoke did not reject a duplicate object key.");
            return false;
        }

        auto malformed = asharia::serialization::readTextArchive("{");
        if (malformed || malformed.error().message.find("byte") == std::string::npos) {
            logFailure("JSON archive smoke did not report a parse byte for malformed input.");
            return false;
        }

        std::string invalidUtf8;
        invalidUtf8.push_back(static_cast<char>(0xFF));
        auto invalidWrite = asharia::serialization::writeTextArchive(
            asharia::serialization::ArchiveValue::string(invalidUtf8));
        if (invalidWrite) {
            logFailure("JSON archive smoke accepted invalid UTF-8 output.");
            return false;
        }

        auto duplicateObjectWrite =
            asharia::serialization::writeTextArchive(asharia::serialization::ArchiveValue::object({
                asharia::serialization::ArchiveMember{
                    .key = "field",
                    .value = asharia::serialization::ArchiveValue::integer(1),
                },
                asharia::serialization::ArchiveMember{
                    .key = "field",
                    .value = asharia::serialization::ArchiveValue::integer(2),
                },
            }));
        if (duplicateObjectWrite ||
            duplicateObjectWrite.error().message.find("duplicate key") == std::string::npos) {
            logFailure("JSON archive smoke did not reject duplicate ArchiveValue keys.");
            return false;
        }

        auto nonFiniteWrite =
            asharia::serialization::writeTextArchive(asharia::serialization::ArchiveValue::floating(
                std::numeric_limits<double>::infinity()));
        if (nonFiniteWrite ||
            nonFiniteWrite.error().message.find("non-finite") == std::string::npos) {
            logFailure("JSON archive smoke did not reject a non-finite float.");
            return false;
        }

        std::cout << "Serialization JSON archive bytes: " << firstText->size() << '\n';
        return true;
    }

    bool smokeMigration() {
        auto registry = makeReflectionSmokeRegistry();
        if (!registry) {
            return false;
        }

        const asharia::reflection::TypeId migratedTransformType =
            asharia::reflection::makeTypeId(kSmokeMigratedTransformTypeName);
        asharia::serialization::MigrationRegistry migrations;
        auto migrationRegistered =
            migrations.registerMigration(migratedTransformType, 1, 2, migrateSmokeTransformV1ToV2);
        if (!migrationRegistered) {
            logFailure(migrationRegistered.error().message);
            return false;
        }

        auto duplicateMigration =
            migrations.registerMigration(migratedTransformType, 1, 2, migrateSmokeTransformV1ToV2);
        if (duplicateMigration ||
            !containsAll(duplicateMigration.error().message,
                         {
                             "operation=register",
                             "fromVersion=1",
                             "toVersion=2",
                             "expected=unique migration step",
                             "actual=duplicate",
                         })) {
            logFailure("Serialization migration smoke accepted a duplicate migration.");
            return false;
        }

        const asharia::serialization::ArchiveValue oldArchive = makeMigratedTransformV1Archive();
        ReflectionSmokeMigratedTransform rejectedWithoutMigration{};
        auto missingMigrationResult = asharia::serialization::deserializeObject(
            *registry, migratedTransformType, oldArchive, &rejectedWithoutMigration);
        if (missingMigrationResult ||
            !containsAll(missingMigrationResult.error().message,
                         {
                             "requires migration",
                             "operation=deserialize",
                             "objectPath=com.asharia.smoke.MigratedTransform",
                             "type=com.asharia.smoke.MigratedTransform",
                             "expected=migration policy",
                             "actual=no migration registry",
                             "version=1",
                         })) {
            logFailure("Serialization migration smoke did not require a migration policy.");
            return false;
        }

        const asharia::serialization::SerializationPolicy policy{
            .includeTypeHeader = true,
            .allowUnknownFields = false,
            .migrations = &migrations,
        };
        ReflectionSmokeMigratedTransform loaded{};
        auto loadedResult = asharia::serialization::deserializeObject(
            *registry, migratedTransformType, oldArchive, &loaded, policy);
        if (!loadedResult) {
            logFailure(loadedResult.error().message);
            return false;
        }

        if (loaded.position.x != 1.0F || loaded.position.y != 2.0F || loaded.position.z != 3.0F ||
            loaded.rotation.x != 0.0F || loaded.rotation.y != 0.0F || loaded.rotation.z != 0.25F ||
            loaded.rotation.w != 1.0F || loaded.scale.x != 1.0F || loaded.scale.y != 1.0F ||
            loaded.scale.z != 1.0F || loaded.debugName != "migrated") {
            logFailure("Serialization migration smoke loaded unexpected migrated values.");
            return false;
        }

        asharia::serialization::MigrationRegistry emptyMigrations;
        const asharia::serialization::SerializationPolicy missingPolicy{
            .includeTypeHeader = true,
            .allowUnknownFields = false,
            .migrations = &emptyMigrations,
        };
        ReflectionSmokeMigratedTransform rejectedMissingRule{};
        auto missingRuleResult = asharia::serialization::deserializeObject(
            *registry, migratedTransformType, oldArchive, &rejectedMissingRule, missingPolicy);
        if (missingRuleResult ||
            !containsAll(missingRuleResult.error().message,
                         {
                             "Missing serialization migration",
                             "operation=migrate",
                             "fromVersion=1",
                             "toVersion=2",
                             "expected=registered migration step",
                             "actual=missing",
                         })) {
            logFailure("Serialization migration smoke did not reject a missing migration rule.");
            return false;
        }

        std::cout << "Serialization migration version: 1 -> 2\n";
        return true;
    }

} // namespace

int main() {
    try {
        const bool passed = smokeRoundtrip() && smokeJsonArchive() && smokeMigration();
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::fputs("Serialization smoke test threw an exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
    } catch (...) {
        std::fputs("Serialization smoke test threw an unknown exception.\n", stderr);
    }
    return EXIT_FAILURE;
}
