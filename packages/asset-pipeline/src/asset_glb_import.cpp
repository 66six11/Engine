#include "asharia/asset_pipeline/asset_glb_import.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <filesystem>
#include <limits>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "asharia/core/error.hpp"

namespace asharia::asset {
    namespace {

        constexpr std::uint32_t kGlbMagic = 0x46546C67U;
        constexpr std::uint32_t kGlbJsonChunkType = 0x4E4F534AU;
        constexpr std::uint32_t kGlbBinChunkType = 0x004E4942U;
        constexpr std::uint32_t kGlbVersion = 2U;
        constexpr std::size_t kGlbHeaderBytes = 12U;
        constexpr std::size_t kGlbChunkHeaderBytes = 8U;
        constexpr float kNormalLengthSquaredEpsilon = 1.0e-20F;
        constexpr float kTransformDeterminantEpsilon = 1.0e-12F;

        struct GlbChunkSpan {
            std::size_t offset{};
            std::size_t size{};
        };

        struct GlbPreflight {
            GlbChunkSpan json;
            GlbChunkSpan bin;
        };

        struct DecodeBudget {
            std::uint64_t bytes{};
        };

        struct AccessorReadRequest {
            std::size_t accessorIndex{};
            std::size_t expectedCount{};
            std::string_view semantic;
        };

        enum class JsonPolicyKey : std::uint8_t {
            None,
            Uri,
            ExtensionsRequired,
            Animations,
            Cameras,
            Camera,
            Lights,
            Skins,
            Sparse,
            Targets,
        };

        enum class JsonExpectation : std::uint8_t {
            FirstObjectKeyOrEnd,
            ObjectKey,
            ObjectColon,
            ObjectValue,
            ObjectCommaOrEnd,
            FirstArrayValueOrEnd,
            ArrayValue,
            ArrayCommaOrEnd,
        };

        struct JsonFrame {
            JsonExpectation expectation{JsonExpectation::FirstObjectKeyOrEnd};
            JsonPolicyKey pendingKey{JsonPolicyKey::None};
            JsonPolicyKey nonEmptyArrayPolicy{JsonPolicyKey::None};
        };

        struct JsonStringScan {
            std::string ascii;
            bool isAscii{true};
        };

        struct JsonHexScanState {
            std::size_t cursor{};
            std::uint16_t value{};
        };

        [[nodiscard]] bool consumeDecodeBudget(DecodeBudget& budget, std::uint64_t elementCount,
                                               std::uint64_t elementBytes,
                                               std::uint64_t limit) noexcept {
            if (budget.bytes > limit || elementBytes == 0U ||
                elementCount > (limit - budget.bytes) / elementBytes) {
                return false;
            }
            budget.bytes += elementCount * elementBytes;
            return true;
        }

        [[nodiscard]] bool accessorFitsBufferView(const fastgltf::Asset& asset,
                                                  const fastgltf::Accessor& accessor,
                                                  std::uint64_t elementBytes) noexcept {
            if (!accessor.bufferViewIndex.has_value() ||
                *accessor.bufferViewIndex >= asset.bufferViews.size() || accessor.count == 0U) {
                return false;
            }
            const fastgltf::BufferView& view = asset.bufferViews[*accessor.bufferViewIndex];
            if (view.bufferIndex >= asset.buffers.size()) {
                return false;
            }
            const auto* array =
                std::get_if<fastgltf::sources::Array>(&asset.buffers[view.bufferIndex].data);
            if (array == nullptr || view.byteOffset > array->bytes.size_bytes() ||
                view.byteLength > array->bytes.size_bytes() - view.byteOffset ||
                accessor.byteOffset > view.byteLength ||
                elementBytes > view.byteLength - accessor.byteOffset) {
                return false;
            }
            const std::uint64_t stride = view.byteStride.value_or(elementBytes);
            return stride >= elementBytes &&
                   accessor.count - 1U <=
                       (view.byteLength - accessor.byteOffset - elementBytes) / stride;
        }

        [[nodiscard]] std::string sourceLabel(const SourceAssetRecord& source) {
            return source.sourcePath.empty() ? std::string{"<unspecified-source>"}
                                             : source.sourcePath;
        }

        [[nodiscard]] Error glbImportError(AssetGlbImportDiagnosticCode code,
                                           const SourceAssetRecord& source, std::string message) {
            return Error{ErrorDomain::Asset, static_cast<int>(code),
                         "Restricted GLB import " + sourceLabel(source) + " " + std::move(message) +
                             "."};
        }

        [[nodiscard]] bool finite(float value) noexcept {
            return std::isfinite(value);
        }

        [[nodiscard]] float canonicalFloat(float value) noexcept {
            return value == 0.0F ? 0.0F : value;
        }

        [[nodiscard]] bool finite(const fastgltf::math::fvec2& value) noexcept {
            return finite(value[0]) && finite(value[1]);
        }

        [[nodiscard]] bool finite(const fastgltf::math::fvec3& value) noexcept {
            return finite(value[0]) && finite(value[1]) && finite(value[2]);
        }

