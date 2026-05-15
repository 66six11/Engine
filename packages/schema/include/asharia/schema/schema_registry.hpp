#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/error.hpp"
#include "asharia/core/result.hpp"
#include "asharia/schema/type_schema.hpp"

namespace asharia::schema {

    [[nodiscard]] Error schemaError(std::string message);

    class SchemaRegistry {
    public:
        [[nodiscard]] VoidResult registerType(TypeSchema type);
        [[nodiscard]] VoidResult freeze();

        [[nodiscard]] bool isFrozen() const noexcept {
            return frozen_;
        }

        [[nodiscard]] const TypeSchema* findType(const TypeId& typeId) const;
        [[nodiscard]] const TypeSchema* findType(std::string_view stableName) const;

        [[nodiscard]] std::span<const TypeSchema> types() const noexcept {
            return types_;
        }

    private:
        bool frozen_{};
        std::vector<TypeSchema> types_;
    };

    [[nodiscard]] VoidResult registerBuiltinSchemas(SchemaRegistry& registry);

} // namespace asharia::schema
