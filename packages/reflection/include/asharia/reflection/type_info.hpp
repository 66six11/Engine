#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "asharia/reflection/field_flags.hpp"
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

    struct NumericRange {
        double minValue{};
        double maxValue{};
        double step{};
    };

    struct FieldAttributes {
        std::string displayName;
        std::string category;
        std::string tooltip;
        std::optional<NumericRange> range;
    };

    using ReadFieldAddressFn = std::function<const void*(const void*)>;
    using WriteFieldAddressFn = std::function<void*(void*)>;

    struct FieldAccessor {
        TypeId ownerType{};
        TypeId fieldType{};
        std::size_t size{};
        std::size_t alignment{};
        ReadFieldAddressFn readAddress;
        WriteFieldAddressFn writeAddress;
    };

    struct FieldInfo {
        FieldId id{};
        std::string name;
        TypeId type{};
        FieldFlagSet flags;
        FieldAttributes attributes;
        FieldAccessor accessor;
    };

    struct TypeInfo {
        TypeId id{};
        std::string name;
        std::uint32_t version{1};
        TypeKind kind{TypeKind::Struct};
        std::vector<FieldInfo> fields;
    };

    [[nodiscard]] constexpr bool isSerializableField(const FieldInfo& field) noexcept {
        return field.flags.has(FieldFlag::Serializable) && !field.flags.has(FieldFlag::Transient);
    }

    [[nodiscard]] constexpr bool isEditorVisibleField(const FieldInfo& field) noexcept {
        return field.flags.has(FieldFlag::EditorVisible);
    }

    [[nodiscard]] constexpr bool isScriptVisibleField(const FieldInfo& field) noexcept {
        return field.flags.has(FieldFlag::ScriptVisible);
    }

} // namespace asharia::reflection
