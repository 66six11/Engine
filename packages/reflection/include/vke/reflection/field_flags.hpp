#pragma once

#include <cstdint>

namespace vke::reflection {

    enum class FieldFlag : std::uint32_t {
        Serializable = 1U << 0U,
        EditorVisible = 1U << 1U,
        RuntimeVisible = 1U << 2U,
        ScriptVisible = 1U << 3U,
        ReadOnly = 1U << 4U,
        Transient = 1U << 5U,
        EditorOnly = 1U << 6U,
        AssetReference = 1U << 7U,
        EntityReference = 1U << 8U,
    };

    class FieldFlagSet {
    public:
        constexpr FieldFlagSet() = default;

        constexpr FieldFlagSet(FieldFlag flag)
            : bits_{static_cast<std::uint32_t>(flag)} {}

        constexpr explicit FieldFlagSet(std::uint32_t bits)
            : bits_{bits} {}

        [[nodiscard]] constexpr bool has(FieldFlag flag) const noexcept {
            return (bits_ & static_cast<std::uint32_t>(flag)) != 0;
        }

        [[nodiscard]] constexpr bool empty() const noexcept {
            return bits_ == 0;
        }

        [[nodiscard]] constexpr std::uint32_t raw() const noexcept {
            return bits_;
        }

        constexpr FieldFlagSet& add(FieldFlag flag) noexcept {
            bits_ |= static_cast<std::uint32_t>(flag);
            return *this;
        }

        constexpr FieldFlagSet& remove(FieldFlag flag) noexcept {
            bits_ &= ~static_cast<std::uint32_t>(flag);
            return *this;
        }

    private:
        std::uint32_t bits_{};
    };

    [[nodiscard]] constexpr FieldFlagSet operator|(FieldFlag lhs, FieldFlag rhs) noexcept {
        return FieldFlagSet{lhs}.add(rhs);
    }

    [[nodiscard]] constexpr FieldFlagSet operator|(FieldFlagSet lhs, FieldFlag rhs) noexcept {
        return lhs.add(rhs);
    }

    [[nodiscard]] constexpr FieldFlagSet operator|(FieldFlag lhs, FieldFlagSet rhs) noexcept {
        return rhs.add(lhs);
    }

    [[nodiscard]] constexpr FieldFlagSet operator&(FieldFlagSet lhs, FieldFlag rhs) noexcept {
        return FieldFlagSet{lhs.raw() & static_cast<std::uint32_t>(rhs)};
    }

    namespace field_flags {
        [[nodiscard]] constexpr FieldFlagSet serializableEditorRuntime() noexcept {
            return FieldFlag::Serializable | FieldFlag::EditorVisible | FieldFlag::RuntimeVisible;
        }

        [[nodiscard]] constexpr FieldFlagSet serializableEditorRuntimeScript() noexcept {
            return serializableEditorRuntime() | FieldFlag::ScriptVisible;
        }
    } // namespace field_flags

} // namespace vke::reflection
