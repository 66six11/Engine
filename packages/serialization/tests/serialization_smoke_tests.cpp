#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "vke/reflection/type_builder.hpp"
#include "vke/serialization/serializer.hpp"
#include "vke/serialization/text_archive.hpp"

namespace {

    constexpr std::string_view kSmokeVec3TypeName = "com.asharia.smoke.Vec3";
    constexpr std::string_view kSmokeQuatTypeName = "com.asharia.smoke.Quat";
    constexpr std::string_view kSmokeTransformTypeName = "com.asharia.smoke.Transform";

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

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    vke::VoidResult registerReflectionSmokeTypes(vke::reflection::TypeRegistry& registry) {
        using namespace vke::reflection;

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

        return TypeBuilder<ReflectionSmokeTransform>(registry, kSmokeTransformTypeName)
            .kind(TypeKind::Component)
            .field("position", &ReflectionSmokeTransform::position, vec3Type, savedEditable)
            .field("rotation", &ReflectionSmokeTransform::rotation, quatType, savedEditable)
            .field("scale", &ReflectionSmokeTransform::scale, vec3Type, savedEditable)
            .field("debugName", &ReflectionSmokeTransform::debugName, editorOnly)
            .field("cachedMagnitude", &ReflectionSmokeTransform::cachedMagnitude, runtimeReadOnly)
            .field("scriptCounter", &ReflectionSmokeTransform::scriptCounter, scriptReadOnly)
            .commit();
    }

