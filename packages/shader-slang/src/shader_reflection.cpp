#include <charconv>
#include <cstddef>
#include <expected>
#include <fstream>
#include <optional>
#include <string_view>

#include "vke/core/error.hpp"
#include "vke/shader_slang/reflection.hpp"

namespace vke {
    namespace {

        [[nodiscard]] Error reflectionError(std::string message) {
            return Error{ErrorDomain::Shader, 0, std::move(message)};
        }

        struct JsonPropertyQuery {
            std::string_view json;
            std::string_view name;
        };

        struct JsonArrayQuery {
            std::string_view arrayJson;
            std::string_view name;
        };

        struct DescriptorBindingParseQuery {
            std::string_view json;
            std::string_view stageVisibility;
        };

        struct PushConstantParseQuery {
            std::string_view json;
            std::string_view fallbackStageVisibility;
        };

        [[nodiscard]] Result<std::string> readTextFile(const std::filesystem::path& path) {
            std::ifstream file{path, std::ios::binary | std::ios::ate};
            if (!file) {
                return std::unexpected{
                    reflectionError("Failed to open shader reflection JSON: " + path.string())};
            }

            const std::streamsize size = file.tellg();
            if (size < 0) {
                return std::unexpected{
                    reflectionError("Failed to measure shader reflection JSON: " + path.string())};
            }

            std::string text(static_cast<std::size_t>(size), '\0');
            file.seekg(0, std::ios::beg);
            if (!file.read(text.data(), size)) {
                return std::unexpected{
                    reflectionError("Failed to read shader reflection JSON: " + path.string())};
            }
            return text;
        }

        [[nodiscard]] std::optional<std::size_t> findPropertyValue(JsonPropertyQuery query) {
            const std::string key = "\"" + std::string{query.name} + "\"";
            const std::size_t keyOffset = query.json.find(key);
            if (keyOffset == std::string_view::npos) {
                return std::nullopt;
            }

            const std::size_t colonOffset = query.json.find(':', keyOffset + key.size());
            if (colonOffset == std::string_view::npos) {
                return std::nullopt;
            }

            std::size_t valueOffset = colonOffset + 1;
            while (valueOffset < query.json.size() &&
                   (query.json[valueOffset] == ' ' || query.json[valueOffset] == '\n' ||
                    query.json[valueOffset] == '\r' || query.json[valueOffset] == '\t')) {
                ++valueOffset;
            }

            return valueOffset;
        }

        [[nodiscard]] Result<std::string> parseStringProperty(std::string_view json,
                                                              std::string_view name) {
            const auto valueOffset =
                findPropertyValue(JsonPropertyQuery{.json = json, .name = name});
            if (!valueOffset || *valueOffset >= json.size() || json[*valueOffset] != '"') {
                return std::unexpected{reflectionError(
                    "Missing string property in shader reflection JSON: " + std::string{name})};
            }

            std::string value;
            for (std::size_t offset = *valueOffset + 1; offset < json.size(); ++offset) {
                const char character = json[offset];
                if (character == '"') {
                    return value;
                }
                if (character == '\\') {
                    if (offset + 1 >= json.size()) {
                        break;
                    }
                    const char escaped = json[++offset];
                    switch (escaped) {
                    case '\\':
                    case '"':
                    case '/':
                        value += escaped;
                        break;
                    case 'n':
                        value += '\n';
                        break;
                    case 'r':
                        value += '\r';
                        break;
                    case 't':
                        value += '\t';
                        break;
                    default:
                        value += escaped;
                        break;
                    }
                    continue;
                }
                value += character;
            }

            return std::unexpected{reflectionError(
                "Unterminated string property in shader reflection JSON: " + std::string{name})};
        }

        [[nodiscard]] Result<std::uint32_t> parseUintProperty(std::string_view json,
                                                              std::string_view name) {
            const auto valueOffset =
                findPropertyValue(JsonPropertyQuery{.json = json, .name = name});
            if (!valueOffset) {
                return std::unexpected{reflectionError(
                    "Missing integer property in shader reflection JSON: " + std::string{name})};
            }

            std::size_t endOffset = *valueOffset;
            while (endOffset < json.size() && json[endOffset] >= '0' && json[endOffset] <= '9') {
                ++endOffset;
            }
            if (endOffset == *valueOffset) {
                return std::unexpected{reflectionError(
                    "Invalid integer property in shader reflection JSON: " + std::string{name})};
            }

            std::uint32_t value{};
            const auto* begin = json.data() + *valueOffset;
            const auto* end = json.data() + endOffset;
            const auto result = std::from_chars(begin, end, value);
            if (result.ec != std::errc{}) {
                return std::unexpected{
                    reflectionError("Out-of-range integer property in shader reflection JSON: " +
                                    std::string{name})};
            }

            return value;
        }

