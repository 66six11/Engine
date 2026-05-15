#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "asharia/cpp_binding/cpp_binding_builder.hpp"

namespace {

    constexpr std::string_view kVec3TypeName = "com.asharia.cppBindingSmoke.Vec3";
    constexpr std::string_view kQuatTypeName = "com.asharia.cppBindingSmoke.Quat";
    constexpr std::string_view kComponentTypeName = "com.asharia.cppBindingSmoke.Component";

    struct SmokeVec3 {
        float x{};
        float y{};
        float z{};
    };

    struct SmokeQuat {
        float x{};
        float y{};
        float z{};
        float w{1.0F};
    };

    struct SmokeComponent {
        [[nodiscard]] float exposure() const noexcept {
            return exposure_;
        }

        asharia::VoidResult setExposure(const float& exposure) noexcept {
            exposure_ = exposure;
            dirty_ = true;
            return {};
        }

        [[nodiscard]] bool dirty() const noexcept {
            return dirty_;
        }

        float value{};
        std::int32_t mismatchedValue{};
        SmokeVec3 vectorValue;
        SmokeQuat mismatchedVectorValue;

    private:
        float exposure_{1.5F};
        bool dirty_{};
    };

} // namespace

namespace asharia::cpp_binding {

    template <> struct ReflectedType<SmokeVec3> {
        [[nodiscard]] static schema::TypeId typeId() {
            return schema::makeTypeId(kVec3TypeName);
        }
    };

    template <> struct ReflectedType<SmokeQuat> {
        [[nodiscard]] static schema::TypeId typeId() {
            return schema::makeTypeId(kQuatTypeName);
        }
    };

} // namespace asharia::cpp_binding

namespace {