        [[nodiscard]] bool finite(const fastgltf::math::fmat4x4& matrix) noexcept {
            for (std::size_t column = 0; column < 4U; ++column) {
                for (std::size_t row = 0; row < 4U; ++row) {
                    if (!finite(matrix[column][row])) {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] std::uint32_t readUint32Le(std::span<const std::uint8_t> bytes,
                                                 std::size_t offset) noexcept {
            return static_cast<std::uint32_t>(bytes[offset]) |
                   (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
                   (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
                   (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
        }

        struct JsonPolicyFindings {
            bool uri{};
            bool extensionsRequired{};
            bool animations{};
            bool cameraOrLight{};
            bool skins{};
            bool sparse{};
            bool targets{};
        };

        struct JsonPolicyScanState {
            std::span<const std::uint8_t> json;
            std::size_t cursor{};
            std::uint32_t maxDepth{};
            const SourceAssetRecord* source{};
            std::vector<JsonFrame> frames;
            JsonPolicyFindings findings;
        };

        enum class JsonScanError : std::uint8_t {
            None,
            Syntax,
            NestingLimit,
        };

        [[nodiscard]] bool jsonWhitespace(std::uint8_t byte) noexcept {
            return byte == static_cast<std::uint8_t>(' ') ||
                   byte == static_cast<std::uint8_t>('\t') ||
                   byte == static_cast<std::uint8_t>('\r') ||
                   byte == static_cast<std::uint8_t>('\n');
        }

        void skipJsonWhitespace(std::span<const std::uint8_t> json, std::size_t& cursor) noexcept {
            while (cursor < json.size() && jsonWhitespace(json[cursor])) {
                ++cursor;
            }
        }

        [[nodiscard]] int jsonHexDigit(std::uint8_t byte) noexcept {
            if (byte >= static_cast<std::uint8_t>('0') && byte <= static_cast<std::uint8_t>('9')) {
                return byte - static_cast<std::uint8_t>('0');
            }
            if (byte >= static_cast<std::uint8_t>('a') && byte <= static_cast<std::uint8_t>('f')) {
                return byte - static_cast<std::uint8_t>('a') + 10;
            }
            if (byte >= static_cast<std::uint8_t>('A') && byte <= static_cast<std::uint8_t>('F')) {
                return byte - static_cast<std::uint8_t>('A') + 10;
            }
            return -1;
        }

        [[nodiscard]] bool scanJsonHexQuad(std::span<const std::uint8_t> json,
                                           JsonHexScanState& state) noexcept {
            if (state.cursor > json.size() || json.size() - state.cursor < 4U) {
                return false;
            }
            state.value = 0U;
            for (std::size_t digitIndex = 0U; digitIndex < 4U; ++digitIndex) {
                const int digit = jsonHexDigit(json[state.cursor++]);
                if (digit < 0) {
                    return false;
                }
                state.value = static_cast<std::uint16_t>((state.value << 4U) |
                                                         static_cast<std::uint16_t>(digit));
            }
            return true;
        }

        void captureJsonKeyByte(JsonStringScan& scan, std::uint8_t byte) {
            constexpr std::size_t kMaxPolicyKeyBytes = 32U;
            if (!scan.isAscii || byte > 0x7FU || scan.ascii.size() >= kMaxPolicyKeyBytes) {
                scan.isAscii = false;
                return;
            }
            scan.ascii.push_back(static_cast<char>(byte));
        }

        [[nodiscard]] bool scanJsonUnicodeEscape(std::span<const std::uint8_t> json,
                                                 std::size_t& cursor, JsonStringScan& scan) {
            JsonHexScanState codeUnit{.cursor = cursor};
            if (!scanJsonHexQuad(json, codeUnit)) {
                return false;
            }
            cursor = codeUnit.cursor;
            if (codeUnit.value >= 0xD800U && codeUnit.value <= 0xDBFFU) {
                if (cursor > json.size() || json.size() - cursor < 6U ||
                    json[cursor] != static_cast<std::uint8_t>('\\') ||
                    json[cursor + 1U] != static_cast<std::uint8_t>('u')) {
                    return false;
                }
                JsonHexScanState lowSurrogate{.cursor = cursor + 2U};
                if (!scanJsonHexQuad(json, lowSurrogate) || lowSurrogate.value < 0xDC00U ||
                    lowSurrogate.value > 0xDFFFU) {
                    return false;
                }
                cursor = lowSurrogate.cursor;
                scan.isAscii = false;
                return true;
            }
            if (codeUnit.value >= 0xDC00U && codeUnit.value <= 0xDFFFU) {
                return false;
            }
            if (codeUnit.value <= 0x7FU) {
                captureJsonKeyByte(scan, static_cast<std::uint8_t>(codeUnit.value));
            } else {
                scan.isAscii = false;
            }
            return true;
        }

        [[nodiscard]] bool scanJsonEscape(std::span<const std::uint8_t> json, std::size_t& cursor,
                                          JsonStringScan& scan) {
            if (cursor >= json.size()) {
                return false;
            }
            const std::uint8_t escape = json[cursor++];
            switch (escape) {
            case static_cast<std::uint8_t>('"'):
            case static_cast<std::uint8_t>('\\'):
            case static_cast<std::uint8_t>('/'):
                captureJsonKeyByte(scan, escape);
                return true;
            case static_cast<std::uint8_t>('b'):
                captureJsonKeyByte(scan, 0x08U);
                return true;
            case static_cast<std::uint8_t>('f'):
                captureJsonKeyByte(scan, 0x0CU);
                return true;
            case static_cast<std::uint8_t>('n'):
                captureJsonKeyByte(scan, static_cast<std::uint8_t>('\n'));
                return true;
            case static_cast<std::uint8_t>('r'):
                captureJsonKeyByte(scan, static_cast<std::uint8_t>('\r'));
                return true;
            case static_cast<std::uint8_t>('t'):
                captureJsonKeyByte(scan, static_cast<std::uint8_t>('\t'));
                return true;
            case static_cast<std::uint8_t>('u'):
                return scanJsonUnicodeEscape(json, cursor, scan);
            default:
                return false;
            }
        }

        [[nodiscard]] bool scanJsonString(std::span<const std::uint8_t> json, std::size_t& cursor,
                                          JsonStringScan& scan) {
            if (cursor >= json.size() || json[cursor] != static_cast<std::uint8_t>('"')) {
                return false;
            }
            ++cursor;
            while (cursor < json.size()) {
                const std::uint8_t byte = json[cursor++];
                if (byte == static_cast<std::uint8_t>('"')) {
                    return true;
                }
                if (byte < 0x20U) {
                    return false;
                }
                if (byte != static_cast<std::uint8_t>('\\')) {
                    captureJsonKeyByte(scan, byte);
                    continue;
                }
                if (!scanJsonEscape(json, cursor, scan)) {
                    return false;
                }
            }
            return false;
        }

        [[nodiscard]] bool jsonDigit(std::uint8_t byte) noexcept {
            return byte >= static_cast<std::uint8_t>('0') && byte <= static_cast<std::uint8_t>('9');
        }

        [[nodiscard]] bool scanRequiredJsonDigits(std::span<const std::uint8_t> json,
                                                  std::size_t& cursor) noexcept {
            const std::size_t start = cursor;
            while (cursor < json.size() && jsonDigit(json[cursor])) {
                ++cursor;
            }
            return cursor != start;
        }

        [[nodiscard]] bool scanJsonFraction(std::span<const std::uint8_t> json,
                                            std::size_t& cursor) noexcept {
            if (cursor >= json.size() || json[cursor] != static_cast<std::uint8_t>('.')) {
                return true;
            }
            ++cursor;
            return scanRequiredJsonDigits(json, cursor);
        }

        [[nodiscard]] bool scanJsonExponent(std::span<const std::uint8_t> json,
                                            std::size_t& cursor) noexcept {
            if (cursor >= json.size() || (json[cursor] != static_cast<std::uint8_t>('e') &&
                                          json[cursor] != static_cast<std::uint8_t>('E'))) {
                return true;
            }
            ++cursor;
            if (cursor < json.size() && (json[cursor] == static_cast<std::uint8_t>('+') ||
                                         json[cursor] == static_cast<std::uint8_t>('-'))) {
                ++cursor;
            }
            return scanRequiredJsonDigits(json, cursor);
        }

        [[nodiscard]] bool scanJsonNumber(std::span<const std::uint8_t> json,
                                          std::size_t& cursor) noexcept {
            if (cursor < json.size() && json[cursor] == static_cast<std::uint8_t>('-')) {
                ++cursor;
            }
            if (cursor >= json.size()) {
                return false;
            }
            if (json[cursor] == static_cast<std::uint8_t>('0')) {
                ++cursor;
                if (cursor < json.size() && jsonDigit(json[cursor])) {
                    return false;
                }
            } else if (json[cursor] >= static_cast<std::uint8_t>('1') &&
                       json[cursor] <= static_cast<std::uint8_t>('9')) {
                static_cast<void>(scanRequiredJsonDigits(json, cursor));
            } else {
                return false;
            }
            return scanJsonFraction(json, cursor) && scanJsonExponent(json, cursor);
        }

        [[nodiscard]] bool consumeJsonLiteral(std::span<const std::uint8_t> json,
                                              std::size_t& cursor,
                                              std::string_view literal) noexcept {
            if (cursor > json.size() || literal.size() > json.size() - cursor) {
                return false;
            }
            for (const char character : literal) {
                if (json[cursor++] != static_cast<std::uint8_t>(character)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] JsonPolicyKey classifyJsonPolicyKey(const JsonStringScan& scan,
                                                          bool rootMember) noexcept {
            if (!scan.isAscii) {
                return JsonPolicyKey::None;
            }
            const std::string_view key = scan.ascii;
            if (key == "uri") {
                return JsonPolicyKey::Uri;
            }
            if (key == "camera") {
                return JsonPolicyKey::Camera;
            }
            if (key == "KHR_lights_punctual") {
                return JsonPolicyKey::Lights;
            }
            if (key == "sparse") {
                return JsonPolicyKey::Sparse;
            }
            if (key == "targets") {
                return JsonPolicyKey::Targets;
            }
            if (!rootMember) {
                return JsonPolicyKey::None;
            }
            if (key == "extensionsRequired") {
                return JsonPolicyKey::ExtensionsRequired;
            }
            if (key == "animations") {
                return JsonPolicyKey::Animations;
            }
            if (key == "cameras") {
                return JsonPolicyKey::Cameras;
            }
            if (key == "skins") {
                return JsonPolicyKey::Skins;
            }
            return JsonPolicyKey::None;
        }

        void recordJsonMember(JsonPolicyFindings& findings, JsonPolicyKey key) noexcept {
            switch (key) {
            case JsonPolicyKey::Uri:
                findings.uri = true;
                break;
            case JsonPolicyKey::Camera:
            case JsonPolicyKey::Lights:
                findings.cameraOrLight = true;
                break;
            case JsonPolicyKey::Sparse:
                findings.sparse = true;
                break;
            case JsonPolicyKey::Targets:
                findings.targets = true;
                break;
            case JsonPolicyKey::None:
            case JsonPolicyKey::ExtensionsRequired:
            case JsonPolicyKey::Animations:
            case JsonPolicyKey::Cameras:
            case JsonPolicyKey::Skins:
                break;
            }
        }

        void recordNonEmptyJsonArray(JsonPolicyFindings& findings, JsonPolicyKey key) noexcept {
            switch (key) {
            case JsonPolicyKey::ExtensionsRequired:
                findings.extensionsRequired = true;
                break;
            case JsonPolicyKey::Animations:
                findings.animations = true;
                break;
            case JsonPolicyKey::Cameras:
                findings.cameraOrLight = true;
                break;
            case JsonPolicyKey::Skins:
                findings.skins = true;
                break;
            case JsonPolicyKey::None:
            case JsonPolicyKey::Uri:
            case JsonPolicyKey::Camera:
            case JsonPolicyKey::Lights:
            case JsonPolicyKey::Sparse:
            case JsonPolicyKey::Targets:
                break;
            }
        }

        [[nodiscard]] JsonScanError beginJsonValue(std::span<const std::uint8_t> json,
                                                   std::size_t& cursor,
                                                   std::vector<JsonFrame>& frames,
                                                   std::uint32_t maxDepth,
                                                   JsonPolicyKey arrayPolicy) {
            if (cursor >= json.size()) {
                return JsonScanError::Syntax;
            }
            const std::uint8_t byte = json[cursor];
            if (byte == static_cast<std::uint8_t>('{') || byte == static_cast<std::uint8_t>('[')) {
                if (frames.size() >= maxDepth) {
                    return JsonScanError::NestingLimit;
                }
                ++cursor;
                const bool object = byte == static_cast<std::uint8_t>('{');
                frames.push_back(JsonFrame{
                    .expectation = object ? JsonExpectation::FirstObjectKeyOrEnd
                                          : JsonExpectation::FirstArrayValueOrEnd,
                    .pendingKey = JsonPolicyKey::None,
                    .nonEmptyArrayPolicy = object ? JsonPolicyKey::None : arrayPolicy,
                });
                return JsonScanError::None;
            }
            if (byte == static_cast<std::uint8_t>('"')) {
                JsonStringScan ignored{.ascii = {}, .isAscii = false};
                return scanJsonString(json, cursor, ignored) ? JsonScanError::None
                                                             : JsonScanError::Syntax;
            }
            if (byte == static_cast<std::uint8_t>('t')) {
                return consumeJsonLiteral(json, cursor, "true") ? JsonScanError::None
                                                                : JsonScanError::Syntax;
            }
            if (byte == static_cast<std::uint8_t>('f')) {
                return consumeJsonLiteral(json, cursor, "false") ? JsonScanError::None
                                                                 : JsonScanError::Syntax;
            }
            if (byte == static_cast<std::uint8_t>('n')) {
                return consumeJsonLiteral(json, cursor, "null") ? JsonScanError::None
                                                                : JsonScanError::Syntax;
            }
            return scanJsonNumber(json, cursor) ? JsonScanError::None : JsonScanError::Syntax;
        }

        [[nodiscard]] Error jsonSyntaxError(const JsonPolicyScanState& state) {
            return glbImportError(AssetGlbImportDiagnosticCode::InvalidJson, *state.source,
                                  "contains invalid JSON near byte " +
                                      std::to_string(state.cursor));
        }

        [[nodiscard]] Result<void> finishJsonValue(JsonPolicyScanState& state,
                                                   JsonPolicyKey arrayPolicy) {
            const JsonScanError error =
                beginJsonValue(state.json, state.cursor, state.frames, state.maxDepth, arrayPolicy);
            if (error == JsonScanError::NestingLimit) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::CountLimitExceeded, *state.source,
                    "JSON nesting exceeds configured limit " + std::to_string(state.maxDepth))};
            }
            if (error == JsonScanError::Syntax) {
                return std::unexpected{jsonSyntaxError(state)};
            }
            return {};
        }

        [[nodiscard]] Result<void> scanObjectKey(JsonPolicyScanState& state) {
            JsonStringScan key;
            if (!scanJsonString(state.json, state.cursor, key)) {
                return std::unexpected{jsonSyntaxError(state)};
            }
            JsonFrame& frame = state.frames.back();
            frame.pendingKey = classifyJsonPolicyKey(key, state.frames.size() == 1U);
            recordJsonMember(state.findings, frame.pendingKey);
            frame.expectation = JsonExpectation::ObjectColon;
            return {};
        }

        [[nodiscard]] Result<void> scanObjectColon(JsonPolicyScanState& state) {
            if (state.cursor >= state.json.size() ||
                state.json[state.cursor] != static_cast<std::uint8_t>(':')) {
                return std::unexpected{jsonSyntaxError(state)};
            }
            ++state.cursor;
            state.frames.back().expectation = JsonExpectation::ObjectValue;
            return {};
        }

        [[nodiscard]] Result<void> scanObjectValue(JsonPolicyScanState& state) {
            JsonFrame& frame = state.frames.back();
            const JsonPolicyKey arrayPolicy = frame.pendingKey;
            frame.pendingKey = JsonPolicyKey::None;
            frame.expectation = JsonExpectation::ObjectCommaOrEnd;
            return finishJsonValue(state, arrayPolicy);
        }

        [[nodiscard]] Result<void> scanObjectSeparator(JsonPolicyScanState& state) {
            if (state.cursor < state.json.size() &&
                state.json[state.cursor] == static_cast<std::uint8_t>('}')) {
                ++state.cursor;
                state.frames.pop_back();
                return {};
            }
            if (state.cursor < state.json.size() &&
                state.json[state.cursor] == static_cast<std::uint8_t>(',')) {
                ++state.cursor;
                state.frames.back().expectation = JsonExpectation::ObjectKey;
                return {};
            }
            return std::unexpected{jsonSyntaxError(state)};
        }

        [[nodiscard]] Result<void> scanArrayValue(JsonPolicyScanState& state) {
            JsonFrame& frame = state.frames.back();
            recordNonEmptyJsonArray(state.findings, frame.nonEmptyArrayPolicy);
            frame.nonEmptyArrayPolicy = JsonPolicyKey::None;
            frame.expectation = JsonExpectation::ArrayCommaOrEnd;
            return finishJsonValue(state, JsonPolicyKey::None);
        }

        [[nodiscard]] Result<void> scanArraySeparator(JsonPolicyScanState& state) {
            if (state.cursor < state.json.size() &&
                state.json[state.cursor] == static_cast<std::uint8_t>(']')) {
                ++state.cursor;
                state.frames.pop_back();
                return {};
            }
            if (state.cursor < state.json.size() &&
                state.json[state.cursor] == static_cast<std::uint8_t>(',')) {
                ++state.cursor;
                state.frames.back().expectation = JsonExpectation::ArrayValue;
                return {};
            }
            return std::unexpected{jsonSyntaxError(state)};
        }

        [[nodiscard]] Result<void> scanJsonStep(JsonPolicyScanState& state) {
            JsonFrame& frame = state.frames.back();
            switch (frame.expectation) {
            case JsonExpectation::FirstObjectKeyOrEnd:
                if (state.cursor < state.json.size() &&
                    state.json[state.cursor] == static_cast<std::uint8_t>('}')) {
                    ++state.cursor;
                    state.frames.pop_back();
                    return {};
                }
                return scanObjectKey(state);
            case JsonExpectation::ObjectKey:
                return scanObjectKey(state);
            case JsonExpectation::ObjectColon:
                return scanObjectColon(state);
            case JsonExpectation::ObjectValue:
                return scanObjectValue(state);
            case JsonExpectation::ObjectCommaOrEnd:
                return scanObjectSeparator(state);
            case JsonExpectation::FirstArrayValueOrEnd:
                if (state.cursor < state.json.size() &&
                    state.json[state.cursor] == static_cast<std::uint8_t>(']')) {
                    ++state.cursor;
                    state.frames.pop_back();
                    return {};
                }
                return scanArrayValue(state);
            case JsonExpectation::ArrayValue:
                return scanArrayValue(state);
            case JsonExpectation::ArrayCommaOrEnd:
                return scanArraySeparator(state);
            }
            return std::unexpected{jsonSyntaxError(state)};
        }

        [[nodiscard]] Result<JsonPolicyFindings> scanJsonPolicy(std::span<const std::uint8_t> json,
                                                                std::uint32_t maxDepth,
                                                                const SourceAssetRecord& source) {
            std::size_t cursor = 0U;
            skipJsonWhitespace(json, cursor);
            if (cursor >= json.size() || json[cursor] != static_cast<std::uint8_t>('{')) {
                return std::unexpected{glbImportError(AssetGlbImportDiagnosticCode::InvalidJson,
                                                      source, "JSON root is not an object")};
            }
            ++cursor;

            JsonPolicyScanState state{
                .json = json,
                .cursor = cursor,
                .maxDepth = maxDepth,
                .source = &source,
                .frames = {},
                .findings = {},
            };
            state.frames.reserve((std::min)(maxDepth, 256U));
            state.frames.push_back(JsonFrame{});
            while (!state.frames.empty()) {
                skipJsonWhitespace(json, state.cursor);
                if (auto step = scanJsonStep(state); !step) {
                    return std::unexpected{std::move(step.error())};
                }
            }
            skipJsonWhitespace(json, state.cursor);
            if (state.cursor != json.size()) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::InvalidJson, source,
                                   "contains trailing non-whitespace JSON bytes near byte " +
                                       std::to_string(state.cursor))};
            }
            return state.findings;
        }

        [[nodiscard]] Result<GlbPreflight> preflightGlb(const AssetGlbImportRequest& request) {
            const std::span<const std::uint8_t> bytes = request.sourceBytes;
            if (bytes.size() > request.limits.maxSourceBytes) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::SourceByteLimitExceeded, request.source,
                    "has " + std::to_string(bytes.size()) +
                        " source bytes, exceeding configured limit " +
                        std::to_string(request.limits.maxSourceBytes))};
            }
            if (bytes.size() < kGlbHeaderBytes + kGlbChunkHeaderBytes) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::InvalidGlb, request.source,
                                   "is shorter than a GLB header and JSON chunk")};
            }

            const std::uint32_t magic = readUint32Le(bytes, 0U);
            const std::uint32_t version = readUint32Le(bytes, 4U);
            const std::uint32_t declaredLength = readUint32Le(bytes, 8U);
            if (magic != kGlbMagic) {
                return std::unexpected{glbImportError(AssetGlbImportDiagnosticCode::InvalidGlb,
                                                      request.source,
                                                      "does not have the GLB magic value")};
            }
            if (version != kGlbVersion) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::InvalidGlb, request.source,
                    "uses unsupported GLB container version " + std::to_string(version))};
            }
            if (declaredLength != bytes.size()) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::InvalidGlb, request.source,
                                   "declares length " + std::to_string(declaredLength) +
                                       " but contains " + std::to_string(bytes.size()) + " bytes")};
            }

            std::size_t cursor = kGlbHeaderBytes;
            const std::uint32_t jsonSize = readUint32Le(bytes, cursor);
            const std::uint32_t jsonType = readUint32Le(bytes, cursor + 4U);
            cursor += kGlbChunkHeaderBytes;
            if (jsonType != kGlbJsonChunkType || jsonSize % 4U != 0U ||
                jsonSize > bytes.size() - cursor) {
                return std::unexpected{glbImportError(AssetGlbImportDiagnosticCode::InvalidGlb,
                                                      request.source,
                                                      "has an invalid first JSON chunk")};
            }
            if (jsonSize > request.limits.maxJsonBytes) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::JsonByteLimitExceeded, request.source,
                    "has a JSON chunk of " + std::to_string(jsonSize) +
                        " bytes, exceeding configured limit " +
                        std::to_string(request.limits.maxJsonBytes))};
            }
            const GlbChunkSpan json{.offset = cursor, .size = jsonSize};
            cursor += jsonSize;

            if (cursor + kGlbChunkHeaderBytes > bytes.size()) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::UnsupportedBufferLayout, request.source,
                    "does not contain the single required BIN chunk")};
            }
            const std::uint32_t binSize = readUint32Le(bytes, cursor);
            const std::uint32_t binType = readUint32Le(bytes, cursor + 4U);
            cursor += kGlbChunkHeaderBytes;
            if (binType != kGlbBinChunkType || binSize % 4U != 0U ||
                binSize > bytes.size() - cursor) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::UnsupportedBufferLayout,
                                   request.source, "has an invalid BIN chunk")};
            }
            const GlbChunkSpan bin{.offset = cursor, .size = binSize};
            cursor += binSize;
            if (cursor != bytes.size()) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::UnsupportedBufferLayout,
                                   request.source, "contains chunks after the single BIN chunk")};
            }

            auto findings = scanJsonPolicy(bytes.subspan(json.offset, json.size),
                                           request.limits.maxJsonNestingDepth, request.source);
            if (!findings) {
                return std::unexpected{std::move(findings.error())};
            }
            if (findings->uri) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::ExternalUriUnsupported, request.source,
                    "contains a URI; this importer accepts only BIN-backed geometry")};
            }
            if (findings->extensionsRequired) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::RequiredExtensionUnsupported,
                                   request.source, "declares one or more required extensions")};
            }
            if (findings->animations) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::AnimationUnsupported, request.source,
                    "contains animations, which are outside the static mesh subset")};
            }
            if (findings->cameraOrLight) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::SceneSemanticUnsupported, request.source,
                    "contains camera or light scene semantics outside the static mesh subset")};
            }
            if (findings->skins) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::SkinUnsupported, request.source,
                                   "contains skins, which are outside the static mesh subset")};
            }
            if (findings->sparse) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::SparseAccessorUnsupported,
                                   request.source, "contains a sparse accessor")};
            }
            if (findings->targets) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::MorphTargetUnsupported,
                                   request.source, "contains morph targets")};
            }

            return GlbPreflight{.json = json, .bin = bin};
        }

        [[nodiscard]] Result<void> validateLimits(const AssetGlbImportRequest& request) {
            const AssetGlbImportLimits& limits = request.limits;
            if (limits.maxSourceBytes < kGlbHeaderBytes + kGlbChunkHeaderBytes ||
                limits.maxJsonBytes == 0U || limits.maxJsonNestingDepth == 0U ||
                limits.maxNodes == 0U || limits.maxNodeDepth == 0U || limits.maxMeshes == 0U ||
                limits.maxPrimitives == 0U || limits.maxMaterialSlots == 0U ||
                limits.maxVertices == 0U || limits.maxIndices == 0U ||
                limits.maxDecodedBytes == 0U) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::InvalidRequest, request.source,
                                   "has one or more zero or unusably small import limits")};
            }
            return {};
        }

        [[nodiscard]] std::string lowerAscii(std::string_view value) {
            std::string result;
            result.reserve(value.size());
            for (char character : value) {
                if (character >= 'A' && character <= 'Z') {
                    character = static_cast<char>(character - 'A' + 'a');
                }
                result.push_back(character);
            }
            return result;
        }

        [[nodiscard]] std::string sourceExtension(std::string_view sourcePath) {
            const std::size_t slash = sourcePath.rfind('/');
            const std::size_t dot = sourcePath.rfind('.');
            if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
                return {};
            }
            return lowerAscii(sourcePath.substr(dot));
        }

        [[nodiscard]] bool asciiEqualIgnoreCase(std::string_view left,
                                                std::string_view right) noexcept {
            if (left.size() != right.size()) {
                return false;
            }
            for (std::size_t index = 0U; index < left.size(); ++index) {
                const auto lower = [](char character) noexcept {
                    return character >= 'A' && character <= 'Z'
                               ? static_cast<char>(character - 'A' + 'a')
                               : character;
                };
                if (lower(left[index]) != lower(right[index])) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool hasSourceExtension(std::string_view sourcePath,
                                              std::string_view extension) noexcept {
            const std::size_t slash = sourcePath.rfind('/');
            const std::size_t dot = sourcePath.rfind('.');
            return dot != std::string_view::npos &&
                   (slash == std::string_view::npos || dot > slash) &&
                   asciiEqualIgnoreCase(sourcePath.substr(dot), extension);
        }

        [[nodiscard]] Result<void> validateRequest(const AssetGlbImportRequest& request) {
            if (auto limits = validateLimits(request); !limits) {
                return limits;
            }
            if (!request.source) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::InvalidRequest, request.source,
                                   "does not contain a valid source asset record")};
            }
            if (request.source.assetTypeName != kGlbMeshAssetTypeName ||
                request.source.assetType != makeAssetTypeId(kGlbMeshAssetTypeName)) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::InvalidRequest, request.source,
                    "does not select mesh asset type " + std::string{kGlbMeshAssetTypeName})};
            }
            if (!request.settings.empty()) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::InvalidRequest, request.source,
                    "contains import settings, but importer version 1 defines no settings")};
            }
            if (sourceExtension(request.source.sourcePath) != kGlbMeshSourceExtension) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::UnsupportedSourceExtension,
                                   request.source, "does not use the supported .glb extension")};
            }
            if (request.source.importerId != makeImporterId(kGlbMeshImporterName) ||
                request.source.importerName != kGlbMeshImporterName ||
                request.source.importerVersion != kGlbMeshImporterVersion) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::InvalidRequest, request.source,
                    "does not select importer " + std::string{kGlbMeshImporterName} + " version " +
                        std::to_string(kGlbMeshImporterVersion.value))};
            }
            return {};
        }

        [[nodiscard]] AssetGlbImportDiagnosticCode
        fastGltfDiagnostic(fastgltf::Error error) noexcept {
            switch (error) {
            case fastgltf::Error::MissingExtensions:
            case fastgltf::Error::UnknownRequiredExtension:
                return AssetGlbImportDiagnosticCode::RequiredExtensionUnsupported;
            case fastgltf::Error::InvalidJson:
                return AssetGlbImportDiagnosticCode::InvalidJson;
            case fastgltf::Error::UnsupportedVersion:
            case fastgltf::Error::InvalidGLB:
            case fastgltf::Error::InvalidFileData:
            case fastgltf::Error::InvalidGltf:
            case fastgltf::Error::InvalidOrMissingAssetField:
            case fastgltf::Error::MissingField:
            case fastgltf::Error::InvalidURI:
            case fastgltf::Error::MissingExternalBuffer:
            case fastgltf::Error::InvalidPath:
            case fastgltf::Error::FileBufferAllocationFailed:
            case fastgltf::Error::FailedWritingFiles:
            case fastgltf::Error::None:
                return AssetGlbImportDiagnosticCode::InvalidGlb;
            }
            return AssetGlbImportDiagnosticCode::InvalidGlb;
        }

        [[nodiscard]] Result<fastgltf::Asset> parseAsset(const AssetGlbImportRequest& request) {
            const auto sourceBytes = std::as_bytes(std::span{request.sourceBytes});
            auto data = fastgltf::GltfDataBuffer::FromBytes(sourceBytes.data(), sourceBytes.size());
            if (!data) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::InvalidGlb, request.source,
                                   "could not allocate parser input: " +
                                       std::string{fastgltf::getErrorMessage(data.error())})};
            }

            fastgltf::Parser parser{fastgltf::Extensions::None};
            auto parsed = parser.loadGltfBinary(data.get(), std::filesystem::path{},
                                                fastgltf::Options::None, fastgltf::Category::All);
            if (!parsed) {
                return std::unexpected{
                    glbImportError(fastGltfDiagnostic(parsed.error()), request.source,
                                   "failed glTF 2.x validation: " +
                                       std::string{fastgltf::getErrorMessage(parsed.error())})};
            }
            if (const fastgltf::Error validation = fastgltf::validate(parsed.get());
                validation != fastgltf::Error::None) {
                return std::unexpected{
                    glbImportError(fastGltfDiagnostic(validation), request.source,
                                   "failed glTF structural validation: " +
                                       std::string{fastgltf::getErrorMessage(validation)})};
            }
            return std::move(parsed.get());
        }

        [[nodiscard]] Result<void> validateAssetTopLevel(const fastgltf::Asset& asset,
                                                         const AssetGlbImportRequest& request) {
            if (!asset.extensionsRequired.empty()) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::RequiredExtensionUnsupported,
                                   request.source, "declares one or more required extensions")};
            }
            if (asset.buffers.size() != 1U ||
                !std::holds_alternative<fastgltf::sources::Array>(asset.buffers.front().data)) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::UnsupportedBufferLayout,
                                   request.source, "must contain exactly one BIN-backed buffer")};
            }
            if (!asset.animations.empty()) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::AnimationUnsupported,
                                   request.source, "contains animations")};
            }
            if (!asset.cameras.empty() || !asset.lights.empty()) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::SceneSemanticUnsupported,
                                   request.source, "contains camera or light scene semantics")};
            }
            if (!asset.skins.empty()) {
                return std::unexpected{glbImportError(AssetGlbImportDiagnosticCode::SkinUnsupported,
                                                      request.source, "contains skins")};
            }
            if (!asset.defaultScene.has_value() || *asset.defaultScene >= asset.scenes.size()) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::MissingDefaultScene,
                                   request.source, "does not select a valid default scene")};
            }
            if (asset.nodes.size() > request.limits.maxNodes ||
                asset.meshes.size() > request.limits.maxMeshes) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                                   "exceeds configured node or mesh count limits")};
            }
            if (asset.materials.size() >= request.limits.maxMaterialSlots) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                    "needs " + std::to_string(asset.materials.size() + 1U) +
                        " material slots including the default slot, exceeding configured limit " +
                        std::to_string(request.limits.maxMaterialSlots))};
            }
            return {};
        }

        [[nodiscard]] Result<std::vector<std::uint32_t>>
        validateNodeHierarchy(const fastgltf::Asset& asset, const AssetGlbImportRequest& request) {
            constexpr std::uint64_t kHierarchyBytesPerNode =
                (sizeof(std::uint32_t) * 2U) + sizeof(std::size_t) + sizeof(bool);
            if (asset.nodes.size() > request.limits.maxDecodedBytes / kHierarchyBytesPerNode) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                    "node hierarchy validation exceeds configured decoded working-byte limit")};
            }
            std::vector<std::uint32_t> parentCounts(asset.nodes.size(), 0U);
            for (const fastgltf::Node& node : asset.nodes) {
                for (const std::size_t child : node.children) {
                    if (child >= asset.nodes.size()) {
                        return std::unexpected{glbImportError(
                            AssetGlbImportDiagnosticCode::InvalidAccessor, request.source,
                            "node hierarchy references a child outside the asset")};
                    }
                    if (++parentCounts[child] > 1U) {
                        return std::unexpected{
                            glbImportError(AssetGlbImportDiagnosticCode::InvalidGlb, request.source,
                                           "node hierarchy gives one node more than one parent")};
                    }
                }
            }
            std::vector<std::uint32_t> remainingParents = parentCounts;
            std::vector<std::size_t> roots;
            roots.reserve(asset.nodes.size());
            for (std::size_t index = 0U; index < remainingParents.size(); ++index) {
                if (remainingParents[index] == 0U) {
                    roots.push_back(index);
                }
            }
            std::size_t rootCursor = 0U;
            std::size_t visitedNodes = 0U;
            while (rootCursor < roots.size()) {
                const std::size_t nodeIndex = roots[rootCursor++];
                ++visitedNodes;
                for (const std::size_t child : asset.nodes[nodeIndex].children) {
                    if (--remainingParents[child] == 0U) {
                        roots.push_back(child);
                    }
                }
            }
            if (visitedNodes != asset.nodes.size()) {
                return std::unexpected{glbImportError(AssetGlbImportDiagnosticCode::InvalidGlb,
                                                      request.source,
                                                      "node hierarchy contains a cycle")};
            }
            return parentCounts;
        }

        [[nodiscard]] Result<void>
        validateDefaultSceneRoots(const fastgltf::Asset& asset,
                                  const std::vector<std::uint32_t>& parentCounts,
                                  const AssetGlbImportRequest& request) {
            std::vector<bool> defaultRoots(asset.nodes.size(), false);
            for (const std::size_t nodeIndex : asset.scenes[*asset.defaultScene].nodeIndices) {
                if (nodeIndex >= asset.nodes.size()) {
                    return std::unexpected{glbImportError(
                        AssetGlbImportDiagnosticCode::InvalidAccessor, request.source,
                        "default scene references a node outside the asset")};
                }
                if (defaultRoots[nodeIndex]) {
                    return std::unexpected{glbImportError(
                        AssetGlbImportDiagnosticCode::InvalidGlb, request.source,
                        "default scene references the same root node more than once")};
                }
                if (parentCounts[nodeIndex] != 0U) {
                    return std::unexpected{
                        glbImportError(AssetGlbImportDiagnosticCode::InvalidGlb, request.source,
                                       "default scene root is also a child of another node")};
                }
                defaultRoots[nodeIndex] = true;
            }
            return {};
        }

        [[nodiscard]] Result<void> validateNodeSubset(const fastgltf::Asset& asset,
                                                      const AssetGlbImportRequest& request) {
            for (const fastgltf::Node& node : asset.nodes) {
                if (node.cameraIndex.has_value() || node.lightIndex.has_value()) {
                    return std::unexpected{
                        glbImportError(AssetGlbImportDiagnosticCode::SceneSemanticUnsupported,
                                       request.source, "contains a camera or light node")};
                }
                if (node.skinIndex.has_value() || !node.weights.empty()) {
                    return std::unexpected{
                        glbImportError(AssetGlbImportDiagnosticCode::SkinUnsupported,
                                       request.source, "contains a skinned or weighted mesh node")};
                }
            }
            return {};
        }

        [[nodiscard]] Result<void> validateMeshSubset(const fastgltf::Asset& asset,
                                                      const AssetGlbImportRequest& request) {
            std::uint64_t primitiveCount = 0U;
            for (const fastgltf::Mesh& mesh : asset.meshes) {
                if (!mesh.weights.empty()) {
                    return std::unexpected{
                        glbImportError(AssetGlbImportDiagnosticCode::MorphTargetUnsupported,
                                       request.source, "contains mesh morph weights")};
                }
                primitiveCount += mesh.primitives.size();
                if (primitiveCount > request.limits.maxPrimitives) {
                    return std::unexpected{glbImportError(
                        AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                        "exceeds configured primitive count limit " +
                            std::to_string(request.limits.maxPrimitives))};
                }
                for (const fastgltf::Primitive& primitive : mesh.primitives) {
                    if (primitive.type != fastgltf::PrimitiveType::Triangles) {
                        return std::unexpected{glbImportError(
                            AssetGlbImportDiagnosticCode::UnsupportedPrimitiveTopology,
                            request.source, "contains a non-TRIANGLES primitive")};
                    }
                    if (!primitive.targets.empty()) {
                        return std::unexpected{
                            glbImportError(AssetGlbImportDiagnosticCode::MorphTargetUnsupported,
                                           request.source, "contains morph targets")};
                    }
                }
            }
            return {};
        }

        [[nodiscard]] Result<void> validateAccessorsSubset(const fastgltf::Asset& asset,
                                                           const AssetGlbImportRequest& request) {
            for (const fastgltf::Accessor& accessor : asset.accessors) {
                if (accessor.sparse.has_value()) {
                    return std::unexpected{
                        glbImportError(AssetGlbImportDiagnosticCode::SparseAccessorUnsupported,
                                       request.source, "contains a sparse accessor")};
                }
            }
            return {};
        }

        [[nodiscard]] Result<void> validateAssetSubset(const fastgltf::Asset& asset,
                                                       const AssetGlbImportRequest& request) {
            if (auto topLevel = validateAssetTopLevel(asset, request); !topLevel) {
                return topLevel;
            }
            auto parentCounts = validateNodeHierarchy(asset, request);
            if (!parentCounts) {
                return std::unexpected{std::move(parentCounts.error())};
            }
            if (auto roots = validateDefaultSceneRoots(asset, *parentCounts, request); !roots) {
                return roots;
            }
            if (auto nodes = validateNodeSubset(asset, request); !nodes) {
                return nodes;
            }
            if (auto meshes = validateMeshSubset(asset, request); !meshes) {
                return meshes;
            }
            return validateAccessorsSubset(asset, request);
        }

        [[nodiscard]] fastgltf::math::fmat4x4 coordinateMirror() {
            fastgltf::math::fmat4x4 result{};
            result[0][0] = -1.0F;
            return result;
        }

        [[nodiscard]] fastgltf::math::fvec3
        transformPosition(const fastgltf::math::fmat4x4& matrix,
                          const fastgltf::math::fvec3& position) {
            const fastgltf::math::fvec4 transformed =
                matrix * fastgltf::math::fvec4{position[0], position[1], position[2], 1.0F};
            return fastgltf::math::fvec3{transformed[0], transformed[1], transformed[2]};
        }

        [[nodiscard]] fastgltf::math::fvec3 cross(fastgltf::math::fvec3 lhs,
                                                  fastgltf::math::fvec3 rhs) {
            return fastgltf::math::fvec3{
                (lhs[1] * rhs[2]) - (lhs[2] * rhs[1]),
                (lhs[2] * rhs[0]) - (lhs[0] * rhs[2]),
                (lhs[0] * rhs[1]) - (lhs[1] * rhs[0]),
            };
        }

        [[nodiscard]] fastgltf::math::fvec3 subtract(fastgltf::math::fvec3 lhs,
                                                     fastgltf::math::fvec3 rhs) {
            return fastgltf::math::fvec3{lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2]};
        }

        [[nodiscard]] float lengthSquared(const fastgltf::math::fvec3& value) {
            return (value[0] * value[0]) + (value[1] * value[1]) + (value[2] * value[2]);
        }

        [[nodiscard]] Result<fastgltf::math::fvec3>
        normalizeVector(const fastgltf::math::fvec3& value, AssetGlbImportDiagnosticCode code,
                        const AssetGlbImportRequest& request, std::string_view label) {
            const float squared = lengthSquared(value);
            if (!finite(value) || !finite(squared) || squared <= kNormalLengthSquaredEpsilon) {
                return std::unexpected{glbImportError(
                    code, request.source,
                    std::string{label} + " has zero, non-finite, or unstable length")};
            }
            const float inverseLength = 1.0F / std::sqrt(squared);
            return fastgltf::math::fvec3{value[0] * inverseLength, value[1] * inverseLength,
                                         value[2] * inverseLength};
        }

        [[nodiscard]] Result<std::vector<fastgltf::math::fvec3>>
        readVec3Accessor(const fastgltf::Asset& asset, std::size_t accessorIndex,
                         DecodeBudget& budget, const AssetGlbImportRequest& request,
                         std::string_view semantic) {
            if (accessorIndex >= asset.accessors.size()) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::InvalidAccessor, request.source,
                    std::string{semantic} + " references an accessor outside the asset")};
            }
            const fastgltf::Accessor& accessor = asset.accessors[accessorIndex];
            if (accessor.type != fastgltf::AccessorType::Vec3 ||
                accessor.componentType != fastgltf::ComponentType::Float || accessor.normalized ||
                !accessor.bufferViewIndex.has_value()) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::UnsupportedVertexAttribute, request.source,
                    std::string{semantic} + " must be a non-normalized float VEC3")};
            }
            if (!accessorFitsBufferView(asset, accessor, sizeof(fastgltf::math::fvec3))) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::InvalidAccessor, request.source,
                                   std::string{semantic} + " reads beyond its buffer view")};
            }
            if (accessor.count > request.limits.maxVertices ||
                !consumeDecodeBudget(budget, accessor.count, sizeof(fastgltf::math::fvec3),
                                     request.limits.maxDecodedBytes)) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                                   std::string{semantic} + " exceeds configured decode limits")};
            }
            std::vector<fastgltf::math::fvec3> values;
            values.resize(accessor.count);
            fastgltf::copyFromAccessor<fastgltf::math::fvec3>(asset, accessor, values.data());
            if (std::ranges::any_of(
                    values, [](const fastgltf::math::fvec3& value) { return !finite(value); })) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::NonFiniteValue, request.source,
                                   std::string{semantic} + " contains NaN or infinity")};
            }
            return values;
        }

        [[nodiscard]] Result<std::vector<fastgltf::math::fvec2>>
        readVec2Accessor(const fastgltf::Asset& asset, AccessorReadRequest readRequest,
                         DecodeBudget& budget, const AssetGlbImportRequest& request) {
            if (readRequest.accessorIndex >= asset.accessors.size()) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::InvalidAccessor, request.source,
                                   std::string{readRequest.semantic} +
                                       " references an accessor outside the asset")};
            }
            const fastgltf::Accessor& accessor = asset.accessors[readRequest.accessorIndex];
            if (accessor.type != fastgltf::AccessorType::Vec2 ||
                accessor.componentType != fastgltf::ComponentType::Float || accessor.normalized ||
                !accessor.bufferViewIndex.has_value() ||
                accessor.count != readRequest.expectedCount) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::UnsupportedVertexAttribute, request.source,
                    std::string{readRequest.semantic} +
                        " must be a non-normalized float VEC2 matching POSITION count")};
            }
            if (!accessorFitsBufferView(asset, accessor, sizeof(fastgltf::math::fvec2))) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::InvalidAccessor, request.source,
                    std::string{readRequest.semantic} + " reads beyond its buffer view")};
            }
            if (accessor.count > request.limits.maxVertices ||
                !consumeDecodeBudget(budget, accessor.count, sizeof(fastgltf::math::fvec2),
                                     request.limits.maxDecodedBytes)) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                    std::string{readRequest.semantic} + " exceeds configured decode limits")};
            }
            std::vector<fastgltf::math::fvec2> values;
            values.resize(accessor.count);
            fastgltf::copyFromAccessor<fastgltf::math::fvec2>(asset, accessor, values.data());
            if (std::ranges::any_of(
                    values, [](const fastgltf::math::fvec2& value) { return !finite(value); })) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::NonFiniteValue, request.source,
                    std::string{readRequest.semantic} + " contains NaN or infinity")};
            }
            return values;
        }

        [[nodiscard]] Result<std::vector<std::uint32_t>>
        readIndices(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive,
                    std::size_t vertexCount, DecodeBudget& budget,
                    const AssetGlbImportRequest& request) {
            std::vector<std::uint32_t> indices;
            std::size_t indexCount = vertexCount;
            if (!primitive.indicesAccessor.has_value()) {
                if (vertexCount > std::numeric_limits<std::uint32_t>::max() ||
                    vertexCount > request.limits.maxIndices ||
                    !consumeDecodeBudget(budget, vertexCount, sizeof(std::uint32_t),
                                         request.limits.maxDecodedBytes)) {
                    return std::unexpected{glbImportError(
                        AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                        "has too many non-indexed vertices for configured uint32 index limits")};
                }
                indices.resize(vertexCount);
                for (std::size_t index = 0; index < vertexCount; ++index) {
                    indices[index] = static_cast<std::uint32_t>(index);
                }
            } else {
                const std::size_t accessorIndex = *primitive.indicesAccessor;
                if (accessorIndex >= asset.accessors.size()) {
                    return std::unexpected{glbImportError(
                        AssetGlbImportDiagnosticCode::InvalidAccessor, request.source,
                        "indices reference an accessor outside the asset")};
                }
                const fastgltf::Accessor& accessor = asset.accessors[accessorIndex];
                indexCount = accessor.count;
                const bool supportedComponent =
                    accessor.componentType == fastgltf::ComponentType::UnsignedByte ||
                    accessor.componentType == fastgltf::ComponentType::UnsignedShort ||
                    accessor.componentType == fastgltf::ComponentType::UnsignedInt;
                if (accessor.type != fastgltf::AccessorType::Scalar || !supportedComponent ||
                    accessor.normalized || !accessor.bufferViewIndex.has_value()) {
                    return std::unexpected{glbImportError(
                        AssetGlbImportDiagnosticCode::InvalidAccessor, request.source,
                        "indices must be non-normalized unsigned scalar values")};
                }
                const std::uint64_t elementBytes =
                    fastgltf::getComponentByteSize(accessor.componentType);
                if (!accessorFitsBufferView(asset, accessor, elementBytes)) {
                    return std::unexpected{
                        glbImportError(AssetGlbImportDiagnosticCode::InvalidAccessor,
                                       request.source, "indices read beyond their buffer view")};
                }
                if (accessor.count > request.limits.maxIndices) {
                    return std::unexpected{glbImportError(
                        AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                        "index accessor exceeds configured index count limit")};
                }
            }

            if (primitive.indicesAccessor.has_value() &&
                (indexCount > request.limits.maxIndices ||
                 !consumeDecodeBudget(budget, indexCount, sizeof(std::uint32_t),
                                      request.limits.maxDecodedBytes))) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                                   "indices exceed configured decode limits")};
            }
            if (primitive.indicesAccessor.has_value()) {
                const fastgltf::Accessor& accessor = asset.accessors[*primitive.indicesAccessor];
                indices.resize(accessor.count);
                fastgltf::copyFromAccessor<std::uint32_t>(asset, accessor, indices.data());
            }

            if (indices.empty() || indices.size() % 3U != 0U) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::InvalidIndex, request.source,
                                   "triangle index count must be non-zero and divisible by three")};
            }
            if (std::ranges::any_of(
                    indices, [vertexCount](std::uint32_t index) { return index >= vertexCount; })) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::InvalidIndex, request.source,
                                   "contains an index outside the POSITION accessor")};
            }
            return indices;
        }

        struct PrimitiveSourceData {
            std::vector<fastgltf::math::fvec3> positions;
            std::vector<fastgltf::math::fvec3> normals;
            std::vector<fastgltf::math::fvec2> textureCoordinates;
            std::vector<std::uint32_t> indices;
            bool hasNormals{};
            bool hasTextureCoordinates{};
        };

        [[nodiscard]] Result<void>
        validatePrimitiveAttributes(const fastgltf::Primitive& primitive,
                                    const AssetGlbImportRequest& request) {
            if (primitive.findAttribute("POSITION") == primitive.attributes.end()) {
                return std::unexpected{glbImportError(AssetGlbImportDiagnosticCode::MissingPosition,
                                                      request.source,
                                                      "contains a primitive without POSITION")};
            }
            for (const fastgltf::Attribute& attribute : primitive.attributes) {
                if (attribute.name != "POSITION" && attribute.name != "NORMAL" &&
                    attribute.name != "TEXCOORD_0") {
                    return std::unexpected{glbImportError(
                        AssetGlbImportDiagnosticCode::UnsupportedVertexAttribute, request.source,
                        "contains unsupported vertex attribute \"" + std::string{attribute.name} +
                            "\"")};
                }
            }
            return {};
        }

        [[nodiscard]] Result<PrimitiveSourceData>
        readPrimitiveSourceData(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive,
                                DecodeBudget& budget, const AssetGlbImportRequest& request) {
            if (auto attributes = validatePrimitiveAttributes(primitive, request); !attributes) {
                return std::unexpected{std::move(attributes.error())};
            }
            const auto* const positionAttribute = primitive.findAttribute("POSITION");
            auto positions = readVec3Accessor(asset, positionAttribute->accessorIndex, budget,
                                              request, "POSITION");
            if (!positions) {
                return std::unexpected{std::move(positions.error())};
            }
            if (positions->empty()) {
                return std::unexpected{glbImportError(AssetGlbImportDiagnosticCode::EmptyMesh,
                                                      request.source,
                                                      "contains an empty POSITION accessor")};
            }
            auto indices = readIndices(asset, primitive, positions->size(), budget, request);
            if (!indices) {
                return std::unexpected{std::move(indices.error())};
            }

            PrimitiveSourceData data{
                .positions = std::move(*positions),
                .indices = std::move(*indices),
            };
            const auto* const normalAttribute = primitive.findAttribute("NORMAL");
            data.hasNormals = normalAttribute != primitive.attributes.end();
            if (data.hasNormals) {
                auto normals = readVec3Accessor(asset, normalAttribute->accessorIndex, budget,
                                                request, "NORMAL");
                if (!normals) {
                    return std::unexpected{std::move(normals.error())};
                }
                if (normals->size() != data.positions.size()) {
                    return std::unexpected{glbImportError(
                        AssetGlbImportDiagnosticCode::InvalidAccessor, request.source,
                        "NORMAL count does not match POSITION count")};
                }
                data.normals = std::move(*normals);
            }

            const auto* const textureCoordinateAttribute = primitive.findAttribute("TEXCOORD_0");
            data.hasTextureCoordinates = textureCoordinateAttribute != primitive.attributes.end();
            if (data.hasTextureCoordinates) {
                auto textureCoordinates =
                    readVec2Accessor(asset,
                                     AccessorReadRequest{
                                         .accessorIndex = textureCoordinateAttribute->accessorIndex,
                                         .expectedCount = data.positions.size(),
                                         .semantic = "TEXCOORD_0",
                                     },
                                     budget, request);
                if (!textureCoordinates) {
                    return std::unexpected{std::move(textureCoordinates.error())};
                }
                data.textureCoordinates = std::move(*textureCoordinates);
            }
            return data;
        }

        [[nodiscard]] Result<fastgltf::math::fmat3x3>
        bakePrimitiveTransform(PrimitiveSourceData& data, const fastgltf::math::fmat4x4& gltfWorld,
                               const AssetGlbImportRequest& request) {
            const fastgltf::math::fmat4x4 localWorld = coordinateMirror() * gltfWorld;
            if (!finite(localWorld)) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::NonFiniteValue, request.source,
                                   "contains a node transform with NaN or infinity")};
            }
            const fastgltf::math::fmat3x3 linear{localWorld};
            const float determinant = fastgltf::math::determinant(linear);
            if (!finite(determinant) || std::fabs(determinant) <= kTransformDeterminantEpsilon) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::NonInvertibleTransform, request.source,
                    "contains a non-invertible mesh node transform")};
            }
            if (determinant < 0.0F) {
                for (std::size_t triangle = 0U; triangle < data.indices.size(); triangle += 3U) {
                    std::swap(data.indices[triangle + 1U], data.indices[triangle + 2U]);
                }
            }
            for (fastgltf::math::fvec3& position : data.positions) {
                position = transformPosition(localWorld, position);
                if (!finite(position)) {
                    return std::unexpected{
                        glbImportError(AssetGlbImportDiagnosticCode::NonFiniteValue, request.source,
                                       "produces a non-finite transformed position")};
                }
            }
            if (data.hasNormals) {
                const fastgltf::math::fmat3x3 normalMatrix =
                    fastgltf::math::transpose(fastgltf::math::inverse(linear));
                for (fastgltf::math::fvec3& normal : data.normals) {
                    auto normalized = normalizeVector(normalMatrix * normal,
                                                      AssetGlbImportDiagnosticCode::NonFiniteValue,
                                                      request, "transformed NORMAL");
                    if (!normalized) {
                        return std::unexpected{std::move(normalized.error())};
                    }
                    normal = *normalized;
                }
            }
            return linear;
        }

        [[nodiscard]] Result<void> validateTriangles(const PrimitiveSourceData& data,
                                                     const AssetGlbImportRequest& request) {
            for (std::size_t triangle = 0U; triangle < data.indices.size(); triangle += 3U) {
                const fastgltf::math::fvec3 edgeOne =
                    subtract(data.positions[data.indices[triangle + 1U]],
                             data.positions[data.indices[triangle]]);
                const fastgltf::math::fvec3 edgeTwo =
                    subtract(data.positions[data.indices[triangle + 2U]],
                             data.positions[data.indices[triangle]]);
                const float twiceAreaSquared = lengthSquared(cross(edgeOne, edgeTwo));
                if (!finite(twiceAreaSquared) || twiceAreaSquared <= kNormalLengthSquaredEpsilon) {
                    return std::unexpected{glbImportError(
                        AssetGlbImportDiagnosticCode::DegenerateTriangle, request.source,
                        "contains a degenerate triangle at local triangle " +
                            std::to_string(triangle / 3U))};
                }
            }
            return {};
        }

        void appendAuthoredNormalVertices(mesh::MeshProductBuildInputV1& product,
                                          const PrimitiveSourceData& data,
                                          std::uint32_t baseVertex) {
            for (std::size_t index = 0U; index < data.positions.size(); ++index) {
                const fastgltf::math::fvec2 textureCoordinate = data.hasTextureCoordinates
                                                                    ? data.textureCoordinates[index]
                                                                    : fastgltf::math::fvec2{};
                product.vertices.push_back(mesh::MeshVertexP3N3Uv2F32{
                    .positionX = canonicalFloat(data.positions[index][0]),
                    .positionY = canonicalFloat(data.positions[index][1]),
                    .positionZ = canonicalFloat(data.positions[index][2]),
                    .normalX = canonicalFloat(data.normals[index][0]),
                    .normalY = canonicalFloat(data.normals[index][1]),
                    .normalZ = canonicalFloat(data.normals[index][2]),
                    .uvX = canonicalFloat(textureCoordinate[0]),
                    .uvY = canonicalFloat(textureCoordinate[1]),
                });
            }
            for (const std::uint32_t index : data.indices) {
                product.indices.push_back(baseVertex + index);
            }
        }

        [[nodiscard]] Result<void> appendFlatNormalVertices(mesh::MeshProductBuildInputV1& product,
                                                            const PrimitiveSourceData& data,
                                                            const AssetGlbImportRequest& request) {
            for (std::size_t triangle = 0U; triangle < data.indices.size(); triangle += 3U) {
                const std::uint32_t firstVertex = data.indices[triangle];
                const std::uint32_t secondVertex = data.indices[triangle + 1U];
                const std::uint32_t thirdVertex = data.indices[triangle + 2U];
                auto faceNormal = normalizeVector(
                    cross(subtract(data.positions[secondVertex], data.positions[firstVertex]),
                          subtract(data.positions[thirdVertex], data.positions[firstVertex])),
                    AssetGlbImportDiagnosticCode::DegenerateTriangle, request,
                    "triangle used for flat normal generation");
                if (!faceNormal) {
                    return std::unexpected{std::move(faceNormal.error())};
                }
                const std::array<std::uint32_t, 3U> faceIndices{firstVertex, secondVertex,
                                                                thirdVertex};
                for (const std::uint32_t sourceIndex : faceIndices) {
                    const fastgltf::math::fvec2 textureCoordinate =
                        data.hasTextureCoordinates ? data.textureCoordinates[sourceIndex]
                                                   : fastgltf::math::fvec2{};
                    const fastgltf::math::fvec3& position = data.positions[sourceIndex];
                    product.vertices.push_back(mesh::MeshVertexP3N3Uv2F32{
                        .positionX = canonicalFloat(position[0]),
                        .positionY = canonicalFloat(position[1]),
                        .positionZ = canonicalFloat(position[2]),
                        .normalX = canonicalFloat((*faceNormal)[0]),
                        .normalY = canonicalFloat((*faceNormal)[1]),
                        .normalZ = canonicalFloat((*faceNormal)[2]),
                        .uvX = canonicalFloat(textureCoordinate[0]),
                        .uvY = canonicalFloat(textureCoordinate[1]),
                    });
                    product.indices.push_back(
                        static_cast<std::uint32_t>(product.vertices.size() - 1U));
                }
            }
            return {};
        }

        [[nodiscard]] Result<void> appendPrimitive(mesh::MeshProductBuildInputV1& product,
                                                   const fastgltf::Asset& asset,
                                                   const fastgltf::Primitive& primitive,
                                                   const fastgltf::math::fmat4x4& gltfWorld,
                                                   DecodeBudget& budget,
                                                   const AssetGlbImportRequest& request) {
            auto sourceData = readPrimitiveSourceData(asset, primitive, budget, request);
            if (!sourceData) {
                return std::unexpected{std::move(sourceData.error())};
            }
            auto transform = bakePrimitiveTransform(*sourceData, gltfWorld, request);
            if (!transform) {
                return std::unexpected{std::move(transform.error())};
            }
            if (auto triangles = validateTriangles(*sourceData, request); !triangles) {
                return triangles;
            }

            const std::uint64_t addedVertices =
                sourceData->hasNormals ? sourceData->positions.size() : sourceData->indices.size();
            const std::uint64_t newVertexCount = product.vertices.size() + addedVertices;
            const std::uint64_t newIndexCount = product.indices.size() + sourceData->indices.size();
            if (newVertexCount > request.limits.maxVertices ||
                newIndexCount > request.limits.maxIndices ||
                newVertexCount > std::numeric_limits<std::uint32_t>::max() ||
                newIndexCount > std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                                   "exceeds configured cooked vertex or index count limits")};
            }
            if (!consumeDecodeBudget(budget, addedVertices, sizeof(mesh::MeshVertexP3N3Uv2F32),
                                     request.limits.maxDecodedBytes) ||
                !consumeDecodeBudget(budget, sourceData->indices.size(), sizeof(std::uint32_t),
                                     request.limits.maxDecodedBytes) ||
                !consumeDecodeBudget(budget, 1U, sizeof(mesh::MeshSubmeshV1),
                                     request.limits.maxDecodedBytes)) {
                return std::unexpected{glbImportError(
                    AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                    "cooked mesh output exceeds configured decoded working-byte limit")};
            }

            const auto firstIndex = static_cast<std::uint32_t>(product.indices.size());
            const auto baseVertex = static_cast<std::uint32_t>(product.vertices.size());
            product.vertices.reserve(static_cast<std::size_t>(newVertexCount));
            product.indices.reserve(static_cast<std::size_t>(newIndexCount));
            product.submeshes.reserve(product.submeshes.size() + 1U);
            if (sourceData->hasNormals) {
                appendAuthoredNormalVertices(product, *sourceData, baseVertex);
            } else {
                if (auto appended = appendFlatNormalVertices(product, *sourceData, request);
                    !appended) {
                    return appended;
                }
            }

            std::uint32_t materialSlot = 0U;
            if (primitive.materialIndex.has_value()) {
                if (*primitive.materialIndex >= asset.materials.size() ||
                    *primitive.materialIndex >= std::numeric_limits<std::uint32_t>::max()) {
                    return std::unexpected{glbImportError(
                        AssetGlbImportDiagnosticCode::InvalidAccessor, request.source,
                        "primitive references a material outside the asset")};
                }
                materialSlot = static_cast<std::uint32_t>(*primitive.materialIndex + 1U);
            }
            product.submeshes.push_back(mesh::MeshSubmeshV1{
                .firstIndex = firstIndex,
                .indexCount = static_cast<std::uint32_t>(product.indices.size() - firstIndex),
                .materialSlot = materialSlot,
            });
            return {};
        }

        struct PendingNode {
            std::size_t nodeIndex;
            fastgltf::math::fmat4x4 parentWorld;
            std::uint32_t depth;
        };

        struct SceneFlattenState {
            mesh::MeshProductBuildInputV1 product;
            DecodeBudget budget;
            std::vector<PendingNode> pending;
            std::vector<bool> visited;
        };

        [[nodiscard]] Result<SceneFlattenState>
        makeSceneFlattenState(const fastgltf::Asset& asset, const AssetGlbImportRequest& request) {
            SceneFlattenState state;
            if (!consumeDecodeBudget(state.budget, asset.materials.size() + 1U,
                                     sizeof(mesh::MeshMaterialSlotV1),
                                     request.limits.maxDecodedBytes)) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                                   "material slots exceed configured decoded working-byte limit")};
            }
            state.product.materialSlots.resize(asset.materials.size() + 1U);
            if (!consumeDecodeBudget(state.budget, asset.nodes.size(),
                                     sizeof(PendingNode) + sizeof(bool),
                                     request.limits.maxDecodedBytes)) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                                   "node traversal exceeds configured decoded working-byte limit")};
            }
            state.pending.reserve(asset.nodes.size());
            const fastgltf::Scene& scene = asset.scenes[*asset.defaultScene];
            for (auto root = scene.nodeIndices.rbegin(); root != scene.nodeIndices.rend(); ++root) {
                state.pending.push_back(PendingNode{
                    .nodeIndex = *root,
                    .parentWorld = fastgltf::math::fmat4x4{},
                    .depth = 1U,
                });
            }
            state.visited.resize(asset.nodes.size(), false);
            return state;
        }

        [[nodiscard]] Result<void> appendNodeMesh(SceneFlattenState& state,
                                                  const fastgltf::Asset& asset,
                                                  const fastgltf::Node& node,
                                                  const fastgltf::math::fmat4x4& world,
                                                  const AssetGlbImportRequest& request) {
            if (!node.meshIndex.has_value()) {
                return {};
            }
            if (*node.meshIndex >= asset.meshes.size()) {
                return std::unexpected{glbImportError(AssetGlbImportDiagnosticCode::InvalidAccessor,
                                                      request.source,
                                                      "node references a mesh outside the asset")};
            }
            const fastgltf::Mesh& mesh = asset.meshes[*node.meshIndex];
            for (const fastgltf::Primitive& primitive : mesh.primitives) {
                auto appended =
                    appendPrimitive(state.product, asset, primitive, world, state.budget, request);
                if (!appended) {
                    return appended;
                }
            }
            return {};
        }

        [[nodiscard]] Result<void> visitPendingNode(SceneFlattenState& state,
                                                    const fastgltf::Asset& asset, PendingNode item,
                                                    const AssetGlbImportRequest& request) {
            if (item.nodeIndex >= asset.nodes.size()) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::InvalidAccessor, request.source,
                                   "default scene traversal reached a node outside the asset")};
            }
            if (item.depth > request.limits.maxNodeDepth) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                                   "default scene exceeds configured node depth limit " +
                                       std::to_string(request.limits.maxNodeDepth))};
            }
            if (state.visited[item.nodeIndex]) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::InvalidGlb, request.source,
                                   "default scene reaches the same node more than once")};
            }
            state.visited[item.nodeIndex] = true;
            const fastgltf::Node& node = asset.nodes[item.nodeIndex];
            const fastgltf::math::fmat4x4 world =
                fastgltf::getTransformMatrix(node, item.parentWorld);
            if (!finite(world)) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::NonFiniteValue, request.source,
                                   "contains a node transform with NaN or infinity")};
            }
            if (auto appended = appendNodeMesh(state, asset, node, world, request); !appended) {
                return appended;
            }
            for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
                state.pending.push_back(PendingNode{
                    .nodeIndex = *child,
                    .parentWorld = world,
                    .depth = item.depth + 1U,
                });
            }
            return {};
        }

        [[nodiscard]] mesh::MeshAabbV1
        computeProductBounds(const std::vector<mesh::MeshVertexP3N3Uv2F32>& vertices) {
            const mesh::MeshVertexP3N3Uv2F32& first = vertices.front();
            mesh::MeshAabbV1 bounds{
                .minX = first.positionX,
                .minY = first.positionY,
                .minZ = first.positionZ,
                .maxX = first.positionX,
                .maxY = first.positionY,
                .maxZ = first.positionZ,
            };
            for (const mesh::MeshVertexP3N3Uv2F32& vertex : vertices) {
                bounds.minX = (std::min)(bounds.minX, vertex.positionX);
                bounds.minY = (std::min)(bounds.minY, vertex.positionY);
                bounds.minZ = (std::min)(bounds.minZ, vertex.positionZ);
                bounds.maxX = (std::max)(bounds.maxX, vertex.positionX);
                bounds.maxY = (std::max)(bounds.maxY, vertex.positionY);
                bounds.maxZ = (std::max)(bounds.maxZ, vertex.positionZ);
            }
            return bounds;
        }

        [[nodiscard]] Result<mesh::MeshProductBuildInputV1>
        flattenDefaultScene(fastgltf::Asset& asset, const AssetGlbImportRequest& request) {
            auto state = makeSceneFlattenState(asset, request);
            if (!state) {
                return std::unexpected{std::move(state.error())};
            }
            while (!state->pending.empty()) {
                const PendingNode item = state->pending.back();
                state->pending.pop_back();
                if (auto visited = visitPendingNode(*state, asset, item, request); !visited) {
                    return std::unexpected{std::move(visited.error())};
                }
            }
            mesh::MeshProductBuildInputV1& product = state->product;
            if (product.vertices.empty() || product.indices.empty() || product.submeshes.empty()) {
                return std::unexpected{
                    glbImportError(AssetGlbImportDiagnosticCode::EmptyMesh, request.source,
                                   "default scene does not contain any supported mesh primitives")};
            }
            product.bounds = computeProductBounds(product.vertices);
            return std::move(product);
        }

    } // namespace

    AssetGlbImporterDescriptor makeRestrictedGlbMeshImporterDescriptor() {
        return AssetGlbImporterDescriptor{
            .importerName = std::string{kGlbMeshImporterName},
            .importerVersion = kGlbMeshImporterVersion,
            .supportedSourceExtension = std::string{kGlbMeshSourceExtension},
        };
    }

    bool isRestrictedGlbMeshImportCandidate(const SourceAssetRecord& source) noexcept {
        return hasSourceExtension(source.sourcePath, kGlbMeshSourceExtension) ||
               source.importerName == kGlbMeshImporterName ||
               source.importerId == makeImporterId(kGlbMeshImporterName) ||
               source.assetTypeName == kGlbMeshAssetTypeName ||
               source.assetType == makeAssetTypeId(kGlbMeshAssetTypeName);
    }

    const char* assetGlbImportDiagnosticCodeName(AssetGlbImportDiagnosticCode code) noexcept {
        switch (code) {
        case AssetGlbImportDiagnosticCode::InvalidRequest:
            return "invalid-request";
        case AssetGlbImportDiagnosticCode::UnsupportedSourceExtension:
            return "unsupported-source-extension";
        case AssetGlbImportDiagnosticCode::SourceByteLimitExceeded:
            return "source-byte-limit-exceeded";
        case AssetGlbImportDiagnosticCode::InvalidGlb:
            return "invalid-glb";
        case AssetGlbImportDiagnosticCode::JsonByteLimitExceeded:
            return "json-byte-limit-exceeded";
        case AssetGlbImportDiagnosticCode::InvalidJson:
            return "invalid-json";
        case AssetGlbImportDiagnosticCode::ExternalUriUnsupported:
            return "external-uri-unsupported";
        case AssetGlbImportDiagnosticCode::RequiredExtensionUnsupported:
            return "required-extension-unsupported";
        case AssetGlbImportDiagnosticCode::UnsupportedBufferLayout:
            return "unsupported-buffer-layout";
        case AssetGlbImportDiagnosticCode::MissingDefaultScene:
            return "missing-default-scene";
        case AssetGlbImportDiagnosticCode::CountLimitExceeded:
            return "count-limit-exceeded";
        case AssetGlbImportDiagnosticCode::AnimationUnsupported:
            return "animation-unsupported";
        case AssetGlbImportDiagnosticCode::SceneSemanticUnsupported:
            return "scene-semantic-unsupported";
        case AssetGlbImportDiagnosticCode::SkinUnsupported:
            return "skin-unsupported";
        case AssetGlbImportDiagnosticCode::MorphTargetUnsupported:
            return "morph-target-unsupported";
        case AssetGlbImportDiagnosticCode::SparseAccessorUnsupported:
            return "sparse-accessor-unsupported";
        case AssetGlbImportDiagnosticCode::UnsupportedPrimitiveTopology:
            return "unsupported-primitive-topology";
        case AssetGlbImportDiagnosticCode::UnsupportedVertexAttribute:
            return "unsupported-vertex-attribute";
        case AssetGlbImportDiagnosticCode::MissingPosition:
            return "missing-position";
        case AssetGlbImportDiagnosticCode::InvalidAccessor:
            return "invalid-accessor";
        case AssetGlbImportDiagnosticCode::InvalidIndex:
            return "invalid-index";
        case AssetGlbImportDiagnosticCode::NonFiniteValue:
            return "non-finite-value";
        case AssetGlbImportDiagnosticCode::NonInvertibleTransform:
            return "non-invertible-transform";
        case AssetGlbImportDiagnosticCode::DegenerateTriangle:
            return "degenerate-triangle";
        case AssetGlbImportDiagnosticCode::EmptyMesh:
            return "empty-mesh";
        }
        return "unknown";
    }

    Result<mesh::MeshProductBuildInputV1>
    importRestrictedGlbMesh(const AssetGlbImportRequest& request) {
        try {
            if (auto validRequest = validateRequest(request); !validRequest) {
                return std::unexpected{std::move(validRequest.error())};
            }
            auto preflight = preflightGlb(request);
            if (!preflight) {
                return std::unexpected{std::move(preflight.error())};
            }
            auto asset = parseAsset(request);
            if (!asset) {
                return std::unexpected{std::move(asset.error())};
            }
            if (auto subset = validateAssetSubset(*asset, request); !subset) {
                return std::unexpected{std::move(subset.error())};
            }
            return flattenDefaultScene(*asset, request);
        } catch (const std::bad_alloc&) {
            return std::unexpected{
                glbImportError(AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                               "could not allocate bounded importer working memory")};
        } catch (const std::length_error&) {
            return std::unexpected{
                glbImportError(AssetGlbImportDiagnosticCode::CountLimitExceeded, request.source,
                               "requested a container size outside platform addressability")};
        }
    }

} // namespace asharia::asset