        [[nodiscard]] Result<std::string_view> parseArrayProperty(std::string_view json,
                                                                  std::string_view name) {
            const auto valueOffset =
                findPropertyValue(JsonPropertyQuery{.json = json, .name = name});
            if (!valueOffset || *valueOffset >= json.size() || json[*valueOffset] != '[') {
                return std::unexpected{reflectionError(
                    "Missing array property in shader reflection JSON: " + std::string{name})};
            }

            bool inString = false;
            bool escaped = false;
            std::uint32_t depth = 0;
            for (std::size_t offset = *valueOffset; offset < json.size(); ++offset) {
                const char character = json[offset];
                if (inString) {
                    if (escaped) {
                        escaped = false;
                    } else if (character == '\\') {
                        escaped = true;
                    } else if (character == '"') {
                        inString = false;
                    }
                    continue;
                }

                if (character == '"') {
                    inString = true;
                } else if (character == '[') {
                    ++depth;
                } else if (character == ']') {
                    --depth;
                    if (depth == 0) {
                        return json.substr(*valueOffset + 1, offset - *valueOffset - 1);
                    }
                }
            }

            return std::unexpected{reflectionError(
                "Unterminated array property in shader reflection JSON: " + std::string{name})};
        }

        [[nodiscard]] Result<std::vector<std::string_view>> splitObjectArray(JsonArrayQuery query) {
            std::vector<std::string_view> objects;
            bool inString = false;
            bool escaped = false;
            std::uint32_t depth = 0;
            std::size_t objectBegin = std::string_view::npos;

            for (std::size_t offset = 0; offset < query.arrayJson.size(); ++offset) {
                const char character = query.arrayJson[offset];
                if (inString) {
                    if (escaped) {
                        escaped = false;
                    } else if (character == '\\') {
                        escaped = true;
                    } else if (character == '"') {
                        inString = false;
                    }
                    continue;
                }

                if (character == '"') {
                    inString = true;
                } else if (character == '{') {
                    if (depth == 0) {
                        objectBegin = offset;
                    }
                    ++depth;
                } else if (character == '}') {
                    if (depth == 0) {
                        return std::unexpected{
                            reflectionError("Malformed object array in shader reflection JSON: " +
                                            std::string{query.name})};
                    }
                    --depth;
                    if (depth == 0 && objectBegin != std::string_view::npos) {
                        objects.push_back(
                            query.arrayJson.substr(objectBegin, offset - objectBegin + 1));
                        objectBegin = std::string_view::npos;
                    }
                }
            }

            if (depth != 0) {
                return std::unexpected{
                    reflectionError("Unterminated object array in shader reflection JSON: " +
                                    std::string{query.name})};
            }

            return objects;
        }

        [[nodiscard]] Result<ShaderVertexInputReflection> parseVertexInput(std::string_view json) {
            auto name = parseStringProperty(json, "name");
            if (!name) {
                return std::unexpected{std::move(name.error())};
            }
            auto location = parseUintProperty(json, "location");
            if (!location) {
                return std::unexpected{std::move(location.error())};
            }
            auto semantic = parseStringProperty(json, "semantic");
            if (!semantic) {
                return std::unexpected{std::move(semantic.error())};
            }
            auto semanticIndex = parseUintProperty(json, "semanticIndex");
            if (!semanticIndex) {
                return std::unexpected{std::move(semanticIndex.error())};
            }
            auto type = parseStringProperty(json, "type");
            if (!type) {
                return std::unexpected{std::move(type.error())};
            }
            auto scalarType = parseStringProperty(json, "scalarType");
            if (!scalarType) {
                return std::unexpected{std::move(scalarType.error())};
            }
            auto rowCount = parseUintProperty(json, "rowCount");
            if (!rowCount) {
                return std::unexpected{std::move(rowCount.error())};
            }
            auto columnCount = parseUintProperty(json, "columnCount");
            if (!columnCount) {
                return std::unexpected{std::move(columnCount.error())};
            }

            return ShaderVertexInputReflection{
                .name = std::move(*name),
                .location = *location,
                .semantic = std::move(*semantic),
                .semanticIndex = *semanticIndex,
                .type = std::move(*type),
                .scalarType = std::move(*scalarType),
                .rowCount = *rowCount,
                .columnCount = *columnCount,
            };
        }

