#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/reflection/context_view.hpp"
#include "asharia/reflection/type_builder.hpp"

namespace {

    constexpr std::string_view kSmokeVec3TypeName = "com.asharia.smoke.Vec3";
    constexpr std::string_view kSmokeQuatTypeName = "com.asharia.smoke.Quat";
    constexpr std::string_view kSmokeTransformTypeName = "com.asharia.smoke.Transform";
    constexpr std::string_view kSmokePropertyComponentTypeName =
        "com.asharia.smoke.PropertyComponent";
    constexpr std::string_view kSmokeDeferredOwnerTypeName = "com.asharia.smoke.DeferredOwner";
    constexpr std::string_view kSmokeDeferredValueTypeName = "com.asharia.smoke.DeferredValue";

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

    struct ReflectionSmokeDeferredValue {
        std::int32_t value{};
    };

    struct ReflectionSmokeDeferredOwner {
        ReflectionSmokeDeferredValue deferred;
    };

    struct ReflectionSmokePropertyComponent {
        [[nodiscard]] float exposure() const noexcept {
            return exposure_;
        }

        [[nodiscard]] std::int32_t derivedCounter() const noexcept {
            return counter_ + 1;
        }

        asharia::VoidResult setExposure(const float& exposure) noexcept {
            exposure_ = exposure;
            dirty_ = true;
            return {};
        }

        [[nodiscard]] bool isDirty() const noexcept {
            return dirty_;
        }

    private:
        float exposure_{1.5F};
        std::int32_t counter_{40};
        bool dirty_{};
    };

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    bool containsAll(std::string_view text, std::initializer_list<std::string_view> needles) {
        return std::ranges::all_of(needles, [text](std::string_view needle) {
            return text.find(needle) != std::string_view::npos;
        });
    }

    const asharia::reflection::FieldInfo* findField(const asharia::reflection::TypeInfo& type,
                                                    std::string_view name) {
        const auto found =
            std::ranges::find_if(type.fields, [name](const asharia::reflection::FieldInfo& field) {
                return field.name == name;
            });
        return found == type.fields.end() ? nullptr : &*found;
    }

