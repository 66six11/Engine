#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace asharia::schema {

    struct TypeId {
        std::string stableName;

        [[nodiscard]] friend bool operator==(const TypeId&, const TypeId&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return !stableName.empty();
        }
    };

    struct FieldId {
        std::uint32_t value{};

        [[nodiscard]] friend bool operator==(FieldId, FieldId) = default;
        [[nodiscard]] explicit constexpr operator bool() const noexcept {
            return value != 0U;
        }
    };

    [[nodiscard]] inline TypeId makeTypeId(std::string_view stableName) {
        return TypeId{std::string{stableName}};
    }

    [[nodiscard]] constexpr FieldId makeFieldId(std::uint32_t value) noexcept {
        return FieldId{value};
    }

    namespace builtin {
        inline constexpr std::string_view kBoolName = "com.asharia.core.Bool";
        inline constexpr std::string_view kInt32Name = "com.asharia.core.Int32";
        inline constexpr std::string_view kUInt32Name = "com.asharia.core.UInt32";
        inline constexpr std::string_view kUInt64Name = "com.asharia.core.UInt64";
        inline constexpr std::string_view kFloatName = "com.asharia.core.Float";
        inline constexpr std::string_view kDoubleName = "com.asharia.core.Double";
        inline constexpr std::string_view kStringName = "com.asharia.core.String";

        [[nodiscard]] inline TypeId boolTypeId() {
            return makeTypeId(kBoolName);
        }

        [[nodiscard]] inline TypeId int32TypeId() {
            return makeTypeId(kInt32Name);
        }

        [[nodiscard]] inline TypeId uint32TypeId() {
            return makeTypeId(kUInt32Name);
        }

        [[nodiscard]] inline TypeId uint64TypeId() {
            return makeTypeId(kUInt64Name);
        }

        [[nodiscard]] inline TypeId floatTypeId() {
            return makeTypeId(kFloatName);
        }

        [[nodiscard]] inline TypeId doubleTypeId() {
            return makeTypeId(kDoubleName);
        }

        [[nodiscard]] inline TypeId stringTypeId() {
            return makeTypeId(kStringName);
        }
    } // namespace builtin

} // namespace asharia::schema