        [[nodiscard]] Result<ShaderDescriptorBindingReflection>
        parseDescriptorBinding(DescriptorBindingParseQuery query) {
            auto name = parseStringProperty(query.json, "name");
            if (!name) {
                return std::unexpected{std::move(name.error())};
            }
            auto set = parseUintProperty(query.json, "set");
            if (!set) {
                return std::unexpected{std::move(set.error())};
            }
            auto binding = parseUintProperty(query.json, "binding");
            if (!binding) {
                return std::unexpected{std::move(binding.error())};
            }
            auto kind = parseStringProperty(query.json, "kind");
            if (!kind) {
                return std::unexpected{std::move(kind.error())};
            }
            auto count = parseUintProperty(query.json, "count");
            if (!count) {
                return std::unexpected{std::move(count.error())};
            }
            auto category = parseStringProperty(query.json, "category");
            if (!category) {
                return std::unexpected{std::move(category.error())};
            }

            return ShaderDescriptorBindingReflection{
                .name = std::move(*name),
                .set = *set,
                .binding = *binding,
                .kind = std::move(*kind),
                .count = *count,
                .category = std::move(*category),
                .stageVisibility = std::string{query.stageVisibility},
            };
        }

        [[nodiscard]] Result<ShaderPushConstantReflection>
        parsePushConstant(PushConstantParseQuery query) {
            auto name = parseStringProperty(query.json, "name");
            if (!name) {
                return std::unexpected{std::move(name.error())};
            }
            auto offset = parseUintProperty(query.json, "offset");
            if (!offset) {
                return std::unexpected{std::move(offset.error())};
            }
            auto size = parseUintProperty(query.json, "size");
            if (!size) {
                return std::unexpected{std::move(size.error())};
            }
            auto stage = parseStringProperty(query.json, "stage");
            std::string stageVisibility =
                stage ? std::move(*stage) : std::string{query.fallbackStageVisibility};

            return ShaderPushConstantReflection{
                .name = std::move(*name),
                .offset = *offset,
                .size = *size,
                .stageVisibility = std::move(stageVisibility),
            };
        }

        void mergeStageVisibility(std::string& merged, std::string_view stageVisibility) {
            if (stageVisibility.empty()) {
                return;
            }
            if (merged.empty()) {
                merged = std::string{stageVisibility};
                return;
            }

            std::size_t begin = 0;
            const std::string_view mergedView{merged};
            while (begin <= merged.size()) {
                const std::size_t end = merged.find('|', begin);
                const std::size_t tokenEnd = end == std::string::npos ? merged.size() : end;
                const std::string_view token = mergedView.substr(begin, tokenEnd - begin);
                if (token == stageVisibility) {
                    return;
                }
                if (end == std::string::npos) {
                    break;
                }
                begin = end + 1;
            }

            merged += '|';
            merged += stageVisibility;
        }

        void mergeDescriptorBinding(std::vector<ShaderDescriptorBindingReflection>& bindings,
                                    const ShaderDescriptorBindingReflection& binding) {
            for (ShaderDescriptorBindingReflection& existing : bindings) {
                if (existing.set == binding.set && existing.binding == binding.binding) {
                    mergeStageVisibility(existing.stageVisibility, binding.stageVisibility);
                    return;
                }
            }

            bindings.push_back(binding);
        }

        void mergePushConstant(std::vector<ShaderPushConstantReflection>& pushConstants,
                               const ShaderPushConstantReflection& pushConstant) {
            for (ShaderPushConstantReflection& existing : pushConstants) {
                if (existing.offset == pushConstant.offset && existing.size == pushConstant.size) {
                    mergeStageVisibility(existing.stageVisibility, pushConstant.stageVisibility);
                    return;
                }
            }

            pushConstants.push_back(pushConstant);
        }

    } // namespace

