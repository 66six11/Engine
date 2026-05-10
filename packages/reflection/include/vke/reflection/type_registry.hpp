#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "vke/core/error.hpp"
#include "vke/core/result.hpp"
#include "vke/reflection/type_info.hpp"

namespace vke::reflection {

    [[nodiscard]] Error reflectionError(std::string message);

    class TypeRegistry {
    public:
        [[nodiscard]] VoidResult registerType(TypeInfo type);
        [[nodiscard]] VoidResult freeze();

        [[nodiscard]] bool isFrozen() const noexcept {
            return frozen_;
        }

        [[nodiscard]] const TypeInfo* findType(TypeId typeId) const;
        [[nodiscard]] const TypeInfo* findType(std::string_view name) const;

        [[nodiscard]] std::span<const TypeInfo> types() const noexcept {
            return types_;
        }

    private:
        bool frozen_{};
        std::vector<TypeInfo> types_;
    };

    [[nodiscard]] VoidResult registerBuiltinTypes(TypeRegistry& registry);

} // namespace vke::reflection
