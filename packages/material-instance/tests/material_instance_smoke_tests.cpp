#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "asharia/material_instance/amat_io.hpp"
#include "asharia/material_instance/amat_resolver.hpp"
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

    bool hasDiagnostic(const asharia::material_instance::AmatResolveResult& result,
                       asharia::material_instance::AmatDiagnosticCode code,
                       std::optional<std::string_view> propertyId = std::nullopt) {
        return std::ranges::any_of(result.diagnostics, [&](const auto& diagnostic) {
            return diagnostic.code == code && (!propertyId || diagnostic.propertyId == *propertyId);
        });
    }

    bool hasDiff(const asharia::material_instance::AmatResolveResult& result,
                 std::string_view propertyId,
                 asharia::material_instance::AmatOverrideDiffKind kind) {
        return std::ranges::any_of(result.overrides, [&](const auto& diff) {
            return diff.propertyId == propertyId && diff.kind == kind;
        });
    }

    bool smokeReadWriteResolve() {
        constexpr std::string_view kAmat = R"json(
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

        auto document = asharia::material_instance::readAmatText(kAmat);
        if (!document) {
            logFailure(document.error().message);
            return false;
        }
        if (document->properties.size() != 3 ||
            document->materialType.stableTypeId != "asharia.material.unlit" ||
            document->materialType.expectedTypeHash != kTypeHash ||
            document->import.lastCookedSignatureHash != kSignatureHash) {
            logFailure("read .amat document did not preserve expected fields.");
            return false;
        }

        auto written = asharia::material_instance::writeAmatText(*document);
        if (!written) {
            logFailure(written.error().message);
            return false;
        }
        if (written->find(R"("baseColor")") == std::string::npos ||
            written->find(R"("lastCookedSignatureHash": "00000000000000bb")") ==
                std::string::npos) {
            logFailure("written .amat text is missing stable fields.");
            return false;
        }
        auto roundTrip = asharia::material_instance::readAmatText(*written);
        if (!roundTrip || roundTrip->properties.size() != document->properties.size() ||
            roundTrip->materialType != document->materialType ||
            roundTrip->import != document->import) {
            logFailure("written .amat text did not round-trip.");
            return false;
        }

        const auto shader = makeShaderDocument();
        auto result = asharia::material_instance::resolveAmatOverrides(
            *document, shader,
            asharia::material_instance::AmatResolveOptions{
                .currentMaterialTypeHash = kTypeHash,
                .currentSignatureHash = kSignatureHash,
            });
        if (asharia::material_instance::hasErrors(result.diagnostics) ||
            result.overrides.size() != 4 ||
            !hasDiff(result, "baseColor",
                     asharia::material_instance::AmatOverrideDiffKind::Overridden) ||
            !hasDiff(result, "roughness",
                     asharia::material_instance::AmatOverrideDiffKind::Overridden) ||
            !hasDiff(result, "albedoMap",
                     asharia::material_instance::AmatOverrideDiffKind::Overridden) ||
            !hasDiff(result, "useAlpha",
                     asharia::material_instance::AmatOverrideDiffKind::Defaulted)) {
            logFailure("valid .amat overrides did not resolve deterministically.");
            return false;
        }

        return true;
    }

    bool smokeDiagnostics() {
        constexpr std::string_view kAmat = R"json(
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

        auto document = asharia::material_instance::readAmatText(kAmat);
        if (!document) {
            logFailure(document.error().message);
            return false;
        }

        const auto shader = makeShaderDocument();
        auto result = asharia::material_instance::resolveAmatOverrides(
            *document, shader,
            asharia::material_instance::AmatResolveOptions{
                .currentMaterialTypeHash = 0x00000000000000CCULL,
                .currentSignatureHash = 0x00000000000000DDULL,
            });
        if (!asharia::material_instance::hasErrors(result.diagnostics) ||
            !hasDiagnostic(result,
                           asharia::material_instance::AmatDiagnosticCode::PropertyTypeMismatch,
                           "roughness") ||
            !hasDiagnostic(result, asharia::material_instance::AmatDiagnosticCode::UnknownProperty,
                           "missingProperty") ||
            !hasDiagnostic(result,
                           asharia::material_instance::AmatDiagnosticCode::StaleMaterialTypeHash) ||
            !hasDiagnostic(result,
                           asharia::material_instance::AmatDiagnosticCode::StaleSignatureHash) ||
            !hasDiff(result, "roughness",
                     asharia::material_instance::AmatOverrideDiffKind::Invalid)) {
            logFailure("invalid .amat fixture did not produce deterministic diagnostics.");
            return false;
        }

        auto malformed = asharia::material_instance::readAmatText("{");
        if (malformed) {
            logFailure("malformed JSON unexpectedly parsed.");
            return false;
        }

        auto unsupportedSchema = asharia::material_instance::readAmatText(R"json({
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

} // namespace

int main() noexcept {
    try {
        bool testsPassed = true;
        testsPassed = smokeReadWriteResolve() && testsPassed;
        testsPassed = smokeDiagnostics() && testsPassed;
        return testsPassed ? 0 : 1;
    } catch (...) {
        return 1;
    }
}
