#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/error.hpp"
#include "asharia/core/result.hpp"
#include "asharia/schema/schema_registry.hpp"

namespace asharia::cpp_binding {

    struct FieldReadContext {
        const void* object{};
    };

    struct FieldWriteContext {
        void* object{};
    };

    struct FieldValueView {
        schema::TypeId type;
        const void* data{};
        std::size_t size{};
        std::size_t alignment{};
    };

    using ReadFieldAddressFn = std::function<const void*(const void*)>;
    using WriteFieldAddressFn = std::function<void*(void*)>;
    using ConstructFieldValueFn = std::function<VoidResult(void*)>;
    using DestroyFieldValueFn = std::function<void(void*)>;
    using ReadFieldValueFn = std::function<VoidResult(const void*, void*)>;
    using WriteFieldValueFn = std::function<VoidResult(void*, const void*)>;
    using WriteDefaultFieldValueFn = std::function<VoidResult(void*)>;

    struct FieldBinding {
        schema::TypeId ownerSchema;
        schema::FieldId fieldId{};
        std::string cppMemberName;
        schema::TypeId valueType;
        schema::TypeId defaultValueType;
        std::size_t size{};
        std::size_t alignment{};
        ReadFieldAddressFn readAddress;
        WriteFieldAddressFn writeAddress;
        ConstructFieldValueFn constructValue;
        DestroyFieldValueFn destroyValue;
        ReadFieldValueFn readValue;
        WriteFieldValueFn writeValue;
        WriteDefaultFieldValueFn writeDefaultValue;
    };

    struct CppTypeBinding {
        schema::TypeId schemaType;
        std::string cppTypeName;
        std::vector<FieldBinding> fields;
    };

    [[nodiscard]] inline const FieldBinding* findFieldBinding(const CppTypeBinding& binding,
                                                              schema::FieldId fieldId) {
        for (const FieldBinding& field : binding.fields) {
            if (field.fieldId == fieldId) {
                return &field;
            }
        }
        return nullptr;
    }

    [[nodiscard]] inline FieldBinding* findFieldBinding(CppTypeBinding& binding,
                                                        schema::FieldId fieldId) {
        for (FieldBinding& field : binding.fields) {
            if (field.fieldId == fieldId) {
                return &field;
            }
        }
        return nullptr;
    }

} // namespace asharia::cpp_binding
