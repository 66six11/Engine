#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "asharia/reflection/type_registry.hpp"

namespace asharia::reflection {

    template <typename T>
    [[nodiscard]] constexpr TypeId reflectedTypeId() noexcept {
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

    template <typename ObjectT>
    class TypeBuilder {
    public:
        TypeBuilder(TypeRegistry& registry, std::string_view typeName)
            : registry_{registry} {
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
        TypeBuilder& field(std::string_view name, FieldT ObjectT::*member, FieldFlagSet flags,
                           FieldAttributes attributes = {}) {
            return field(name, member, reflectedTypeId<FieldT>(), flags, std::move(attributes));
        }

        template <typename FieldT>
        TypeBuilder& field(std::string_view name, FieldT ObjectT::*member, TypeId fieldType,
                           FieldFlagSet flags, FieldAttributes attributes = {}) {
            FieldAccessor accessor{
                .ownerType = type_.id,
                .fieldType = fieldType,
                .size = sizeof(FieldT),
                .alignment = alignof(FieldT),
                .readAddress =
                    [member](const void* object) -> const void* {
                    if (object == nullptr) {
                        return nullptr;
                    }
                    const auto* typedObject = static_cast<const ObjectT*>(object);
                    return &(typedObject->*member);
                },
                .writeAddress = {},
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

            type_.fields.push_back(FieldInfo{
                .id = makeFieldId(type_.name, name),
                .name = std::string{name},
                .type = fieldType,
                .flags = flags,
                .attributes = std::move(attributes),
                .accessor = std::move(accessor),
            });
            return *this;
        }

        [[nodiscard]] VoidResult commit() {
            return registry_.registerType(std::move(type_));
        }

    private:
        TypeRegistry& registry_;
        TypeInfo type_;
    };

} // namespace asharia::reflection
