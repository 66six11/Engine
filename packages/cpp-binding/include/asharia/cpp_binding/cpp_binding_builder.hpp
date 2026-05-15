#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "asharia/cpp_binding/binding_registry.hpp"

namespace asharia::cpp_binding {

    template <typename T> [[nodiscard]] schema::TypeId reflectedTypeId() {
        using ValueT = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<ValueT, bool>) {
            return schema::builtin::boolTypeId();
        } else if constexpr (std::is_same_v<ValueT, std::int32_t>) {
            return schema::builtin::int32TypeId();
        } else if constexpr (std::is_same_v<ValueT, std::uint32_t>) {
            return schema::builtin::uint32TypeId();
        } else if constexpr (std::is_same_v<ValueT, std::uint64_t>) {
            return schema::builtin::uint64TypeId();
        } else if constexpr (std::is_same_v<ValueT, float>) {
            return schema::builtin::floatTypeId();
        } else if constexpr (std::is_same_v<ValueT, double>) {
            return schema::builtin::doubleTypeId();
        } else if constexpr (std::is_same_v<ValueT, std::string>) {
            return schema::builtin::stringTypeId();
        } else {
            return {};
        }
    }

    template <typename ObjectT> class CppBindingBuilder {
    public:
        CppBindingBuilder(BindingRegistry& registry, schema::TypeId schemaType,
                          std::string_view cppTypeName)
            : registry_{registry} {
            binding_.schemaType = std::move(schemaType);
            binding_.cppTypeName = std::string{cppTypeName};
        }

        template <typename FieldT>
        CppBindingBuilder& field(schema::FieldId fieldId, std::string_view cppMemberName,
                                 FieldT ObjectT::* member) {
            return addMemberField(fieldId, cppMemberName, member, true);
        }

        template <typename FieldT>
        CppBindingBuilder& readonlyField(schema::FieldId fieldId, std::string_view cppMemberName,
                                         FieldT ObjectT::* member) {
            return addMemberField(fieldId, cppMemberName, member, false);
        }

        template <typename FieldT, typename GetterT, typename SetterT>
        CppBindingBuilder& property(schema::FieldId fieldId, std::string_view cppMemberName,
                                    GetterT getter, SetterT setter) {
            using ValueT = std::remove_cvref_t<FieldT>;
            static_assert(std::is_invocable_r_v<ValueT, GetterT, const ObjectT&>,
                          "Property getter must be invocable as FieldT(const ObjectT&).");
            static_assert(
                std::is_invocable_r_v<VoidResult, SetterT, ObjectT&, const ValueT&>,
                "Property setter must be invocable as VoidResult(ObjectT&, const FieldT&).");

            FieldBinding field = makeValueBinding<ValueT>(fieldId, cppMemberName);
            field.readValue = [getter = std::move(getter)](const void* object,
                                                           void* value) -> VoidResult {
                if (object == nullptr || value == nullptr) {
                    return nullBindingArgument("read C++ property");
                }
                const auto* typedObject = static_cast<const ObjectT*>(object);
                *static_cast<ValueT*>(value) = std::invoke(getter, *typedObject);
                return {};
            };
            field.writeValue = [setter = std::move(setter)](void* object,
                                                            const void* value) -> VoidResult {
                if (object == nullptr || value == nullptr) {
                    return nullBindingArgument("write C++ property");
                }
                auto* typedObject = static_cast<ObjectT*>(object);
                return std::invoke(setter, *typedObject, *static_cast<const ValueT*>(value));
            };
            binding_.fields.push_back(std::move(field));
            return *this;
        }

        template <typename FieldT, typename GetterT>
        CppBindingBuilder& readonlyProperty(schema::FieldId fieldId, std::string_view cppMemberName,
                                            GetterT getter) {
            using ValueT = std::remove_cvref_t<FieldT>;
            static_assert(std::is_invocable_r_v<ValueT, GetterT, const ObjectT&>,
                          "Readonly property getter must be invocable as FieldT(const ObjectT&).");

            FieldBinding field = makeValueBinding<ValueT>(fieldId, cppMemberName);
            field.readValue = [getter = std::move(getter)](const void* object,
                                                           void* value) -> VoidResult {
                if (object == nullptr || value == nullptr) {
                    return nullBindingArgument("read C++ readonly property");
                }
                const auto* typedObject = static_cast<const ObjectT*>(object);
                *static_cast<ValueT*>(value) = std::invoke(getter, *typedObject);
                return {};
            };
            binding_.fields.push_back(std::move(field));
            return *this;
        }

        template <typename FieldT> CppBindingBuilder& defaultValue(FieldT value) {
            using ValueT = std::remove_cvref_t<FieldT>;
            static_assert(std::is_copy_assignable_v<ValueT>,
                          "C++ binding default values must be copy assignable.");

            if (!binding_.fields.empty()) {
                schema::TypeId defaultValueType = reflectedTypeId<ValueT>();
                if (!defaultValueType) {
                    defaultValueType = binding_.fields.back().valueType;
                }
                binding_.fields.back().defaultValueType = std::move(defaultValueType);
                binding_.fields.back().writeDefaultValue =
                    [defaultValue = ValueT{std::move(value)}](void* fieldValue) -> VoidResult {
                    if (fieldValue == nullptr) {
                        return nullBindingArgument("write C++ field default value");
                    }
                    *static_cast<ValueT*>(fieldValue) = defaultValue;
                    return {};
                };
            }
            return *this;
        }

        [[nodiscard]] VoidResult commit() {
            return registry_.registerType(std::move(binding_));
        }

    private:
        template <typename FieldT>
        CppBindingBuilder& addMemberField(schema::FieldId fieldId, std::string_view cppMemberName,
                                          FieldT ObjectT::* member, bool writable) {
            using ValueT = std::remove_cvref_t<FieldT>;
            FieldBinding field = makeBindingHeader<ValueT>(fieldId, cppMemberName);
            field.readAddress = [member](const void* object) -> const void* {
                if (object == nullptr) {
                    return nullptr;
                }
                const auto* typedObject = static_cast<const ObjectT*>(object);
                return &(typedObject->*member);
            };
            if (writable) {
                field.writeAddress = [member](void* object) -> void* {
                    if (object == nullptr) {
                        return nullptr;
                    }
                    auto* typedObject = static_cast<ObjectT*>(object);
                    return &(typedObject->*member);
                };
            }
            binding_.fields.push_back(std::move(field));
            return *this;
        }

        [[nodiscard]] static VoidResult nullBindingArgument(std::string_view operation) {
            return std::unexpected{
                bindingError("Cannot " + std::string{operation} + " with a null argument.")};
        }

        template <typename FieldT>
        [[nodiscard]] FieldBinding makeBindingHeader(schema::FieldId fieldId,
                                                     std::string_view cppMemberName) const {
            using ValueT = std::remove_cvref_t<FieldT>;
            schema::TypeId valueType = reflectedTypeId<ValueT>();
            if (!valueType) {
                valueType = schemaValueType(fieldId);
            }
            return FieldBinding{
                .ownerSchema = binding_.schemaType,
                .fieldId = fieldId,
                .cppMemberName = std::string{cppMemberName},
                .valueType = std::move(valueType),
                .defaultValueType = {},
                .size = sizeof(ValueT),
                .alignment = alignof(ValueT),
            };
        }

        template <typename FieldT>
        [[nodiscard]] FieldBinding makeValueBinding(schema::FieldId fieldId,
                                                    std::string_view cppMemberName) const {
            using ValueT = std::remove_cvref_t<FieldT>;
            static_assert(std::is_default_constructible_v<ValueT>,
                          "C++ property binding values must be default constructible.");
            static_assert(std::is_copy_constructible_v<ValueT>,
                          "C++ property binding values must be copy constructible.");
            static_assert(std::is_copy_assignable_v<ValueT>,
                          "C++ property binding values must be copy assignable.");
            static_assert(std::is_destructible_v<ValueT>,
                          "C++ property binding values must be destructible.");

            FieldBinding field = makeBindingHeader<ValueT>(fieldId, cppMemberName);
            field.constructValue = [](void* value) -> VoidResult {
                if (value == nullptr) {
                    return nullBindingArgument("construct C++ field value");
                }
                std::construct_at(static_cast<ValueT*>(value));
                return {};
            };
            field.destroyValue = [](void* value) {
                if (value != nullptr) {
                    std::destroy_at(static_cast<ValueT*>(value));
                }
            };
            return field;
        }

        [[nodiscard]] schema::TypeId schemaValueType(schema::FieldId fieldId) const {
            const schema::TypeSchema* schemaType =
                registry_.schemaRegistry().findType(binding_.schemaType);
            const schema::FieldSchema* schemaField =
                schemaType == nullptr ? nullptr : schema::findFieldById(*schemaType, fieldId);
            return schemaField == nullptr ? schema::TypeId{} : schemaField->valueType;
        }

        BindingRegistry& registry_;
        CppTypeBinding binding_;
    };

} // namespace asharia::cpp_binding
