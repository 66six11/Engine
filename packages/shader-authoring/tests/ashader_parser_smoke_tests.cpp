#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "asharia/shader_authoring/ashader_parser.hpp"

namespace {

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    bool containsCode(const asharia::shader_authoring::AshaderParseResult& result,
                      asharia::shader_authoring::AshaderDiagnosticCode code) {
        return std::ranges::any_of(
            result.diagnostics, [code](const auto& diagnostic) { return diagnostic.code == code; });
    }

    bool expectNoErrors(const asharia::shader_authoring::AshaderParseResult& result,
                        std::string_view label) {
        if (!result.document) {
            logFailure(std::string{label} + " did not produce a document.");
            return false;
        }
        if (asharia::shader_authoring::hasErrors(result.diagnostics)) {
            logFailure(std::string{label} + " produced diagnostics unexpectedly.");
            for (const auto& diagnostic : result.diagnostics) {
                logFailure(diagnostic.message);
            }
            return false;
        }
        return true;
    }

    bool expectDiagnostic(std::string_view source,
                          asharia::shader_authoring::AshaderDiagnosticCode code,
                          std::string_view label) {
        const auto result = asharia::shader_authoring::parseAshaderDocument(source);
        if (!containsCode(result, code)) {
            logFailure(std::string{label} + " did not produce expected diagnostic " +
                       std::string{asharia::shader_authoring::toString(code)} + ".");
            return false;
        }
        return true;
    }

    bool smokeUnlitDocument() {
        constexpr std::string_view kSource = R"ashader(
schema 2

shader "asharia.material.unlit" {
  properties {
    color baseColor = [1, 1, 1, 1]
    texture2D albedoMap
    sampler linearSampler
    float roughness = 0.5
    uint layer = 2
    bool enabled = true
  }

  pass "Forward" {
    tag "SceneForward"
    vertex vertexMain
    fragment fragmentMain
    cull back
    depthTest lessEqual
    depthWrite true
    blend disabled
    slang "Unlit.slang"
    graph "Unlit.agraph"
  }
}
)ashader";

        const auto result = asharia::shader_authoring::parseAshaderDocument(kSource);
        if (!expectNoErrors(result, "unlit .ashader")) {
            return false;
        }

        const auto& document = *result.document;
        if (document.schemaVersion != 2 || document.shaderTypeId != "asharia.material.unlit" ||
            document.properties.size() != 6 || document.passes.size() != 1 ||
            document.slangFiles.size() != 1 || document.graphFiles.size() != 1) {
            logFailure("unlit .ashader produced the wrong document shape.");
            return false;
        }

        const auto& pass = document.passes.front();
        if (pass.name != "Forward" || pass.tag != "SceneForward" ||
            pass.vertexEntry != "vertexMain" || pass.fragmentEntry != "fragmentMain" ||
            pass.computeEntry.has_value() || pass.cullMode != "back" ||
            pass.depthTest != "lessEqual" || pass.depthWrite != true ||
            pass.blendMode != "disabled" || pass.slangFiles.size() != 1 ||
            pass.graphFiles.size() != 1) {
            logFailure("unlit .ashader pass facts were not captured.");
            return false;
        }

        if (document.properties.front().type !=
                asharia::shader_authoring::AshaderPropertyType::Color ||
            document.properties.front().defaultValue.elements.size() != 4 ||
            document.properties[3].defaultValue.text != "0.5" ||
            document.properties[5].defaultValue.text != "true") {
            logFailure("unlit .ashader property facts were not captured.");
            return false;
        }

