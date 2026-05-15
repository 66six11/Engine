#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asharia::reflection {

    enum class AttributeValueKind : std::uint8_t {
        Bool,
        Int64,
        Float64,
        String,
    };

    struct AttributeValue {
        [[nodiscard]] static AttributeValue boolean(bool value) {
            AttributeValue attribute;
            attribute.kind = AttributeValueKind::Bool;
            attribute.boolValue = value;
            return attribute;
        }

        [[nodiscard]] static AttributeValue integer(std::int64_t value) {
            AttributeValue attribute;
            attribute.kind = AttributeValueKind::Int64;
            attribute.int64Value = value;
            return attribute;
        }

        [[nodiscard]] static AttributeValue floating(double value) {
            AttributeValue attribute;
            attribute.kind = AttributeValueKind::Float64;
            attribute.float64Value = value;
            return attribute;
        }

        [[nodiscard]] static AttributeValue string(std::string value) {
            AttributeValue attribute;
            attribute.kind = AttributeValueKind::String;
            attribute.stringValue = std::move(value);
            return attribute;
        }

        AttributeValueKind kind{AttributeValueKind::Bool};
        bool boolValue{};
        std::int64_t int64Value{};
        double float64Value{};
        std::string stringValue;
    };

    struct FieldAttribute {
        std::string key;
        AttributeValue value;
    };

    using AttributeSet = std::vector<FieldAttribute>;

    namespace attributes {
        [[nodiscard]] inline FieldAttribute boolean(std::string_view key, bool value) {
            return FieldAttribute{
                .key = std::string{key},
                .value = AttributeValue::boolean(value),
            };
        }

        [[nodiscard]] inline FieldAttribute integer(std::string_view key, std::int64_t value) {
            return FieldAttribute{
                .key = std::string{key},
                .value = AttributeValue::integer(value),
            };
        }

        [[nodiscard]] inline FieldAttribute floating(std::string_view key, double value) {
            return FieldAttribute{
                .key = std::string{key},
                .value = AttributeValue::floating(value),
            };
        }

        [[nodiscard]] inline FieldAttribute string(std::string_view key, std::string value) {
            return FieldAttribute{
                .key = std::string{key},
                .value = AttributeValue::string(std::move(value)),
            };
        }
    } // namespace attributes

    [[nodiscard]] inline const FieldAttribute*
    findAttribute(std::span<const FieldAttribute> attributes, std::string_view key) {
        for (const FieldAttribute& attribute : attributes) {
            if (attribute.key == key) {
                return &attribute;
            }
        }
        return nullptr;
    }

    [[nodiscard]] inline bool hasAttribute(std::span<const FieldAttribute> attributes,
                                           std::string_view key) {
        return findAttribute(attributes, key) != nullptr;
    }

    [[nodiscard]] inline bool hasBoolAttribute(std::span<const FieldAttribute> attributes,
                                               std::string_view key, bool expected = true) {
        const FieldAttribute* attribute = findAttribute(attributes, key);
        return attribute != nullptr && attribute->value.kind == AttributeValueKind::Bool &&
               attribute->value.boolValue == expected;
    }

} // namespace asharia::reflection