    std::optional<vke::reflection::TypeRegistry> makeReflectionSmokeRegistry() {
        vke::reflection::TypeRegistry registry;
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

    bool smokeRoundtrip() {
        auto registry = makeReflectionSmokeRegistry();
        if (!registry) {
            return false;
        }

        const vke::reflection::TypeId transformType =
            vke::reflection::makeTypeId(kSmokeTransformTypeName);
        const ReflectionSmokeTransform source{
            .position = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
            .rotation = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
            .scale = {.x = 2.0F, .y = 2.0F, .z = 2.0F},
            .debugName = "roundtrip",
            .cachedMagnitude = 99.0F,
            .scriptCounter = 12,
        };

        auto archive = vke::serialization::serializeObject(*registry, transformType, &source);
        if (!archive) {
            logFailure(archive.error().message);
            return false;
        }

        auto firstText = vke::serialization::writeTextArchive(*archive);
        if (!firstText) {
            logFailure(firstText.error().message);
            return false;
        }

        auto secondText = vke::serialization::writeTextArchive(*archive);
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
            vke::serialization::deserializeObject(*registry, transformType, *archive, &loaded);
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

        vke::serialization::ArchiveValue badArchive = *archive;
        vke::serialization::ArchiveValue* fields = badArchive.findMemberValue("fields");
        vke::serialization::ArchiveValue* position =
            fields == nullptr ? nullptr : fields->findMemberValue("position");
        vke::serialization::ArchiveValue* positionFields =
            position == nullptr ? nullptr : position->findMemberValue("fields");
        vke::serialization::ArchiveValue* xValue =
            positionFields == nullptr ? nullptr : positionFields->findMemberValue("x");
        if (xValue == nullptr) {
            logFailure("Serialization roundtrip smoke could not edit the bad archive.");
            return false;
        }
        *xValue = vke::serialization::ArchiveValue::string("wrong");

        ReflectionSmokeTransform rejected{};
        auto rejectedResult =
            vke::serialization::deserializeObject(*registry, transformType, badArchive, &rejected);
        if (rejectedResult || rejectedResult.error().message.find(".x") == std::string::npos) {
            logFailure("Serialization roundtrip smoke did not reject a bad field type.");
            return false;
        }

        vke::serialization::ArchiveValue badVersionArchive = *archive;
        vke::serialization::ArchiveValue* versionValue =
            badVersionArchive.findMemberValue("version");
        if (versionValue == nullptr) {
            logFailure("Serialization roundtrip smoke could not edit the archive version.");
            return false;
        }
        *versionValue = vke::serialization::ArchiveValue::integer(2);

        ReflectionSmokeTransform rejectedVersion{};
        auto rejectedVersionResult = vke::serialization::deserializeObject(
            *registry, transformType, badVersionArchive, &rejectedVersion);
        if (rejectedVersionResult ||
            rejectedVersionResult.error().message.find("version") == std::string::npos) {
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

        vke::serialization::ArchiveValue archive = vke::serialization::ArchiveValue::object({
            vke::serialization::ArchiveMember{
                .key = "escaped",
                .value = vke::serialization::ArchiveValue::string(escaped),
            },
            vke::serialization::ArchiveMember{
                .key = "validUtf8",
                .value = vke::serialization::ArchiveValue::string(validUtf8),
            },
            vke::serialization::ArchiveMember{
                .key = "array",
                .value = vke::serialization::ArchiveValue::array({
                    vke::serialization::ArchiveValue::integer(7),
                    vke::serialization::ArchiveValue::floating(0.25),
                    vke::serialization::ArchiveValue::boolean(true),
                }),
            },
        });

        auto firstText = vke::serialization::writeTextArchive(archive);
        if (!firstText) {
            logFailure(firstText.error().message);
            return false;
        }

        auto secondText = vke::serialization::writeTextArchive(archive);
        if (!secondText) {
            logFailure(secondText.error().message);
            return false;
        }

        if (*firstText != *secondText) {
            logFailure("JSON archive smoke produced nondeterministic output.");
            return false;
        }

        auto parsed = vke::serialization::readTextArchive(*firstText);
        if (!parsed) {
            logFailure(parsed.error().message);
            return false;
        }

        const vke::serialization::ArchiveValue* parsedEscaped = parsed->findMemberValue("escaped");
        const vke::serialization::ArchiveValue* parsedUtf8 = parsed->findMemberValue("validUtf8");
        if (parsedEscaped == nullptr ||
            parsedEscaped->kind != vke::serialization::ArchiveValueKind::String ||
            parsedEscaped->stringValue != escaped || parsedUtf8 == nullptr ||
            parsedUtf8->kind != vke::serialization::ArchiveValueKind::String ||
            parsedUtf8->stringValue != validUtf8) {
            logFailure("JSON archive smoke failed to round-trip escaped strings.");
            return false;
        }

        auto duplicate = vke::serialization::readTextArchive(R"({"field":1,"field":2})");
        if (duplicate || duplicate.error().message.find("duplicate key") == std::string::npos) {
            logFailure("JSON archive smoke did not reject a duplicate object key.");
            return false;
        }

        auto malformed = vke::serialization::readTextArchive("{");
        if (malformed || malformed.error().message.find("byte") == std::string::npos) {
            logFailure("JSON archive smoke did not report a parse byte for malformed input.");
            return false;
        }

        std::string invalidUtf8;
        invalidUtf8.push_back(static_cast<char>(0xFF));
        auto invalidWrite = vke::serialization::writeTextArchive(
            vke::serialization::ArchiveValue::string(invalidUtf8));
        if (invalidWrite) {
            logFailure("JSON archive smoke accepted invalid UTF-8 output.");
            return false;
        }

        auto duplicateObjectWrite =
            vke::serialization::writeTextArchive(vke::serialization::ArchiveValue::object({
                vke::serialization::ArchiveMember{
                    .key = "field",
                    .value = vke::serialization::ArchiveValue::integer(1),
                },
                vke::serialization::ArchiveMember{
                    .key = "field",
                    .value = vke::serialization::ArchiveValue::integer(2),
                },
            }));
        if (duplicateObjectWrite ||
            duplicateObjectWrite.error().message.find("duplicate key") == std::string::npos) {
            logFailure("JSON archive smoke did not reject duplicate ArchiveValue keys.");
            return false;
        }

        auto nonFiniteWrite = vke::serialization::writeTextArchive(
            vke::serialization::ArchiveValue::floating(std::numeric_limits<double>::infinity()));
        if (nonFiniteWrite ||
            nonFiniteWrite.error().message.find("non-finite") == std::string::npos) {
            logFailure("JSON archive smoke did not reject a non-finite float.");
            return false;
        }

        std::cout << "Serialization JSON archive bytes: " << firstText->size() << '\n';
        return true;
    }

} // namespace

int main() {
    const bool passed = smokeRoundtrip() && smokeJsonArchive();
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
