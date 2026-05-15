#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/cpp_binding/type_binding.hpp"

namespace asharia::cpp_binding {

    [[nodiscard]] Error bindingError(std::string message);

    class BindingRegistry {
    public:
        explicit BindingRegistry(const schema::SchemaRegistry& schemaRegistry) noexcept;

        [[nodiscard]] VoidResult registerType(CppTypeBinding binding);
        [[nodiscard]] VoidResult freeze();

        [[nodiscard]] bool isFrozen() const noexcept {
            return frozen_;
        }

        [[nodiscard]] const schema::SchemaRegistry& schemaRegistry() const noexcept {
            return schemaRegistry_;
        }

        [[nodiscard]] const CppTypeBinding* findBinding(const schema::TypeId& typeId) const;
        [[nodiscard]] const CppTypeBinding* findBinding(std::string_view stableName) const;

        [[nodiscard]] std::span<const CppTypeBinding> bindings() const noexcept {
            return bindings_;
        }

    private:
        const schema::SchemaRegistry& schemaRegistry_;
        bool frozen_{};
        std::vector<CppTypeBinding> bindings_;
    };

} // namespace asharia::cpp_binding