        std::cout << "Parsed .ashader shader: " << document.shaderTypeId << '\n';
        return true;
    }

    bool smokeRawSlangBlock() {
        constexpr std::string_view kSource = R"ashader(
schema 2

shader "asharia.material.debug_color" {
  properties {
    color tint = [1, 0, 1, 1]
  }

  pass "Forward" {
    vertex vertexMain
    fragment fragmentMain
  }

  slang {
    float4 shadeMaterial() {
      if (true) {
        return Material.tint;
      }
      return float4(1, 0, 1, 1);
    }
  }
}
)ashader";

        const auto result = asharia::shader_authoring::parseAshaderDocument(kSource);
        if (!expectNoErrors(result, "raw slang .ashader")) {
            return false;
        }

        const auto& document = *result.document;
        const auto& raw = document.rawSlang;
        if (!raw || raw->text.find("return Material.tint") == std::string::npos ||
            raw->bodySpan.begin.line >= raw->bodySpan.end.line) {
            logFailure("raw slang block body/span was not captured.");
            return false;
        }

        return true;
    }

    bool smokeDiagnostics() {
        constexpr std::string_view kDuplicateProperty = R"ashader(
schema 2
shader "dup" {
  properties {
    float value
    color value = [1, 1, 1, 1]
  }
  pass "Forward" { vertex vertexMain slang "Dup.slang" }
}
)ashader";

        constexpr std::string_view kUnknownType = R"ashader(
schema 2
shader "bad.type" {
  properties { matrix4 model }
  pass "Forward" { vertex vertexMain slang "Bad.slang" }
}
)ashader";

        constexpr std::string_view kInvalidDefaults = R"ashader(
schema 2
shader "bad.defaults" {
  properties {
    color tint = [1, 1, 1]
    uint layer = -1
    bool enabled = 1
    texture2D albedo = 1
  }
  pass "Forward" { vertex vertexMain slang "Bad.slang" }
}
)ashader";

        constexpr std::string_view kMissingEntry = R"ashader(
schema 2
shader "missing.entry" {
  properties { float value = 1 }
  pass "Forward" { tag "SceneForward" slang "Missing.slang" }
}
)ashader";

        constexpr std::string_view kUnsupportedSchema = R"ashader(
schema 1
shader "old" {
  properties { float value = 1 }
  pass "Forward" { vertex vertexMain slang "Old.slang" }
}
)ashader";

        constexpr std::string_view kMissingSlang = R"ashader(
schema 2
shader "missing.slang" {
  properties { float value = 1 }
  pass "Forward" { vertex vertexMain }
}
)ashader";

        constexpr std::string_view kUnbalancedRaw = R"ashader(
schema 2
shader "raw.bad" {
  properties { float value = 1 }
  pass "Forward" { vertex vertexMain }
  slang {
    float4 shadeMaterial() {
      return float4(1, 1, 1, 1);
)ashader";

        return expectDiagnostic(kDuplicateProperty,
                                asharia::shader_authoring::AshaderDiagnosticCode::DuplicateProperty,
                                "duplicate property") &&
               expectDiagnostic(
                   kUnknownType,
                   asharia::shader_authoring::AshaderDiagnosticCode::UnknownPropertyType,
                   "unknown type") &&
               expectDiagnostic(
                   kInvalidDefaults,
                   asharia::shader_authoring::AshaderDiagnosticCode::InvalidDefaultValue,
                   "invalid defaults") &&
               expectDiagnostic(kMissingEntry,
                                asharia::shader_authoring::AshaderDiagnosticCode::MissingPassEntry,
                                "missing pass entry") &&
               expectDiagnostic(kUnsupportedSchema,
                                asharia::shader_authoring::AshaderDiagnosticCode::UnsupportedSchema,
                                "unsupported schema") &&
               expectDiagnostic(
                   kMissingSlang,
                   asharia::shader_authoring::AshaderDiagnosticCode::MissingSlangReference,
                   "missing slang") &&
               expectDiagnostic(
                   kUnbalancedRaw,
                   asharia::shader_authoring::AshaderDiagnosticCode::UnbalancedRawSlangBlock,
                   "unbalanced raw slang");
    }

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    if (!smokeUnlitDocument() || !smokeRawSlangBlock() || !smokeDiagnostics()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