    bool hasContextField(const asharia::reflection::ContextFieldView& view, std::string_view name) {
        return std::ranges::any_of(view.fields,
                                   [name](const asharia::reflection::FieldInfo* field) {
                                       return field != nullptr && field->name == name;
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

        return TypeBuilder<ReflectionSmokePropertyComponent>(registry,
                                                             kSmokePropertyComponentTypeName)
            .kind(TypeKind::Component)
            .property<float>(
                "exposure",
                [](const ReflectionSmokePropertyComponent& component) {
                    return component.exposure();
                },
                [](ReflectionSmokePropertyComponent& component, const float& exposure) {
                    return component.setExposure(exposure);
                },
                savedEditable)
            .readonlyProperty<std::int32_t>(
                "derivedCounter",
                [](const ReflectionSmokePropertyComponent& component) {
                    return component.derivedCounter();
                },
                FieldFlag::EditorVisible | FieldFlag::RuntimeVisible)
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

    bool smokeDeferredFieldRegistration() {
        using namespace asharia::reflection;

        TypeRegistry registry;
        auto builtins = registerBuiltinTypes(registry);
        if (!builtins) {
            logFailure(builtins.error().message);
            return false;
        }

        const FieldFlagSet saved = field_flags::serializableEditorRuntime();
        const TypeId deferredValueType = makeTypeId(kSmokeDeferredValueTypeName);
        auto ownerRegistered =
            TypeBuilder<ReflectionSmokeDeferredOwner>(registry, kSmokeDeferredOwnerTypeName)
                .kind(TypeKind::Struct)
                .field("deferred", &ReflectionSmokeDeferredOwner::deferred, deferredValueType,
                       saved)
                .commit();
        if (!ownerRegistered) {
            logFailure(ownerRegistered.error().message);
            return false;
        }

        auto missingFreeze = registry.freeze();
        if (missingFreeze ||
            !containsAll(missingFreeze.error().message, {
                                                            "operation=freeze",
                                                            "type=com.asharia.smoke.DeferredOwner",
                                                            "field=deferred",
                                                            "expected=registered field type",
                                                            "actual=missing",
                                                        })) {
            logFailure("Reflection registry smoke did not reject missing field type at freeze.");
            return false;
        }

        auto valueRegistered =
            TypeBuilder<ReflectionSmokeDeferredValue>(registry, kSmokeDeferredValueTypeName)
                .kind(TypeKind::Struct)
                .field("value", &ReflectionSmokeDeferredValue::value, saved)
                .commit();
        if (!valueRegistered) {
            logFailure(valueRegistered.error().message);
            return false;
        }

        auto frozen = registry.freeze();
        if (!frozen) {
            logFailure(frozen.error().message);
            return false;
        }

        return true;
    }

    bool smokeRegistry() {
        if (!smokeDeferredFieldRegistration()) {
            return false;
        }

        asharia::reflection::TypeRegistry registry;
        auto registered = registerReflectionSmokeTypes(registry);
        if (!registered) {
            logFailure(registered.error().message);
            return false;
        }

        if (registry.findType(kSmokeTransformTypeName) == nullptr) {
            logFailure("Reflection registry smoke could not find the transform type.");
            return false;
        }

        asharia::reflection::TypeInfo duplicate{
            .id = asharia::reflection::makeTypeId(kSmokeTransformTypeName),
            .name = std::string{kSmokeTransformTypeName},
            .version = 1,
            .kind = asharia::reflection::TypeKind::Component,
            .fields = {},
        };
        auto duplicateRegistered = registry.registerType(std::move(duplicate));
        if (duplicateRegistered || !containsAll(duplicateRegistered.error().message,
                                                {
                                                    "operation=register",
                                                    "type=com.asharia.smoke.Transform",
                                                    "expected=unique type name",
                                                    "actual=duplicate",
                                                })) {
            logFailure("Reflection registry smoke accepted a duplicate type.");
            return false;
        }

        auto frozen = registry.freeze();
        if (!frozen) {
            logFailure(frozen.error().message);
            return false;
        }

        asharia::reflection::TypeInfo lateType{
            .id = asharia::reflection::makeTypeId("com.asharia.smoke.LateType"),
            .name = "com.asharia.smoke.LateType",
            .version = 1,
            .kind = asharia::reflection::TypeKind::Struct,
            .fields = {},
        };
        auto lateRegistered = registry.registerType(std::move(lateType));
        if (lateRegistered ||
            !containsAll(lateRegistered.error().message, {
                                                             "operation=register",
                                                             "type=com.asharia.smoke.LateType",
                                                             "expected=mutable registry",
                                                             "actual=frozen",
                                                         })) {
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

        const asharia::reflection::TypeInfo* transformType =
            registry->findType(kSmokeTransformTypeName);
        if (transformType == nullptr || transformType->fields.size() != 6) {
            logFailure("Reflection transform smoke saw an unexpected field count.");
            return false;
        }

        const asharia::reflection::FieldInfo* positionField = findField(*transformType, "position");
        const asharia::reflection::FieldInfo* cachedField =
            findField(*transformType, "cachedMagnitude");
        if (positionField == nullptr || cachedField == nullptr ||
            !positionField->flags.has(asharia::reflection::FieldFlag::Serializable) ||
            !positionField->flags.has(asharia::reflection::FieldFlag::EditorVisible) ||
            !positionField->flags.has(asharia::reflection::FieldFlag::RuntimeVisible) ||
            !positionField->flags.has(asharia::reflection::FieldFlag::ScriptVisible) ||
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

    bool smokePropertyAccessors() {
        auto registry = makeReflectionSmokeRegistry();
        if (!registry) {
            return false;
        }

        const asharia::reflection::TypeInfo* propertyType =
            registry->findType(kSmokePropertyComponentTypeName);
        if (propertyType == nullptr || propertyType->fields.size() != 2) {
            logFailure("Reflection property smoke saw an unexpected field count.");
            return false;
        }

        const asharia::reflection::FieldInfo* exposureField = findField(*propertyType, "exposure");
        const asharia::reflection::FieldInfo* derivedField =
            findField(*propertyType, "derivedCounter");
        if (exposureField == nullptr || derivedField == nullptr ||
            exposureField->accessor.readAddress || exposureField->accessor.writeAddress ||
            !exposureField->accessor.readValue || !exposureField->accessor.writeValue ||
            !derivedField->flags.has(asharia::reflection::FieldFlag::ReadOnly) ||
            derivedField->accessor.writeValue || derivedField->accessor.writeAddress ||
            !derivedField->accessor.readValue) {
            logFailure("Reflection property smoke saw unexpected accessor metadata.");
            return false;
        }

        ReflectionSmokePropertyComponent component;
        float exposure{};
        auto readExposure = exposureField->accessor.readValue(&component, &exposure);
        if (!readExposure || exposure != 1.5F) {
            logFailure("Reflection property smoke failed to read through getter.");
            return false;
        }

        const float updatedExposure = 2.25F;
        auto wroteExposure = exposureField->accessor.writeValue(&component, &updatedExposure);
        if (!wroteExposure || component.exposure() != updatedExposure || !component.isDirty()) {
            logFailure("Reflection property smoke failed to write through setter.");
            return false;
        }

        std::int32_t derivedCounter{};
        auto readDerived = derivedField->accessor.readValue(&component, &derivedCounter);
        if (!readDerived || derivedCounter != 41) {
            logFailure("Reflection property smoke failed to read readonly property.");
            return false;
        }

        std::cout << "Reflection property fields: " << propertyType->fields.size() << '\n';
        return true;
    }

    bool smokeContexts() {
        auto registry = makeReflectionSmokeRegistry();
        if (!registry) {
            return false;
        }

        const asharia::reflection::TypeInfo* transformType =
            registry->findType(kSmokeTransformTypeName);
        if (transformType == nullptr) {
            logFailure("Reflection context smoke could not find transform type.");
            return false;
        }

        const asharia::reflection::ContextFieldView serializeView =
            asharia::reflection::makeSerializeContextView(*transformType);
        const asharia::reflection::ContextFieldView editView =
            asharia::reflection::makeEditContextView(*transformType);
        const asharia::reflection::ContextFieldView scriptView =
            asharia::reflection::makeScriptContextView(*transformType);

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
    try {
        const bool passed =
            smokeRegistry() && smokeTransform() && smokePropertyAccessors() && smokeContexts();
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::fputs("Reflection smoke test threw an exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
    } catch (...) {
        std::fputs("Reflection smoke test threw an unknown exception.\n", stderr);
    }
    return EXIT_FAILURE;
}
