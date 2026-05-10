#pragma once

#include <cstdint>
#include <string_view>

namespace vke::reflection {

    struct TypeId {
        std::uint64_t value{};

        [[nodiscard]] friend bool operator==(TypeId, TypeId) = default;
        [[nodiscard]] constexpr explicit operator bool() const noexcept {
            return value != 0;
        }
    };

    struct FieldId {
        std::uint64_t value{};

        [[nodiscard]] friend bool operator==(FieldId, FieldId) = default;
        [[nodiscard]] constexpr explicit operator bool() const noexcept {
            return value != 0;
        }
    };

    namespace detail {
        inline constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
        inline constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

        [[nodiscard]] constexpr std::uint64_t hashAppend(std::uint64_t hash,
                                                         std::string_view text) noexcept {
            for (const char character : text) {
                hash ^= static_cast<unsigned char>(character);
                hash *= kFnv1a64Prime;
            }
            return hash;
        }
    } // namespace detail

    [[nodiscard]] constexpr std::uint64_t stableHash(std::string_view text) noexcept {
        return detail::hashAppend(detail::kFnv1a64Offset, text);
    }

    [[nodiscard]] constexpr TypeId makeTypeId(std::string_view typeName) noexcept {
        return TypeId{stableHash(typeName)};
    }

    [[nodiscard]] constexpr FieldId makeFieldId(std::string_view ownerTypeName,
                                                std::string_view fieldName) noexcept {
        std::uint64_t hash = detail::hashAppend(detail::kFnv1a64Offset, ownerTypeName);
        hash ^= static_cast<unsigned char>('.');
        hash *= detail::kFnv1a64Prime;
        hash = detail::hashAppend(hash, fieldName);
        return FieldId{hash};
    }

    namespace builtin {
        inline constexpr std::string_view kBoolName = "com.vke.core.Bool";
        inline constexpr std::string_view kInt32Name = "com.vke.core.Int32";
        inline constexpr std::string_view kUInt32Name = "com.vke.core.UInt32";
        inline constexpr std::string_view kUInt64Name = "com.vke.core.UInt64";
        inline constexpr std::string_view kFloatName = "com.vke.core.Float";
        inline constexpr std::string_view kDoubleName = "com.vke.core.Double";
        inline constexpr std::string_view kStringName = "com.vke.core.String";

        [[nodiscard]] constexpr TypeId boolTypeId() noexcept {
            return makeTypeId(kBoolName);
        }

        [[nodiscard]] constexpr TypeId int32TypeId() noexcept {
            return makeTypeId(kInt32Name);
        }

        [[nodiscard]] constexpr TypeId uint32TypeId() noexcept {
            return makeTypeId(kUInt32Name);
        }

        [[nodiscard]] constexpr TypeId uint64TypeId() noexcept {
            return makeTypeId(kUInt64Name);
        }

        [[nodiscard]] constexpr TypeId floatTypeId() noexcept {
            return makeTypeId(kFloatName);
        }

        [[nodiscard]] constexpr TypeId doubleTypeId() noexcept {
            return makeTypeId(kDoubleName);
        }

        [[nodiscard]] constexpr TypeId stringTypeId() noexcept {
            return makeTypeId(kStringName);
        }
    } // namespace builtin

} // namespace vke::reflection
