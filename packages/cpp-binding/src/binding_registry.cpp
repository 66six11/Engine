#include "asharia/cpp_binding/binding_registry.hpp"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace asharia::cpp_binding {
    namespace {

        struct DiagnosticField {
            std::string key;
            std::string value;

            DiagnosticField(const char* fieldKey, std::string_view fieldValue)
                : key{fieldKey}, value{fieldValue} {}
        };

        [[nodiscard]] std::string fieldIdText(schema::FieldId fieldId) {
            return std::to_string(fieldId.value);
        }

        [[nodiscard]] Error makeBindingError(std::string message,
                                             std::initializer_list<DiagnosticField> details = {}) {
            bool wroteHeader = false;
            for (const DiagnosticField& detail : details) {
                if (detail.value.empty()) {
                    continue;
                }
                if (!wroteHeader) {
                    message += " [";
                    wroteHeader = true;
                } else {
                    message += "; ";
                }
                message += detail.key;
                message += '=';
                message += detail.value;
            }
            if (wroteHeader) {
                message += ']';
            }
            return Error{ErrorDomain::CppBinding, 0, std::move(message)};
        }

        [[nodiscard]] VoidResult validateFieldBinding(const schema::SchemaRegistry& schemas,
                                                      const CppTypeBinding& binding,
                                                      const FieldBinding& field) {
            const schema::TypeSchema* type = schemas.findType(binding.schemaType);
            const schema::FieldSchema* schemaField =
                type == nullptr ? nullptr : schema::findFieldById(*type, field.fieldId);
            if (schemaField == nullptr) {
                return std::unexpected{
                    makeBindingError("C++ binding references an unknown schema field.",
                                     {{"operation", "register"},
                                      {"type", binding.schemaType.stableName},
                                      {"fieldId", fieldIdText(field.fieldId)},
                                      {"expected", "registered schema field"},
                                      {"actual", "missing"}})};
            }
            if (field.ownerSchema != binding.schemaType) {
                return std::unexpected{
                    makeBindingError("C++ binding field owner does not match type binding.",
                                     {{"operation", "register"},
                                      {"type", binding.schemaType.stableName},
                                      {"field", schemaField->key},
                                      {"expected", binding.schemaType.stableName},
                                      {"actual", field.ownerSchema.stableName}})};
            }
            if (field.valueType && field.valueType != schemaField->valueType) {
                return std::unexpected{
                    makeBindingError("C++ binding field value type does not match schema.",
                                     {{"operation", "register"},
                                      {"type", binding.schemaType.stableName},
                                      {"field", schemaField->key},
                                      {"expected", schemaField->valueType.stableName},
                                      {"actual", field.valueType.stableName}})};
            }
            if (field.writeDefaultValue && field.defaultValueType &&
                field.defaultValueType != schemaField->valueType) {
                return std::unexpected{
                    makeBindingError(
                        "C++ binding field default value type does not match schema.",
                        {{"operation", "register"},
                         {"type", binding.schemaType.stableName},
                         {"field", schemaField->key},
                         {"expected", schemaField->valueType.stableName},
                         {"actual", field.defaultValueType.stableName}})};
            }
            if (field.size == 0U || field.alignment == 0U) {
                return std::unexpected{
                    makeBindingError("C++ binding field has invalid storage metadata.",
                                     {{"operation", "register"},
                                      {"type", binding.schemaType.stableName},
                                      {"field", schemaField->key},
                                      {"expected", "size/alignment"},
                                      {"actual", "zero"}})};
            }
            if (!field.readAddress && !field.readValue) {
                return std::unexpected{makeBindingError("C++ binding field cannot be read.",
                                                        {{"operation", "register"},
                                                         {"type", binding.schemaType.stableName},
                                                         {"field", schemaField->key},
                                                         {"expected", "read binding"},
                                                         {"actual", "missing"}})};
            }
            return {};
        }

    } // namespace

    Error bindingError(std::string message) {
        return Error{ErrorDomain::CppBinding, 0, std::move(message)};
    }

    BindingRegistry::BindingRegistry(const schema::SchemaRegistry& schemaRegistry) noexcept
        : schemaRegistry_{schemaRegistry} {}

    VoidResult BindingRegistry::registerType(CppTypeBinding binding) {
        if (frozen_) {
            return std::unexpected{makeBindingError("Cannot register C++ binding after freeze.",
                                                    {{"operation", "register"},
                                                     {"type", binding.schemaType.stableName},
                                                     {"expected", "mutable binding registry"},
                                                     {"actual", "frozen"}})};
        }

        if (schemaRegistry_.findType(binding.schemaType) == nullptr) {
            return std::unexpected{
                makeBindingError("C++ binding references an unknown schema type.",
                                 {{"operation", "register"},
                                  {"type", binding.schemaType.stableName},
                                  {"expected", "registered schema type"},
                                  {"actual", "missing"}})};
        }

        for (const CppTypeBinding& existing : bindings_) {
            if (existing.schemaType == binding.schemaType) {
                return std::unexpected{
                    makeBindingError("C++ binding registry already contains this type.",
                                     {{"operation", "register"},
                                      {"type", binding.schemaType.stableName},
                                      {"expected", "unique type binding"},
                                      {"actual", "duplicate"}})};
            }
        }

        for (const FieldBinding& field : binding.fields) {
            const auto duplicateField =
                std::ranges::count_if(binding.fields, [&field](const FieldBinding& other) {
                    return other.fieldId == field.fieldId;
                });
            if (duplicateField > 1) {
                return std::unexpected{
                    makeBindingError("C++ binding type has duplicate field bindings.",
                                     {{"operation", "register"},
                                      {"type", binding.schemaType.stableName},
                                      {"fieldId", fieldIdText(field.fieldId)},
                                      {"expected", "unique field binding"},
                                      {"actual", "duplicate"}})};
            }
            if (auto valid = validateFieldBinding(schemaRegistry_, binding, field); !valid) {
                return valid;
            }
        }

        bindings_.push_back(std::move(binding));
        return {};
    }

    VoidResult BindingRegistry::freeze() {
        frozen_ = true;
        return {};
    }

    const CppTypeBinding* BindingRegistry::findBinding(const schema::TypeId& typeId) const {
        if (!typeId) {
            return nullptr;
        }
        return findBinding(typeId.stableName);
    }

    const CppTypeBinding* BindingRegistry::findBinding(std::string_view stableName) const {
        const auto found =
            std::ranges::find_if(bindings_, [stableName](const CppTypeBinding& binding) {
                return binding.schemaType.stableName == stableName;
            });
        return found == bindings_.end() ? nullptr : &*found;
    }

} // namespace asharia::cpp_binding
