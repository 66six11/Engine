#include "asharia/material_instance/mat_parameters.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace asharia::material_instance {
    namespace {
        using shader_authoring::ShaderPropertyDecl;
        using shader_authoring::ShaderPropertyDefaultKind;
        using shader_authoring::ShaderPropertyType;

        Error parameterError(MatParameterError code, std::string_view property,
                             std::string_view message) {
            return {ErrorDomain::Material, static_cast<int>(code),
                    "Material parameter '" + std::string{property} + "': " + std::string{message}};
        }

        std::uint32_t componentCount(ShaderPropertyType type) {
            switch (type) {
            case ShaderPropertyType::Float:
            case ShaderPropertyType::Int:
            case ShaderPropertyType::UInt:
            case ShaderPropertyType::Bool:
                return 1;
            case ShaderPropertyType::Float2:
                return 2;
            case ShaderPropertyType::Float3:
                return 3;
            case ShaderPropertyType::Float4:
            case ShaderPropertyType::Color:
                return 4;
            default:
                return 0;
            }
        }

        template <typename T> bool parseNumber(std::string_view text, T& value) {
            if (text.empty()) {
                return false;
            }
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
        }

        Result<MatPropertyValue> defaultValue(const ShaderPropertyDecl& property) {
            const auto& source = property.defaultValue;
            MatPropertyValue result;
            bool valid = false;
            switch (property.type) {
            case ShaderPropertyType::Float:
                result.kind = MatPropertyValueKind::Number;
                valid = (source.kind == ShaderPropertyDefaultKind::Number ||
                         source.kind == ShaderPropertyDefaultKind::Integer) &&
                        parseNumber(source.text, result.numberValue);
                break;
            case ShaderPropertyType::Int:
                result.kind = MatPropertyValueKind::Integer;
                valid = source.kind == ShaderPropertyDefaultKind::Integer &&
                        parseNumber(source.text, result.integerValue);
                break;
            case ShaderPropertyType::UInt:
                result.kind = MatPropertyValueKind::UnsignedInteger;
                valid = source.kind == ShaderPropertyDefaultKind::Integer &&
                        parseNumber(source.text, result.unsignedIntegerValue);
                break;
            case ShaderPropertyType::Bool:
                result.kind = MatPropertyValueKind::Boolean;
                valid = source.kind == ShaderPropertyDefaultKind::Boolean &&
                        (source.text == "true" || source.text == "false");
                result.boolValue = source.text == "true";
                break;
            default:
                result.kind = MatPropertyValueKind::Vector;
                valid = source.kind == ShaderPropertyDefaultKind::Vector &&
                        source.elements.size() == componentCount(property.type);
                if (valid) {
                    for (const auto& text : source.elements) {
                        double value{};
                        if (!parseNumber(text, value)) {
                            valid = false;
                            break;
                        }
                        result.vectorValue.push_back(value);
                    }
                }
                break;
            }
            if (!valid) {
                return std::unexpected{parameterError(MatParameterError::InvalidValue,
                                                      property.name, "missing or invalid default")};
            }
            return result;
        }

        Result<std::uint32_t> floatWord(double value, std::string_view property) {
            static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
                return std::unexpected{parameterError(MatParameterError::InvalidValue, property,
                                                      "value is outside finite float32 range")};
            }
            const auto converted = static_cast<float>(value);
            if (value != 0.0 && converted == 0.0F) {
                return std::unexpected{parameterError(MatParameterError::InvalidValue, property,
                                                      "nonzero value underflows to zero")};
            }
            return std::bit_cast<std::uint32_t>(converted);
        }

        Result<std::array<std::uint32_t, 4>> valueWords(const MatPropertyValue& value,
                                                        std::string_view property) {
            std::array<std::uint32_t, 4> words{};
            switch (value.kind) {
            case MatPropertyValueKind::Number: {
                auto word = floatWord(value.numberValue, property);
                if (!word) {
                    return std::unexpected{word.error()};
                }
                words[0] = *word;
                break;
            }
            case MatPropertyValueKind::Vector:
                for (std::size_t index = 0; index < value.vectorValue.size(); ++index) {
                    auto word = floatWord(value.vectorValue[index], property);
                    if (!word) {
                        return std::unexpected{word.error()};
                    }
                    words.at(index) = *word;
                }
                break;
            case MatPropertyValueKind::Integer:
                if (value.integerValue < std::numeric_limits<std::int32_t>::min() ||
                    value.integerValue > std::numeric_limits<std::int32_t>::max()) {
                    return std::unexpected{parameterError(MatParameterError::InvalidValue, property,
                                                          "value exceeds int32 range")};
                }
                words[0] = static_cast<std::uint32_t>(value.integerValue);
                break;
            case MatPropertyValueKind::UnsignedInteger:
                if (value.unsignedIntegerValue > std::numeric_limits<std::uint32_t>::max()) {
                    return std::unexpected{parameterError(MatParameterError::InvalidValue, property,
                                                          "value exceeds uint32 range")};
                }
                words[0] = static_cast<std::uint32_t>(value.unsignedIntegerValue);
                break;
            case MatPropertyValueKind::Boolean:
                words[0] = value.boolValue ? 1U : 0U;
                break;
            case MatPropertyValueKind::AssetGuid:
                return std::unexpected{parameterError(MatParameterError::UnsupportedType, property,
                                                      "resource values are not constants")};
            }
            return words;
        }
        VoidResult writeWords(MatParameterBlock& block, std::vector<bool>& occupied,
                              std::span<const std::uint32_t> words,
                              const MatParameterMember& member) {
            for (std::size_t component = 0; component < words.size(); ++component) {
                const auto offset = member.byteOffset + (component * 4);
                if (occupied[offset / 4]) {
                    return std::unexpected{parameterError(MatParameterError::InvalidLayout,
                                                          member.propertyId,
                                                          "overlapping members")};
                }
                occupied[offset / 4] = true;
                for (std::uint32_t byte = 0; byte < 4; ++byte) {
                    block.bytes[offset + byte] =
                        static_cast<std::byte>((words[component] >> (byte * 8)) & 0xffU);
                }
            }
            return {};
        }
    } // namespace

    Result<MatParameterBlock> packMatParameters(const MatDocument& document,
                                                const shader_authoring::ShaderDocument& shader,
                                                std::span<const MatParameterMember> members,
                                                std::uint32_t byteSize,
                                                const MatResolveOptions& options) {
        if (byteSize > kMaxMatParameterBytes || members.size() > kMaxMatParameters ||
            shader.properties.size() > kMaxMatParameters ||
            document.properties.size() > kMaxMatParameters) {
            return std::unexpected{parameterError(MatParameterError::BudgetExceeded, {},
                                                  "parameter count or byte budget exceeded")};
        }
        if (shader.schemaVersion != 2 || shader.shaderTypeId.empty()) {
            return std::unexpected{parameterError(MatParameterError::InvalidInput, {},
                                                  "invalid shader schema or identity")};
        }
        auto resolved = resolveMatOverrides(document, shader, options);
        const auto diagnostic = std::ranges::find(
            resolved.diagnostics, MatDiagnosticSeverity::Error, &MatDiagnostic::severity);
        if (diagnostic != resolved.diagnostics.end()) {
            return std::unexpected{parameterError(MatParameterError::InvalidInput,
                                                  diagnostic->propertyId, diagnostic->message)};
        }
        if (members.size() != shader.properties.size() || byteSize % 4 != 0 ||
            (members.empty() && byteSize != 0)) {
            return std::unexpected{parameterError(MatParameterError::InvalidLayout, {},
                                                  "layout must cover all properties exactly")};
        }
        MatParameterBlock result{.bytes = std::vector<std::byte>(byteSize),
                                 .diagnostics = std::move(resolved.diagnostics)};
        std::vector<bool> occupied(byteSize / 4, false);
        for (std::size_t index = 0; index < shader.properties.size(); ++index) {
            const auto& property = shader.properties[index];
            if (property.name.empty() || std::ranges::count(shader.properties, property.name,
                                                            &ShaderPropertyDecl::name) != 1) {
                return std::unexpected{parameterError(
                    MatParameterError::InvalidInput, property.name, "empty or duplicate property")};
            }
            const auto count = componentCount(property.type);
            if (count == 0) {
                return std::unexpected{parameterError(MatParameterError::UnsupportedType,
                                                      property.name,
                                                      "only numeric parameters supported")};
            }
            const auto member =
                std::ranges::find(members, property.name, &MatParameterMember::propertyId);
            if (member == members.end() ||
                std::ranges::count(members, property.name, &MatParameterMember::propertyId) != 1 ||
                member->type != property.type || member->byteOffset % 4 != 0 ||
                member->byteOffset > byteSize || count * 4 > byteSize - member->byteOffset) {
                return std::unexpected{
                    parameterError(MatParameterError::InvalidLayout, property.name,
                                   "missing, duplicate, mismatched or out-of-range member")};
            }
            const auto overrideValue = std::ranges::find(document.properties, property.name,
                                                         &MatPropertyOverride::propertyId);
            auto value = overrideValue == document.properties.end()
                             ? defaultValue(property)
                             : Result<MatPropertyValue>{overrideValue->value};
            if (!value) {
                return std::unexpected{value.error()};
            }
            auto words = valueWords(*value, property.name);
            if (!words) {
                return std::unexpected{words.error()};
            }
            if (auto written =
                    writeWords(result, occupied, std::span{*words}.first(count), *member);
                !written) {
                return std::unexpected{written.error()};
            }
        }
        return result;
    }
} // namespace asharia::material_instance
