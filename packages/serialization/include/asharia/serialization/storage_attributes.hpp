#pragma once

#include <string_view>

#include "asharia/reflection/attributes.hpp"

namespace asharia::serialization::storage_attributes {

    inline constexpr std::string_view kPersistent = "asharia.storage.persistent";
    inline constexpr std::string_view kTransient = "asharia.storage.transient";

    [[nodiscard]] inline reflection::FieldAttribute persistent(bool value = true) {
        return reflection::attributes::boolean(kPersistent, value);
    }

    [[nodiscard]] inline reflection::FieldAttribute transient(bool value = true) {
        return reflection::attributes::boolean(kTransient, value);
    }

} // namespace asharia::serialization::storage_attributes
