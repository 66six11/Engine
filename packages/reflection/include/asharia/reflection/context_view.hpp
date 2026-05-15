#pragma once

#include <string_view>
#include <vector>

#include "asharia/reflection/type_info.hpp"

namespace asharia::reflection {

    struct ContextFieldView {
        std::vector<const FieldInfo*> fields;
    };

    [[nodiscard]] inline ContextFieldView makeAttributeContextView(const TypeInfo& type,
                                                                   std::string_view attributeKey,
                                                                   bool expectedValue = true) {
        ContextFieldView view;
        for (const FieldInfo& field : type.fields) {
            if (hasBoolAttribute(field.attributes, attributeKey, expectedValue)) {
                view.fields.push_back(&field);
            }
        }
        return view;
    }

} // namespace asharia::reflection
