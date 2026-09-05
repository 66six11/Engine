#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "asharia/material_instance/mat_io.hpp"
#include "asharia/material_instance/mat_parameters.hpp"
#include "asharia/material_instance/mat_resolver.hpp"
#include "asharia/shader_authoring/ashader_parser.hpp"

namespace {

    constexpr std::uint64_t kTypeHash = 0x00000000000000AAULL;
    constexpr std::uint64_t kSignatureHash = 0x00000000000000BBULL;

    void logFailure(std::string_view message) {
        std::cerr << "material-instance smoke failure: " << message << '\n';
    }

    asharia::shader_authoring::AshaderDocument makeShaderDocument() {
        constexpr std::string_view kSource = R"ashader(
schema 2

shader "asharia.material.unlit" {
  properties {
    color baseColor = [1, 1, 1, 1]
    float roughness = 0.5
    texture2D albedoMap
    bool useAlpha = false
  }

  pass "Forward" {
    vertex vertexMain
    fragment fragmentMain
    slang "Unlit.slang"
  }
}
)ashader";

        auto parsed = asharia::shader_authoring::parseAshaderDocument(
            kSource, asharia::shader_authoring::AshaderParseOptions{.sourceName = "Unlit.ashader"});
        if (!parsed.document) {
            logFailure("failed to parse .ashader fixture.");
            return {};
        }
        return *parsed.document;
    }

    bool hasDiagnostic(const asharia::material_instance::MatResolveResult& result,
                       asharia::material_instance::MatDiagnosticCode code,
                       std::optional<std::string_view> propertyId = std::nullopt) {
        return std::ranges::any_of(result.diagnostics, [&](const auto& diagnostic) {
            return diagnostic.code == code && (!propertyId || diagnostic.propertyId == *propertyId);
        });
    }

    bool hasDiff(const asharia::material_instance::MatResolveResult& result,
                 std::string_view propertyId,
                 asharia::material_instance::MatOverrideDiffKind kind) {
        return std::ranges::any_of(result.overrides, [&](const auto& diff) {
            return diff.propertyId == propertyId && diff.kind == kind;
        });
    }

    bool smokeReadWriteResolve() {
        constexpr std::string_view kMat = R"json(
{
  "schemaVersion": 2,
  "materialType": {
    "assetGuid": "11111111-1111-1111-1111-111111111111",
    "stableTypeId": "asharia.material.unlit",
    "expectedTypeHash": "00000000000000aa"
  },
  "variant": {
    "staticSwitches": {}
  },
  "properties": {
    "baseColor": {
      "propertyId": "baseColor",
      "type": "color",
      "value": [1.0, 0.0, 0.0, 1.0]
    },
    "roughness": {
      "propertyId": "roughness",
      "type": "float",
      "value": 0.25
    },
    "albedoMap": {
      "propertyId": "albedoMap",
      "type": "texture2D",
      "assetGuid": "22222222-2222-2222-2222-222222222222"
    }
  },
  "import": {
    "lastCookedSignatureHash": "00000000000000bb",
    "lastCookedAt": "2026-06-13T00:00:00Z"
  }
}
)json";

        auto document = asharia::material_instance::readMatText(kMat);
        if (!document) {
            logFailure(document.error().message);
            return false;
        }
        if (document->properties.size() != 3 ||
            document->materialType.stableTypeId != "asharia.material.unlit" ||
            document->materialType.expectedTypeHash != kTypeHash ||
            document->import.lastCookedSignatureHash != kSignatureHash) {
            logFailure("read .mat document did not preserve expected fields.");
            return false;
        }

        auto written = asharia::material_instance::writeMatText(*document);
        if (!written) {
            logFailure(written.error().message);
            return false;
        }
        if (written->find(R"("baseColor")") == std::string::npos ||
            written->find(R"("lastCookedSignatureHash": "00000000000000bb")") ==
                std::string::npos) {
            logFailure("written .mat text is missing stable fields.");
            return false;
        }
        auto roundTrip = asharia::material_instance::readMatText(*written);
        if (!roundTrip || roundTrip->properties.size() != document->properties.size() ||
            roundTrip->materialType != document->materialType ||
            roundTrip->import != document->import) {
            logFailure("written .mat text did not round-trip.");
            return false;
        }

        const auto shader = makeShaderDocument();
        auto result = asharia::material_instance::resolveMatOverrides(
            *document, shader,
            asharia::material_instance::MatResolveOptions{
                .currentMaterialTypeHash = kTypeHash,
                .currentSignatureHash = kSignatureHash,
            });
        if (asharia::material_instance::hasErrors(result.diagnostics) ||
            result.overrides.size() != 4 ||
            !hasDiff(result, "baseColor",
                     asharia::material_instance::MatOverrideDiffKind::Overridden) ||
            !hasDiff(result, "roughness",
                     asharia::material_instance::MatOverrideDiffKind::Overridden) ||
            !hasDiff(result, "albedoMap",
                     asharia::material_instance::MatOverrideDiffKind::Overridden) ||
            !hasDiff(result, "useAlpha",
                     asharia::material_instance::MatOverrideDiffKind::Defaulted)) {
            logFailure("valid .mat overrides did not resolve deterministically.");
            return false;
        }

        return true;
    }

    bool smokeDiagnostics() {
        constexpr std::string_view kMat = R"json(
{
  "schemaVersion": 2,
  "materialType": {
    "assetGuid": "11111111-1111-1111-1111-111111111111",
    "stableTypeId": "asharia.material.unlit",
    "expectedTypeHash": "00000000000000aa"
  },
  "variant": {
    "staticSwitches": {}
  },
  "properties": {
    "roughness": {
      "propertyId": "roughness",
      "type": "bool",
      "value": true
    },
    "missingProperty": {
      "propertyId": "missingProperty",
      "type": "float",
      "value": 1.0
    }
  },
  "import": {
    "lastCookedSignatureHash": "00000000000000bb"
  }
}
)json";

        auto document = asharia::material_instance::readMatText(kMat);
        if (!document) {
            logFailure(document.error().message);
            return false;
        }

        const auto shader = makeShaderDocument();
        auto result = asharia::material_instance::resolveMatOverrides(
            *document, shader,
            asharia::material_instance::MatResolveOptions{
                .currentMaterialTypeHash = 0x00000000000000CCULL,
                .currentSignatureHash = 0x00000000000000DDULL,
            });
        if (!asharia::material_instance::hasErrors(result.diagnostics) ||
            !hasDiagnostic(result,
                           asharia::material_instance::MatDiagnosticCode::PropertyTypeMismatch,
                           "roughness") ||
            !hasDiagnostic(result, asharia::material_instance::MatDiagnosticCode::UnknownProperty,
                           "missingProperty") ||
            !hasDiagnostic(result,
                           asharia::material_instance::MatDiagnosticCode::StaleMaterialTypeHash) ||
            !hasDiagnostic(result,
                           asharia::material_instance::MatDiagnosticCode::StaleSignatureHash) ||
            !hasDiff(result, "roughness",
                     asharia::material_instance::MatOverrideDiffKind::Invalid)) {
            logFailure("invalid .mat fixture did not produce deterministic diagnostics.");
            return false;
        }

        auto malformed = asharia::material_instance::readMatText("{");
        if (malformed) {
            logFailure("malformed JSON unexpectedly parsed.");
            return false;
        }

        auto unsupportedSchema = asharia::material_instance::readMatText(R"json({
  "schemaVersion": 3,
  "materialType": {
    "assetGuid": "11111111-1111-1111-1111-111111111111",
    "stableTypeId": "asharia.material.unlit",
    "expectedTypeHash": "00000000000000aa"
  },
  "properties": {},
  "import": {
    "lastCookedSignatureHash": "00000000000000bb"
  }
})json");
        if (unsupportedSchema) {
            logFailure("unsupported schemaVersion unexpectedly parsed.");
            return false;
        }

        return true;
    }

    bool smokeProgrammaticValueValidation() {
        using namespace asharia::material_instance;
        using asharia::shader_authoring::AshaderPropertyType;
        MatDocument valid;
        valid.materialType.assetGuid.bytes[0] = 1;
        valid.materialType.stableTypeId = "asharia.material.unlit";
        valid.materialType.expectedTypeHash = kTypeHash;
        valid.import.lastCookedSignatureHash = kSignatureHash;
        valid.properties.push_back(
            {.propertyId = "roughness",
             .type = AshaderPropertyType::Float,
             .value = {.kind = MatPropertyValueKind::Number, .numberValue = 0.25}});
        const auto shader = makeShaderDocument();
        if (!validateMatDocument(valid) ||
            hasErrors(resolveMatOverrides(valid, shader).diagnostics)) {
            logFailure("valid programmatic material was rejected");
            return false;
        }
        const auto rejected = [&](const MatDocument& document) {
            const auto resolved = resolveMatOverrides(document, shader);
            if (validateMatDocument(document) || writeMatText(document) ||
                !hasDiagnostic(resolved, MatDiagnosticCode::InvalidOverride) ||
                !hasErrors(resolved.diagnostics) || !resolved.overrides.empty()) {
                logFailure("malformed programmatic material escaped a public validation boundary");
                return false;
            }
            const auto repeated = resolveMatOverrides(document, shader);
            return resolved.diagnostics.front().message == repeated.diagnostics.front().message;
        };
        for (double number :
             {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity()}) {
            auto invalid = valid;
            invalid.properties[0].value.numberValue = number;
            if (!rejected(invalid)) {
                return false;
            }
        }
        auto vector = valid;
        vector.properties[0] = {
            .propertyId = "baseColor",
            .type = AshaderPropertyType::Color,
            .value = {.kind = MatPropertyValueKind::Vector, .vectorValue = {1, 0, 0, 1}}};
        if (!validateMatDocument(vector) ||
            hasErrors(resolveMatOverrides(vector, shader).diagnostics)) {
            logFailure("valid color override was rejected");
            return false;
        }
        vector.properties[0].value.vectorValue[2] = std::numeric_limits<double>::infinity();
        if (!rejected(vector)) {
            return false;
        }
        vector.properties[0].value.vectorValue = {1, 0, 0};
        if (!rejected(vector)) {
            return false;
        }
        auto wrongKind = valid;
        wrongKind.properties[0].value.kind = MatPropertyValueKind::Boolean;
        if (!rejected(wrongKind)) {
            return false;
        }
        auto duplicate = valid;
        duplicate.properties.push_back(duplicate.properties.front());
        if (!rejected(duplicate)) {
            return false;
        }
        auto missingIdentity = valid;
        missingIdentity.materialType.assetGuid = {};
        return rejected(missingIdentity);
    }

    bool checkParameterFailures(
        const asharia::material_instance::MatDocument& document,
        const asharia::shader_authoring::AshaderDocument& shader,
        const std::vector<asharia::material_instance::MatParameterMember>& members) {
        using namespace asharia::material_instance;
        using namespace asharia::shader_authoring;
        auto rejected = [&](const MatDocument& input, const AshaderDocument& type,
                            const std::vector<MatParameterMember>& layout, std::uint32_t size,
                            MatParameterError code) {
            auto first = packMatParameters(input, type, layout, size);
            auto second = packMatParameters(input, type, layout, size);
            if (first || second || first.error().domain != asharia::ErrorDomain::Material ||
                first.error().code != static_cast<int>(code) ||
                first.error().message != second.error().message) {
                logFailure("expected deterministic parameter failure");
                return false;
            }
            return true;
        };
        auto invalidLayout = members;
        invalidLayout[0].byteOffset = 32;
        if (!rejected(document, shader, invalidLayout, 48, MatParameterError::InvalidLayout)) {
            return false;
        }
        for (auto offset : {1U, 48U, std::numeric_limits<std::uint32_t>::max()}) {
            invalidLayout = members;
            invalidLayout[0].byteOffset = offset;
            if (!rejected(document, shader, invalidLayout, 48, MatParameterError::InvalidLayout)) {
                return false;
            }
        }
        invalidLayout = members;
        invalidLayout[0] = invalidLayout[1];
        if (!rejected(document, shader, invalidLayout, 48, MatParameterError::InvalidLayout)) {
            return false;
        }
        invalidLayout = members;
        invalidLayout[0].type = AshaderPropertyType::Float;
        if (!rejected(document, shader, invalidLayout, 48, MatParameterError::InvalidLayout)) {
            return false;
        }
        invalidLayout.pop_back();
        if (!rejected(document, shader, invalidLayout, 48, MatParameterError::InvalidLayout)) {
            return false;
        }
        if (!rejected(document, shader, members, kMaxMatParameterBytes + 4,
                      MatParameterError::BudgetExceeded)) {
            return false;
        }
        for (double value :
             {std::numeric_limits<double>::max(), std::numeric_limits<double>::denorm_min()}) {
            auto invalid = document;
            invalid.properties[0].value.numberValue = value;
            if (!rejected(invalid, shader, members, 48, MatParameterError::InvalidValue)) {
                return false;
            }
        }
        auto invalid = document;
        invalid.properties[0].value.numberValue = std::numeric_limits<double>::infinity();
        if (!rejected(invalid, shader, members, 48, MatParameterError::InvalidInput)) {
            return false;
        }
        auto badShader = shader;
        badShader.properties[0].defaultValue.elements = {"1", "2"};
        if (!rejected(document, badShader, members, 48, MatParameterError::InvalidValue)) {
            return false;
        }
        badShader = shader;
        badShader.properties[2].defaultValue.text = "2147483648";
        if (!rejected(document, badShader, members, 48, MatParameterError::InvalidValue)) {
            return false;
        }
        badShader = shader;
        badShader.properties[3].defaultValue.text = "4294967296";
        if (!rejected(document, badShader, members, 48, MatParameterError::InvalidValue)) {
            return false;
        }
        badShader = shader;
        badShader.properties[0].defaultValue = {};
        if (!rejected(document, badShader, members, 48, MatParameterError::InvalidValue)) {
            return false;
        }
        badShader.properties[0].type = AshaderPropertyType::Texture2D;
        if (!rejected(document, badShader, members, 48, MatParameterError::UnsupportedType)) {
            return false;
        }
        badShader = shader;
        badShader.properties[1].name = badShader.properties[0].name;
        return rejected(document, badShader, members, 48, MatParameterError::InvalidInput);
    }

    bool checkParameterNumericBoundaries(asharia::material_instance::MatDocument document,
                                         asharia::shader_authoring::AshaderDocument shader) {
        using namespace asharia::material_instance;
        using namespace asharia::shader_authoring;
        shader.properties = {shader.properties[1]};
        const std::vector<MatParameterMember> scalar{
            {.propertyId = "gain", .type = AshaderPropertyType::Float, .byteOffset = 0}};
        for (double value :
             {static_cast<double>(std::numeric_limits<float>::max()),
              -static_cast<double>(std::numeric_limits<float>::max()),
              static_cast<double>(std::numeric_limits<float>::denorm_min()), 0.1, -0.0}) {
            document.properties[0].value.numberValue = value;
            if (!packMatParameters(document, shader, scalar, 4)) {
                logFailure("representable float or ordinary rounding rejected");
                return false;
            }
        }
        document.properties.clear();
        auto defaulted = packMatParameters(document, shader, scalar, 4);
        if (!defaulted ||
            defaulted->bytes !=
                std::vector<std::byte>{std::byte{0}, std::byte{0}, std::byte{128}, std::byte{62}}) {
            return false;
        }
        std::size_t width = 1;
        for (auto type : {AshaderPropertyType::Float2, AshaderPropertyType::Float3,
                          AshaderPropertyType::Float4}) {
            auto& property = shader.properties[0];
            property.type = type;
            ++width;
            property.defaultValue = {.kind = AshaderPropertyDefaultKind::Vector,
                                     .elements = std::vector<std::string>(width, "1")};
            const std::vector<MatParameterMember> layout{
                {.propertyId = "gain", .type = type, .byteOffset = 0}};
            auto vector = packMatParameters(document, shader, layout, 16);
            if (!vector || vector->bytes[(width * 4) - 1] != std::byte{63}) {
                return false;
            }
            property.defaultValue.elements[0] = "nan";
            if (packMatParameters(document, shader, layout, 16)) {
                return false;
            }
        }
        shader.properties.clear();
        auto empty = packMatParameters(document, shader, {}, 0);
        return empty && empty->bytes.empty();
    }

    bool smokeParameterPacking() {
        using namespace asharia::material_instance;
        using namespace asharia::shader_authoring;
        auto parsed = parseAshaderDocument(R"(
schema 2
shader "asharia.material.unlit" {
 properties {
  color tint = [1, 0.5, 0, 1]
  float gain = 0.25
  int mode = -2
  uint mask = 4294967295
  bool enabled = true
 }
 pass "Forward" { vertex vertexMain fragment fragmentMain slang "Unlit.slang" }
})");
        auto document = readMatText(R"({"schemaVersion":2,"materialType":{
"assetGuid":"11111111-1111-1111-1111-111111111111",
"stableTypeId":"asharia.material.unlit","expectedTypeHash":"00000000000000aa"},
"properties":{"gain":{"propertyId":"gain","type":"float","value":2.0}},
"import":{"lastCookedSignatureHash":"00000000000000bb"}})");
        if (!parsed.document || !document) {
            logFailure("parameter fixture parse failed");
            return false;
        }
        auto shader = *parsed.document;
        if (!checkParameterNumericBoundaries(*document, shader)) {
            return false;
        }
        std::vector<MatParameterMember> members{
            {.propertyId = "gain", .type = AshaderPropertyType::Float, .byteOffset = 0},
            {.propertyId = "tint", .type = AshaderPropertyType::Color, .byteOffset = 16},
            {.propertyId = "mode", .type = AshaderPropertyType::Int, .byteOffset = 32},
            {.propertyId = "mask", .type = AshaderPropertyType::UInt, .byteOffset = 36},
            {.propertyId = "enabled", .type = AshaderPropertyType::Bool, .byteOffset = 40}};
        const auto original = MatDocument{*document};
        auto block = packMatParameters(*document, shader, members, 48);
        // Explicit byte oracle: float 2, padding, float4(1,.5,0,1), -2, UINT_MAX, bool32(1).
        const std::vector<unsigned char> expected{
            0,   0,   0,   64,  0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0,   0,
            0,   0,   128, 63,  0,   0,   0,   63,  0, 0, 0, 0, 0, 0, 128, 63,
            254, 255, 255, 255, 255, 255, 255, 255, 1, 0, 0, 0, 0, 0, 0,   0};
        if (!block || *document != original ||
            !std::ranges::equal(block->bytes, expected, {}, [](std::byte byte) {
                return std::to_integer<unsigned char>(byte);
            })) {
            logFailure("parameter bytes/defaults/padding differ from oracle");
            return false;
        }
        std::ranges::reverse(members);
        auto repeated = packMatParameters(*document, shader, members, 48);
        if (!repeated || repeated->bytes != block->bytes) {
            return false;
        }
        if (!checkParameterFailures(*document, shader, members)) {
            return false;
        }
        const auto stale =
            packMatParameters(*document, shader, members, 48, {.currentMaterialTypeHash = 123});
        return stale && stale->bytes == block->bytes && !stale->diagnostics.empty();
    }

} // namespace

int main() noexcept {
    try {
        bool testsPassed = true;
        testsPassed = smokeReadWriteResolve() && testsPassed;
        testsPassed = smokeDiagnostics() && testsPassed;
        testsPassed = smokeProgrammaticValueValidation() && testsPassed;
        testsPassed = smokeParameterPacking() && testsPassed;
        return testsPassed ? 0 : 1;
    } catch (...) {
        return 1;
    }
}