    [[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
        return text.find(needle) != std::string_view::npos;
    }

    [[nodiscard]] asharia::schema::FieldSchema persistentFloat(std::uint32_t id,
                                                               std::string_view key) {
        return asharia::schema::FieldSchema{
            .id = asharia::schema::makeFieldId(id),
            .key = std::string{key},
            .valueType = asharia::schema::builtin::floatTypeId(),
            .valueKind = asharia::schema::ValueKind::Float,
            .metadata = {.persistence = {.stored = true}},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeVec3Schema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kVec3TypeName),
            .canonicalName = std::string{kVec3TypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::InlineStruct,
            .fields = {persistentFloat(1, "x"), persistentFloat(2, "y"), persistentFloat(3, "z")},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeQuatSchema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kQuatTypeName),
            .canonicalName = std::string{kQuatTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::InlineStruct,
            .fields = {persistentFloat(1, "x"), persistentFloat(2, "y"), persistentFloat(3, "z"),
                       persistentFloat(4, "w")},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeComponentSchema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kComponentTypeName),
            .canonicalName = std::string{kComponentTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::Object,
            .fields =
                {
                    asharia::schema::FieldSchema{
                        .id = asharia::schema::makeFieldId(1),
                        .key = "value",
                        .valueType = asharia::schema::builtin::floatTypeId(),
                        .valueKind = asharia::schema::ValueKind::Float,
                        .metadata = {.persistence = {.stored = true}},
                    },
                    asharia::schema::FieldSchema{
                        .id = asharia::schema::makeFieldId(2),
                        .key = "exposure",
                        .valueType = asharia::schema::builtin::floatTypeId(),
                        .valueKind = asharia::schema::ValueKind::Float,
                        .metadata = {.persistence = {.stored = true}},
                    },
                    asharia::schema::FieldSchema{
                        .id = asharia::schema::makeFieldId(3),
                        .key = "vectorValue",
                        .valueType = asharia::schema::makeTypeId(kVec3TypeName),
                        .valueKind = asharia::schema::ValueKind::InlineStruct,
                        .metadata = {.persistence = {.stored = true}},
                    },
                },
        };
    }

} // namespace

int main() {
    asharia::schema::SchemaRegistry schemas;
    if (auto registered = asharia::schema::registerBuiltinSchemas(schemas); !registered) {
        std::cerr << registered.error().message << '\n';
        return EXIT_FAILURE;
    }
    if (auto registered = schemas.registerType(makeVec3Schema()); !registered) {
        std::cerr << registered.error().message << '\n';
        return EXIT_FAILURE;
    }
    if (auto registered = schemas.registerType(makeQuatSchema()); !registered) {
        std::cerr << registered.error().message << '\n';
        return EXIT_FAILURE;
    }
    if (auto registered = schemas.registerType(makeComponentSchema()); !registered) {
        std::cerr << registered.error().message << '\n';
        return EXIT_FAILURE;
    }
    if (auto frozen = schemas.freeze(); !frozen) {
        std::cerr << frozen.error().message << '\n';
        return EXIT_FAILURE;
    }

    asharia::cpp_binding::BindingRegistry bindings{schemas};
    auto mismatchedField =
        asharia::cpp_binding::CppBindingBuilder<SmokeComponent>(
            bindings, asharia::schema::makeTypeId(kComponentTypeName), "SmokeComponent")
            .field(asharia::schema::makeFieldId(1), "mismatchedValue",
                   &SmokeComponent::mismatchedValue)
            .commit();
    if (mismatchedField || !contains(mismatchedField.error().message, "value type")) {
        std::cerr << "C++ binding registry accepted a member type mismatch.\n";
        return EXIT_FAILURE;
    }

    auto mismatchedCustomField =
        asharia::cpp_binding::CppBindingBuilder<SmokeComponent>(
            bindings, asharia::schema::makeTypeId(kComponentTypeName), "SmokeComponent")
            .field(asharia::schema::makeFieldId(3), "mismatchedVectorValue",
                   &SmokeComponent::mismatchedVectorValue)
            .commit();
    if (mismatchedCustomField || !contains(mismatchedCustomField.error().message, "value type")) {
        std::cerr << "C++ binding registry accepted a custom member type mismatch.\n";
        return EXIT_FAILURE;
    }

    auto mismatchedDefault =
        asharia::cpp_binding::CppBindingBuilder<SmokeComponent>(
            bindings, asharia::schema::makeTypeId(kComponentTypeName), "SmokeComponent")
            .field(asharia::schema::makeFieldId(1), "value", &SmokeComponent::value)
            .defaultValue(std::int32_t{7})
            .commit();
    if (mismatchedDefault || !contains(mismatchedDefault.error().message, "default value type")) {
        std::cerr << "C++ binding registry accepted a default value type mismatch.\n";
        return EXIT_FAILURE;
    }

    auto mismatchedCustomDefault =
        asharia::cpp_binding::CppBindingBuilder<SmokeComponent>(
            bindings, asharia::schema::makeTypeId(kComponentTypeName), "SmokeComponent")
            .field(asharia::schema::makeFieldId(3), "vectorValue", &SmokeComponent::vectorValue)
            .defaultValue(SmokeQuat{})
            .commit();
    if (mismatchedCustomDefault ||
        !contains(mismatchedCustomDefault.error().message, "default value type")) {
        std::cerr << "C++ binding registry accepted a custom default value type mismatch.\n";
        return EXIT_FAILURE;
    }

    auto componentBinding =
        asharia::cpp_binding::CppBindingBuilder<SmokeComponent>(
            bindings, asharia::schema::makeTypeId(kComponentTypeName), "SmokeComponent")
            .field(asharia::schema::makeFieldId(1), "value", &SmokeComponent::value)
            .defaultValue(7.0F)
            .property<float>(
                asharia::schema::makeFieldId(2), "exposure",
                [](const SmokeComponent& component) { return component.exposure(); },
                [](SmokeComponent& component, const float& exposure) {
                    return component.setExposure(exposure);
                })
            .field(asharia::schema::makeFieldId(3), "vectorValue", &SmokeComponent::vectorValue)
            .defaultValue(SmokeVec3{.x = 1.0F, .y = 2.0F, .z = 3.0F})
            .commit();
    if (!componentBinding) {
        std::cerr << componentBinding.error().message << '\n';
        return EXIT_FAILURE;
    }

    auto duplicate =
        asharia::cpp_binding::CppBindingBuilder<SmokeComponent>(
            bindings, asharia::schema::makeTypeId(kComponentTypeName), "SmokeComponent")
            .field(asharia::schema::makeFieldId(1), "value", &SmokeComponent::value)
            .commit();
    if (duplicate || !contains(duplicate.error().message, "duplicate")) {
        std::cerr << "C++ binding registry accepted a duplicate type binding.\n";
        return EXIT_FAILURE;
    }

    if (auto frozen = bindings.freeze(); !frozen) {
        std::cerr << frozen.error().message << '\n';
        return EXIT_FAILURE;
    }

    SmokeComponent component;
    const asharia::cpp_binding::CppTypeBinding* binding =
        bindings.findBinding(asharia::schema::makeTypeId(kComponentTypeName));
    if (binding == nullptr) {
        std::cerr << "C++ binding registry could not find component binding.\n";
        return EXIT_FAILURE;
    }

    const asharia::cpp_binding::FieldBinding* valueField =
        asharia::cpp_binding::findFieldBinding(*binding, asharia::schema::makeFieldId(1));
    const asharia::cpp_binding::FieldBinding* exposureField =
        asharia::cpp_binding::findFieldBinding(*binding, asharia::schema::makeFieldId(2));
    const asharia::cpp_binding::FieldBinding* vectorField =
        asharia::cpp_binding::findFieldBinding(*binding, asharia::schema::makeFieldId(3));
    if (valueField == nullptr || exposureField == nullptr || !valueField->writeAddress ||
        vectorField == nullptr || !exposureField->readValue || !exposureField->writeValue ||
        !valueField->writeDefaultValue || !vectorField->writeDefaultValue) {
        std::cerr << "C++ binding fields did not expose expected thunks.\n";
        return EXIT_FAILURE;
    }

    auto* value = static_cast<float*>(valueField->writeAddress(&component));
    if (value == nullptr) {
        std::cerr << "C++ binding could not write member field.\n";
        return EXIT_FAILURE;
    }
    *value = 3.0F;
    if (component.value != 3.0F) {
        std::cerr << "C++ binding member field write did not update object.\n";
        return EXIT_FAILURE;
    }

    float exposure{};
    if (auto read = exposureField->readValue(&component, &exposure); !read || exposure != 1.5F) {
        std::cerr << "C++ binding property getter failed.\n";
        return EXIT_FAILURE;
    }
    const float newExposure = 2.25F;
    if (auto wrote = exposureField->writeValue(&component, &newExposure);
        !wrote || component.exposure() != newExposure || !component.dirty()) {
        std::cerr << "C++ binding property setter failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "C++ binding fields: " << binding->fields.size() << '\n';
    return EXIT_SUCCESS;
}
