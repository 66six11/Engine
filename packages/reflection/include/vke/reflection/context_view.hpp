#pragma once

#include <vector>

#include "vke/reflection/type_info.hpp"

namespace vke::reflection {

    struct ContextFieldView {
        std::vector<const FieldInfo*> fields;
    };

    [[nodiscard]] inline ContextFieldView makeSerializeContextView(const TypeInfo& type) {
        ContextFieldView view;
        for (const FieldInfo& field : type.fields) {
            if (isSerializableField(field)) {
                view.fields.push_back(&field);
            }
        }
        return view;
    }

    [[nodiscard]] inline ContextFieldView makeEditContextView(const TypeInfo& type) {
        ContextFieldView view;
        for (const FieldInfo& field : type.fields) {
            if (isEditorVisibleField(field)) {
                view.fields.push_back(&field);
            }
        }
        return view;
    }

    [[nodiscard]] inline ContextFieldView makeScriptContextView(const TypeInfo& type) {
        ContextFieldView view;
        for (const FieldInfo& field : type.fields) {
            if (isScriptVisibleField(field)) {
                view.fields.push_back(&field);
            }
        }
        return view;
    }

} // namespace vke::reflection
