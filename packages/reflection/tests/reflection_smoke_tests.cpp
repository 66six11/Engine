#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "vke/reflection/context_view.hpp"
#include "vke/reflection/type_builder.hpp"

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

    const vke::reflection::FieldInfo* findField(const vke::reflection::TypeInfo& type,
                                                std::string_view name) {
        const auto found =
            std::ranges::find_if(type.fields, [name](const vke::reflection::FieldInfo& field) {
                return field.name == name;
            });
        return found == type.fields.end() ? nullptr : &*found;
    }

    bool hasContextField(const vke::reflection::ContextFieldView& view, std::string_view name) {
        return std::ranges::any_of(view.fields, [name](const vke::reflection::FieldInfo* field) {
            return field != nullptr && field->name == name;
        });
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

    bool smokeRegistry() {
        vke::reflection::TypeRegistry registry;
        auto registered = registerReflectionSmokeTypes(registry);
        if (!registered) {
            logFailure(registered.error().message);
            return false;
        }

        if (registry.findType(kSmokeTransformTypeName) == nullptr) {
            logFailure("Reflection registry smoke could not find the transform type.");
            return false;
        }

        vke::reflection::TypeInfo duplicate{
            .id = vke::reflection::makeTypeId(kSmokeTransformTypeName),
            .name = std::string{kSmokeTransformTypeName},
            .version = 1,
            .kind = vke::reflection::TypeKind::Component,
            .fields = {},
        };
        auto duplicateRegistered = registry.registerType(std::move(duplicate));
        if (duplicateRegistered) {
            logFailure("Reflection registry smoke accepted a duplicate type.");
            return false;
        }

        auto frozen = registry.freeze();
        if (!frozen) {
            logFailure(frozen.error().message);
            return false;
        }

        vke::reflection::TypeInfo lateType{
            .id = vke::reflection::makeTypeId("com.asharia.smoke.LateType"),
            .name = "com.asharia.smoke.LateType",
            .version = 1,
            .kind = vke::reflection::TypeKind::Struct,
            .fields = {},
        };
        auto lateRegistered = registry.registerType(std::move(lateType));
        if (lateRegistered) {
            logFailure("Reflection registry smoke accepted registration after freeze.");
            return false;
        }

        std::cout << "Reflection registry types: " << registry.types().size() << '\n';
        return true;
    }

    bool smokeTransform() {
        auto registry = makeReflectionSmokeRegistry();
        if (!registry) {
            return false;
        }

        const vke::reflection::TypeInfo* transformType =
            registry->findType(kSmokeTransformTypeName);
        if (transformType == nullptr || transformType->fields.size() != 6) {
            logFailure("Reflection transform smoke saw an unexpected field count.");
            return false;
        }

        const vke::reflection::FieldInfo* positionField = findField(*transformType, "position");
        const vke::reflection::FieldInfo* cachedField =
            findField(*transformType, "cachedMagnitude");
        if (positionField == nullptr || cachedField == nullptr ||
            !positionField->flags.has(vke::reflection::FieldFlag::Serializable) ||
            !positionField->flags.has(vke::reflection::FieldFlag::EditorVisible) ||
            !positionField->flags.has(vke::reflection::FieldFlag::RuntimeVisible) ||
            !positionField->flags.has(vke::reflection::FieldFlag::ScriptVisible) ||
            cachedField->accessor.writeAddress) {
            logFailure("Reflection transform smoke saw unexpected field metadata.");
            return false;
        }

        ReflectionSmokeTransform transform{
            .position = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
            .rotation = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
            .scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F},
            .debugName = "transform smoke",
            .cachedMagnitude = 3.0F,
            .scriptCounter = 7,
        };

        const auto* readPosition = static_cast<const ReflectionSmokeVec3*>(
            positionField->accessor.readAddress(&transform));
        auto* writePosition =
            static_cast<ReflectionSmokeVec3*>(positionField->accessor.writeAddress(&transform));
        if (readPosition == nullptr || writePosition == nullptr || readPosition->x != 1.0F) {
            logFailure("Reflection transform smoke failed to read position through accessor.");
            return false;
        }

        writePosition->x = 4.0F;
        if (transform.position.x != 4.0F) {
            logFailure("Reflection transform smoke failed to write position through accessor.");
            return false;
        }

        std::cout << "Reflection transform fields: " << transformType->fields.size() << '\n';
        return true;
    }

    bool smokeContexts() {
        auto registry = makeReflectionSmokeRegistry();
        if (!registry) {
            return false;
        }

        const vke::reflection::TypeInfo* transformType =
            registry->findType(kSmokeTransformTypeName);
        if (transformType == nullptr) {
            logFailure("Reflection context smoke could not find transform type.");
            return false;
        }

        const vke::reflection::ContextFieldView serializeView =
            vke::reflection::makeSerializeContextView(*transformType);
        const vke::reflection::ContextFieldView editView =
            vke::reflection::makeEditContextView(*transformType);
        const vke::reflection::ContextFieldView scriptView =
            vke::reflection::makeScriptContextView(*transformType);

        if (!hasContextField(serializeView, "debugName") ||
            hasContextField(serializeView, "cachedMagnitude") ||
            !hasContextField(editView, "cachedMagnitude") ||
            hasContextField(editView, "scriptCounter") ||
            !hasContextField(scriptView, "scriptCounter") ||
            hasContextField(scriptView, "debugName")) {
            logFailure("Reflection context smoke produced unexpected field projections.");
            return false;
        }

        std::cout << "Reflection context fields: serialize=" << serializeView.fields.size()
                  << ", edit=" << editView.fields.size() << ", script=" << scriptView.fields.size()
                  << '\n';
        return true;
    }

} // namespace

int main() {
    const bool passed = smokeRegistry() && smokeTransform() && smokeContexts();
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