    Result<ShaderReflection> readShaderReflection(const std::filesystem::path& path) {
        auto text = readTextFile(path);
        if (!text) {
            return std::unexpected{std::move(text.error())};
        }

        auto source = parseStringProperty(*text, "source");
        if (!source) {
            return std::unexpected{std::move(source.error())};
        }
        auto entry = parseStringProperty(*text, "entry");
        if (!entry) {
            return std::unexpected{std::move(entry.error())};
        }
        auto stage = parseStringProperty(*text, "stage");
        if (!stage) {
            return std::unexpected{std::move(stage.error())};
        }
        auto profile = parseStringProperty(*text, "profile");
        if (!profile) {
            return std::unexpected{std::move(profile.error())};
        }
        auto target = parseStringProperty(*text, "target");
        if (!target) {
            return std::unexpected{std::move(target.error())};
        }

        auto vertexInputArray = parseArrayProperty(*text, "vertexInputs");
        if (!vertexInputArray) {
            return std::unexpected{std::move(vertexInputArray.error())};
        }
        auto vertexInputObjects = splitObjectArray(
            JsonArrayQuery{.arrayJson = *vertexInputArray, .name = "vertexInputs"});
        if (!vertexInputObjects) {
            return std::unexpected{std::move(vertexInputObjects.error())};
        }

        std::vector<ShaderVertexInputReflection> vertexInputs;
        vertexInputs.reserve(vertexInputObjects->size());
        for (const std::string_view object : *vertexInputObjects) {
            auto vertexInput = parseVertexInput(object);
            if (!vertexInput) {
                return std::unexpected{std::move(vertexInput.error())};
            }
            vertexInputs.push_back(std::move(*vertexInput));
        }

        auto descriptorArray = parseArrayProperty(*text, "descriptorBindings");
        if (!descriptorArray) {
            return std::unexpected{std::move(descriptorArray.error())};
        }
        auto descriptorObjects = splitObjectArray(
            JsonArrayQuery{.arrayJson = *descriptorArray, .name = "descriptorBindings"});
        if (!descriptorObjects) {
            return std::unexpected{std::move(descriptorObjects.error())};
        }
        std::vector<ShaderDescriptorBindingReflection> descriptorBindings;
        descriptorBindings.reserve(descriptorObjects->size());
        for (const std::string_view object : *descriptorObjects) {
            auto descriptorBinding = parseDescriptorBinding(
                DescriptorBindingParseQuery{.json = object, .stageVisibility = *stage});
            if (!descriptorBinding) {
                return std::unexpected{std::move(descriptorBinding.error())};
            }
            descriptorBindings.push_back(std::move(*descriptorBinding));
        }

        auto pushConstantArray = parseArrayProperty(*text, "pushConstants");
        if (!pushConstantArray) {
            return std::unexpected{std::move(pushConstantArray.error())};
        }
        auto pushConstantObjects = splitObjectArray(
            JsonArrayQuery{.arrayJson = *pushConstantArray, .name = "pushConstants"});
        if (!pushConstantObjects) {
            return std::unexpected{std::move(pushConstantObjects.error())};
        }
        std::vector<ShaderPushConstantReflection> pushConstants;
        pushConstants.reserve(pushConstantObjects->size());
        for (const std::string_view object : *pushConstantObjects) {
            auto pushConstant = parsePushConstant(
                PushConstantParseQuery{.json = object, .fallbackStageVisibility = *stage});
            if (!pushConstant) {
                return std::unexpected{std::move(pushConstant.error())};
            }
            pushConstants.push_back(std::move(*pushConstant));
        }

        const auto descriptorBindingCount = static_cast<std::uint32_t>(descriptorBindings.size());
        const auto pushConstantCount = static_cast<std::uint32_t>(pushConstants.size());

        return ShaderReflection{
            .source = std::move(*source),
            .entry = std::move(*entry),
            .stage = std::move(*stage),
            .profile = std::move(*profile),
            .target = std::move(*target),
            .vertexInputs = std::move(vertexInputs),
            .descriptorBindings = std::move(descriptorBindings),
            .pushConstants = std::move(pushConstants),
            .descriptorBindingCount = descriptorBindingCount,
            .pushConstantCount = pushConstantCount,
        };
    }

    ShaderResourceSignature shaderResourceSignature(std::span<const ShaderReflection> shaders) {
        ShaderResourceSignature signature;
        for (const ShaderReflection& shader : shaders) {
            for (const ShaderDescriptorBindingReflection& binding : shader.descriptorBindings) {
                mergeDescriptorBinding(signature.descriptorBindings, binding);
            }
            for (const ShaderPushConstantReflection& pushConstant : shader.pushConstants) {
                mergePushConstant(signature.pushConstants, pushConstant);
            }
        }
        signature.descriptorBindingCount =
            static_cast<std::uint32_t>(signature.descriptorBindings.size());
        signature.pushConstantCount = static_cast<std::uint32_t>(signature.pushConstants.size());
        return signature;
    }

} // namespace vke
