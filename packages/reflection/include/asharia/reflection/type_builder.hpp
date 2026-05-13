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

        template <typename FieldT>
        TypeBuilder& field(std::string_view name, FieldT ObjectT::* member, FieldFlagSet flags,
                           FieldAttributes attributes = {}) {
            return field(name, member, reflectedTypeId<FieldT>(), flags, std::move(attributes));
        }

        template <typename FieldT>
        TypeBuilder& field(std::string_view name, FieldT ObjectT::* member, TypeId fieldType,
                           FieldFlagSet flags, FieldAttributes attributes = {}) {
            using ValueT = std::remove_cvref_t<FieldT>;

            FieldAccessor accessor = makeAccessorHeader<ValueT>(type_.id, fieldType);
            accessor.readAddress = [member](const void* object) -> const void* {
                if (object == nullptr) {
                    return nullptr;
                }
                const auto* typedObject = static_cast<const ObjectT*>(object);
                return &(typedObject->*member);
            };

            if (!flags.has(FieldFlag::ReadOnly)) {
                accessor.writeAddress = [member](void* object) -> void* {
                    if (object == nullptr) {
                        return nullptr;
                    }
                    auto* typedObject = static_cast<ObjectT*>(object);
                    return &(typedObject->*member);
                };
            }

            addField(name, fieldType, flags, std::move(attributes), std::move(accessor));
            return *this;
        }

        template <typename FieldT, typename GetterT, typename SetterT>
        TypeBuilder& property(std::string_view name, GetterT getter, SetterT setter,
                              FieldFlagSet flags, FieldAttributes attributes = {}) {
            return property<FieldT>(name, reflectedTypeId<FieldT>(), std::move(getter),
                                    std::move(setter), flags, std::move(attributes));
        }

        template <typename FieldT, typename GetterT, typename SetterT>
        TypeBuilder& property(std::string_view name, TypeId fieldType, GetterT getter,
                              SetterT setter, FieldFlagSet flags, FieldAttributes attributes = {}) {
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

            if (!flags.has(FieldFlag::ReadOnly)) {
                accessor.writeValue =
                    [setter = std::move(setter)](void* object, const void* value) -> VoidResult {
                    if (object == nullptr || value == nullptr) {
                        return nullAccessorArgument("write reflected property");
                    }
                    auto* typedObject = static_cast<ObjectT*>(object);
                    return std::invoke(setter, *typedObject, *static_cast<const ValueT*>(value));
                };
            }

            addField(name, fieldType, flags, std::move(attributes), std::move(accessor));
            return *this;
        }

        template <typename FieldT, typename GetterT>
        TypeBuilder& readonlyProperty(std::string_view name, GetterT getter, FieldFlagSet flags,
                                      FieldAttributes attributes = {}) {
            return readonlyProperty<FieldT>(name, reflectedTypeId<FieldT>(), std::move(getter),
                                            flags, std::move(attributes));
        }

        template <typename FieldT, typename GetterT>
        TypeBuilder& readonlyProperty(std::string_view name, TypeId fieldType, GetterT getter,
                                      FieldFlagSet flags, FieldAttributes attributes = {}) {
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

            flags.add(FieldFlag::ReadOnly);
            addField(name, fieldType, flags, std::move(attributes), std::move(accessor));
            return *this;
        }

        [[nodiscard]] VoidResult commit() {
            return registry_.registerType(std::move(type_));
        }

    private:
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

        void addField(std::string_view name, TypeId fieldType, FieldFlagSet flags,
                      FieldAttributes attributes, FieldAccessor accessor) {
            type_.fields.push_back(FieldInfo{
                .id = makeFieldId(type_.name, name),
                .name = std::string{name},
                .type = fieldType,
                .flags = flags,
                .attributes = std::move(attributes),
                .accessor = std::move(accessor),
            });
        }

        TypeRegistry& registry_;
        TypeInfo type_;
    };

} // namespace asharia::reflection
