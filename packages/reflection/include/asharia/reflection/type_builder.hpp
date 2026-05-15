#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "asharia/reflection/type_registry.hpp"

namespace asharia::reflection {

    template <typename T> [[nodiscard]] constexpr TypeId reflectedTypeId() noexcept {
        using ValueT = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<ValueT, bool>) {
            return builtin::boolTypeId();
        } else if constexpr (std::is_same_v<ValueT, std::int32_t>) {
            return builtin::int32TypeId();
        } else if constexpr (std::is_same_v<ValueT, std::uint32_t>) {
            return builtin::uint32TypeId();
        } else if constexpr (std::is_same_v<ValueT, std::uint64_t>) {
            return builtin::uint64TypeId();
        } else if constexpr (std::is_same_v<ValueT, float>) {
            return builtin::floatTypeId();
        } else if constexpr (std::is_same_v<ValueT, double>) {
            return builtin::doubleTypeId();
        } else if constexpr (std::is_same_v<ValueT, std::string>) {
            return builtin::stringTypeId();
        } else {
            return TypeId{};
        }
    }

    template <typename ObjectT> class TypeBuilder {
    public:
        TypeBuilder(TypeRegistry& registry, std::string_view typeName) : registry_{registry} {
            type_.id = makeTypeId(typeName);
            type_.name = std::string{typeName};
        }

        TypeBuilder& version(std::uint32_t version) noexcept {
            type_.version = version;
            return *this;
        }

        TypeBuilder& kind(TypeKind kind) noexcept {
            type_.kind = kind;
            return *this;
        }

        TypeBuilder& attribute(FieldAttribute attribute) {
            type_.attributes.push_back(std::move(attribute));
            return *this;
        }

        TypeBuilder& attributes(AttributeSet attributes) {
            type_.attributes = std::move(attributes);
            return *this;
        }

        template <typename FieldT> TypeBuilder& defaultValue(FieldT value) {
            using ValueT = std::remove_cvref_t<FieldT>;
            static_assert(std::is_copy_assignable_v<ValueT>,
                          "Reflected field default values must be copy assignable.");

            if (!type_.fields.empty()) {
                TypeId valueType = reflectedTypeId<ValueT>();
                if (!valueType) {
                    valueType = type_.fields.back().type;
                }
                type_.fields.back().defaultProvider = FieldDefaultProvider{
                    .valueType = valueType,
                    .writeValue = [defaultValue =
                                       ValueT{std::move(value)}](void* fieldValue) -> VoidResult {
                        if (fieldValue == nullptr) {
                            return nullAccessorArgument("write reflected field default value");
                        }
                        *static_cast<ValueT*>(fieldValue) = defaultValue;
                        return {};
                    },
                };
            }
            return *this;
        }

        template <typename FieldT, typename ValidatorT>
        TypeBuilder& validator(ValidatorT validate) {
            using ValueT = std::remove_cvref_t<FieldT>;
            static_assert(std::is_invocable_r_v<VoidResult, ValidatorT, const ValueT&>,
                          "Reflected field validators must be invocable as "
                          "VoidResult(const FieldT&).");

            if (!type_.fields.empty()) {
                TypeId valueType = reflectedTypeId<ValueT>();
                if (!valueType) {
                    valueType = type_.fields.back().type;
                }
                type_.fields.back().validator = FieldValidator{
                    .valueType = valueType,
                    .validateValue =
                        [validate = std::move(validate)](const void* fieldValue) -> VoidResult {
                        if (fieldValue == nullptr) {
                            return nullAccessorArgument("validate reflected field value");
                        }
                        return std::invoke(validate, *static_cast<const ValueT*>(fieldValue));
                    },
                };
            }
            return *this;
        }

        template <typename FieldT>
        TypeBuilder& field(std::string_view name, FieldT ObjectT::* member,
                           AttributeSet attributes = {}) {
            return field(name, member, reflectedTypeId<FieldT>(), std::move(attributes));
        }

        template <typename FieldT>
        TypeBuilder& field(std::string_view name, FieldT ObjectT::* member, TypeId fieldType,
                           AttributeSet attributes = {}) {
            return addMemberField(name, member, fieldType, true, std::move(attributes));
        }

        template <typename FieldT>
        TypeBuilder& readonlyField(std::string_view name, FieldT ObjectT::* member,
                                   AttributeSet attributes = {}) {
            return readonlyField(name, member, reflectedTypeId<FieldT>(), std::move(attributes));
        }

        template <typename FieldT>
        TypeBuilder& readonlyField(std::string_view name, FieldT ObjectT::* member,
                                   TypeId fieldType, AttributeSet attributes = {}) {
            return addMemberField(name, member, fieldType, false, std::move(attributes));
        }

        template <typename FieldT, typename GetterT, typename SetterT>
        TypeBuilder& property(std::string_view name, GetterT getter, SetterT setter,
                              AttributeSet attributes = {}) {
            return property<FieldT>(name, reflectedTypeId<FieldT>(), std::move(getter),
                                    std::move(setter), std::move(attributes));
        }

        template <typename FieldT, typename GetterT, typename SetterT>
        TypeBuilder& property(std::string_view name, TypeId fieldType, GetterT getter,
                              SetterT setter, AttributeSet attributes = {}) {
            using ValueT = std::remove_cvref_t<FieldT>;
            static_assert(std::is_invocable_r_v<ValueT, GetterT, const ObjectT&>,
                          "Property getter must be invocable as FieldT(const ObjectT&).");
            static_assert(
                std::is_invocable_r_v<VoidResult, SetterT, ObjectT&, const ValueT&>,
                "Property setter must be invocable as VoidResult(ObjectT&, const FieldT&).");

            FieldAccessor accessor = makeValueAccessor<ValueT>(type_.id, fieldType);
            accessor.readValue = [getter = std::move(getter)](const void* object,
                                                              void* value) -> VoidResult {
                if (object == nullptr || value == nullptr) {
                    return nullAccessorArgument("read reflected property");
                }
                const auto* typedObject = static_cast<const ObjectT*>(object);
                *static_cast<ValueT*>(value) = std::invoke(getter, *typedObject);
                return {};
            };

            accessor.writeValue = [setter = std::move(setter)](void* object,
                                                               const void* value) -> VoidResult {
                if (object == nullptr || value == nullptr) {
                    return nullAccessorArgument("write reflected property");
                }
                auto* typedObject = static_cast<ObjectT*>(object);
                return std::invoke(setter, *typedObject, *static_cast<const ValueT*>(value));
            };

            addField(name, fieldType, std::move(attributes), std::move(accessor));
            return *this;
        }

        template <typename FieldT, typename GetterT>
        TypeBuilder& readonlyProperty(std::string_view name, GetterT getter,
                                      AttributeSet attributes = {}) {
            return readonlyProperty<FieldT>(name, reflectedTypeId<FieldT>(), std::move(getter),
                                            std::move(attributes));
        }

        template <typename FieldT, typename GetterT>
        TypeBuilder& readonlyProperty(std::string_view name, TypeId fieldType, GetterT getter,
                                      AttributeSet attributes = {}) {
            using ValueT = std::remove_cvref_t<FieldT>;
            static_assert(std::is_invocable_r_v<ValueT, GetterT, const ObjectT&>,
                          "Readonly property getter must be invocable as FieldT(const ObjectT&).");

            FieldAccessor accessor = makeValueAccessor<ValueT>(type_.id, fieldType);
            accessor.readValue = [getter = std::move(getter)](const void* object,
                                                              void* value) -> VoidResult {
                if (object == nullptr || value == nullptr) {
                    return nullAccessorArgument("read reflected readonly property");
                }
                const auto* typedObject = static_cast<const ObjectT*>(object);
                *static_cast<ValueT*>(value) = std::invoke(getter, *typedObject);
                return {};
            };

            addField(name, fieldType, std::move(attributes), std::move(accessor));
            return *this;
        }

        [[nodiscard]] VoidResult commit() {
            return registry_.registerType(std::move(type_));
        }

    private:
        template <typename FieldT>
        TypeBuilder& addMemberField(std::string_view name, FieldT ObjectT::* member,
                                    TypeId fieldType, bool writable, AttributeSet attributes) {
            using ValueT = std::remove_cvref_t<FieldT>;
            FieldAccessor accessor = makeAccessorHeader<ValueT>(type_.id, fieldType);
            accessor.readAddress = [member](const void* object) -> const void* {
                if (object == nullptr) {
                    return nullptr;
                }
                const auto* typedObject = static_cast<const ObjectT*>(object);
                return &(typedObject->*member);
            };

            if (writable) {
                accessor.writeAddress = [member](void* object) -> void* {
                    if (object == nullptr) {
                        return nullptr;
                    }
                    auto* typedObject = static_cast<ObjectT*>(object);
                    return &(typedObject->*member);
                };
            }

            addField(name, fieldType, std::move(attributes), std::move(accessor));
            return *this;
        }

        [[nodiscard]] static VoidResult nullAccessorArgument(std::string_view operation) {
            return std::unexpected{
                reflectionError("Cannot " + std::string{operation} + " with a null argument.")};
        }

        template <typename FieldT>
        [[nodiscard]] static FieldAccessor makeAccessorHeader(TypeId ownerType, TypeId fieldType) {
            using ValueT = std::remove_cvref_t<FieldT>;
            return FieldAccessor{
                .ownerType = ownerType,
                .fieldType = fieldType,
                .size = sizeof(ValueT),
                .alignment = alignof(ValueT),
                .readAddress = {},
                .writeAddress = {},
                .constructValue = {},
                .destroyValue = {},
                .readValue = {},
                .writeValue = {},
            };
        }

        template <typename FieldT>
        [[nodiscard]] static FieldAccessor makeValueAccessor(TypeId ownerType, TypeId fieldType) {
            using ValueT = std::remove_cvref_t<FieldT>;
            static_assert(std::is_default_constructible_v<ValueT>,
                          "Reflected property fields must be default constructible.");
            static_assert(std::is_copy_constructible_v<ValueT>,
                          "Reflected property fields must be copy constructible.");
            static_assert(std::is_copy_assignable_v<ValueT>,
                          "Reflected property fields must be copy assignable.");
            static_assert(std::is_destructible_v<ValueT>,
                          "Reflected property fields must be destructible.");

            FieldAccessor accessor = makeAccessorHeader<ValueT>(ownerType, fieldType);
            accessor.constructValue = [](void* value) -> VoidResult {
                if (value == nullptr) {
                    return nullAccessorArgument("construct reflected field value");
                }
                std::construct_at(static_cast<ValueT*>(value));
                return {};
            };
            accessor.destroyValue = [](void* value) {
                if (value != nullptr) {
                    std::destroy_at(static_cast<ValueT*>(value));
                }
            };
            return accessor;
        }

        void addField(std::string_view name, TypeId fieldType, AttributeSet attributes,
                      FieldAccessor accessor) {
            type_.fields.push_back(FieldInfo{
                .id = makeFieldId(type_.name, name),
                .name = std::string{name},
                .type = fieldType,
                .attributes = std::move(attributes),
                .accessor = std::move(accessor),
                .defaultProvider = {},
                .validator = {},
            });
        }

        TypeRegistry& registry_;
        TypeInfo type_;
    };

} // namespace asharia::reflection
