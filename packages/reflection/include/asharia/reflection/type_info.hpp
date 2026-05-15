#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/reflection/attributes.hpp"
#include "asharia/reflection/ids.hpp"

namespace asharia::reflection {

    enum class TypeKind : std::uint8_t {
        Scalar,
        String,
        Enum,
        Struct,
        Component,
        ResourceConfig,
        AssetHandle,
        EntityHandle,
        Array,
        Vector,
    };

    enum class FieldCapability : std::uint32_t {
        ReadAddress = 1U << 0U,
        WriteAddress = 1U << 1U,
        ReadValue = 1U << 2U,
        WriteValue = 1U << 3U,
        ConstructValue = 1U << 4U,
        DestroyValue = 1U << 5U,
        DefaultValue = 1U << 6U,
        ValidateValue = 1U << 7U,
    };

    using ReadFieldAddressFn = std::function<const void*(const void*)>;
    using WriteFieldAddressFn = std::function<void*(void*)>;
    using ConstructFieldValueFn = std::function<VoidResult(void*)>;
    using DestroyFieldValueFn = std::function<void(void*)>;
    using ReadFieldValueFn = std::function<VoidResult(const void*, void*)>;
    using WriteFieldValueFn = std::function<VoidResult(void*, const void*)>;
    using WriteDefaultFieldValueFn = std::function<VoidResult(void*)>;
    using ValidateFieldValueFn = std::function<VoidResult(const void*)>;

    struct FieldAccessor {
        TypeId ownerType{};
        TypeId fieldType{};
        std::size_t size{};
        std::size_t alignment{};
        ReadFieldAddressFn readAddress;
        WriteFieldAddressFn writeAddress;
        ConstructFieldValueFn constructValue;
        DestroyFieldValueFn destroyValue;
        ReadFieldValueFn readValue;
        WriteFieldValueFn writeValue;
    };

    struct FieldDefaultProvider {
        TypeId valueType{};
        WriteDefaultFieldValueFn writeValue;

        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(writeValue);
        }
    };

    struct FieldValidator {
        TypeId valueType{};
        ValidateFieldValueFn validateValue;

        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(validateValue);
        }
    };

    struct FieldInfo {
        FieldId id{};
        std::string name;
        TypeId type{};
        AttributeSet attributes;
        FieldAccessor accessor;
        FieldDefaultProvider defaultProvider;
        FieldValidator validator;
    };

    [[nodiscard]] inline std::uint32_t fieldCapabilities(const FieldInfo& field) {
        std::uint32_t capabilities{};
        if (field.accessor.readAddress) {
            capabilities |= static_cast<std::uint32_t>(FieldCapability::ReadAddress);
        }
        if (field.accessor.writeAddress) {
            capabilities |= static_cast<std::uint32_t>(FieldCapability::WriteAddress);
        }
        if (field.accessor.readValue) {
            capabilities |= static_cast<std::uint32_t>(FieldCapability::ReadValue);
        }
        if (field.accessor.writeValue) {
            capabilities |= static_cast<std::uint32_t>(FieldCapability::WriteValue);
        }
        if (field.accessor.constructValue) {
            capabilities |= static_cast<std::uint32_t>(FieldCapability::ConstructValue);
        }
        if (field.accessor.destroyValue) {
            capabilities |= static_cast<std::uint32_t>(FieldCapability::DestroyValue);
        }
        if (field.defaultProvider) {
            capabilities |= static_cast<std::uint32_t>(FieldCapability::DefaultValue);
        }
        if (field.validator) {
            capabilities |= static_cast<std::uint32_t>(FieldCapability::ValidateValue);
        }
        return capabilities;
    }

    [[nodiscard]] inline bool hasFieldCapability(const FieldInfo& field,
                                                 FieldCapability capability) {
        return (fieldCapabilities(field) & static_cast<std::uint32_t>(capability)) != 0U;
    }

    struct TypeInfo {
        TypeId id{};
        std::string name;
        std::uint32_t version{1};
        TypeKind kind{TypeKind::Struct};
        AttributeSet attributes;
        std::vector<FieldInfo> fields;
    };

} // namespace asharia::reflection
