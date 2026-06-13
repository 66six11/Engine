#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "asharia/shader_authoring/ashader_generated_slang.hpp"
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

    bool generatedContainsCode(const asharia::shader_authoring::GeneratedSlangResult& result,
                               asharia::shader_authoring::AshaderDiagnosticCode code) {
        return std::ranges::any_of(
            result.diagnostics, [code](const auto& diagnostic) { return diagnostic.code == code; });
    }

    bool hasLineMapSection(const asharia::shader_authoring::GeneratedSlangResult& result,
                           asharia::shader_authoring::GeneratedSlangSection section,
                           std::string_view label) {
        return std::ranges::any_of(result.lineMap, [section, label](const auto& entry) {
            return entry.section == section && entry.label == label;
        });
    }

    bool hasEntryPoint(const asharia::shader_authoring::GeneratedSlangResult& result,
                       asharia::shader_authoring::GeneratedSlangStage stage,
                       std::string_view passName, std::string_view sourceEntryName,
                       std::string_view generatedWrapperName) {
        return std::ranges::any_of(result.entryPoints, [&](const auto& entry) {
            return entry.stage == stage && entry.passName == passName &&
                   entry.sourceEntryName == sourceEntryName &&
                   entry.compileEntryName == sourceEntryName &&
                   entry.generatedWrapperName == generatedWrapperName &&
                   entry.sourceSpan.begin.line > 0;
        });
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

    bool smokeGeneratedSlangSkeleton() {
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
    slang "Unlit.slang"
  }

  pass "LightTiles" {
    compute computeMain
  }
}
)ashader";

        const auto parsed = asharia::shader_authoring::parseAshaderDocument(kSource);
        if (!expectNoErrors(parsed, "generated slang .ashader")) {
            return false;
        }

        const auto generated = asharia::shader_authoring::buildGeneratedSlang(
            *parsed.document, asharia::shader_authoring::GeneratedSlangOptions{
                                  .sourceName = "Assets/Shaders/Unlit/Unlit.ashader",
                                  .generatedName = "Unlit.generated.slang",
                              });

        if (asharia::shader_authoring::hasErrors(generated.diagnostics)) {
            logFailure("generated Slang skeleton produced diagnostics unexpectedly.");
            return false;
        }

        if (generated.source.find("struct __AshariaMaterialParams") == std::string::npos ||
            generated.source.find("[[vk::binding(0, 0)]]") == std::string::npos ||
            generated.source.find("ConstantBuffer<__AshariaMaterialParams> __ashariaMaterial;") ==
                std::string::npos ||
            generated.source.find("#define Material __ashariaMaterial") == std::string::npos ||
            generated.source.find("[[vk::binding(1, 0)]]") == std::string::npos ||
            generated.source.find("Texture2D<float4> albedoMap;") == std::string::npos ||
            generated.source.find("[[vk::binding(2, 0)]]") == std::string::npos ||
            generated.source.find("SamplerState linearSampler;") == std::string::npos ||
            generated.source.find("// External Slang reference: Unlit.slang") ==
                std::string::npos ||
            generated.source.find("void __asharia_Forward_vertex()") == std::string::npos ||
            generated.source.find("vertexMain();") == std::string::npos ||
            generated.source.find("void __asharia_Forward_fragment()") == std::string::npos ||
            generated.source.find("fragmentMain();") == std::string::npos ||
            generated.source.find("void __asharia_LightTiles_compute()") == std::string::npos ||
            generated.source.find("computeMain();") == std::string::npos) {
            logFailure("generated Slang skeleton text is missing expected sections.");
            return false;
        }

        if (generated.bindings.size() != 6 || generated.bindings[0].name != "baseColor" ||
            generated.bindings[0].binding != 0 || !generated.bindings[0].inMaterialParameterBlock ||
            generated.bindings[4].name != "albedoMap" || generated.bindings[4].binding != 1 ||
            generated.bindings[4].inMaterialParameterBlock ||
            generated.bindings[5].name != "linearSampler" || generated.bindings[5].binding != 2) {
            logFailure("generated Slang binding facts are not deterministic.");
            return false;
        }

        if (generated.entryPoints.size() != 3 ||
            !hasEntryPoint(generated, asharia::shader_authoring::GeneratedSlangStage::Vertex,
                           "Forward", "vertexMain", "__asharia_Forward_vertex") ||
            !hasEntryPoint(generated, asharia::shader_authoring::GeneratedSlangStage::Fragment,
                           "Forward", "fragmentMain", "__asharia_Forward_fragment") ||
            !hasEntryPoint(generated, asharia::shader_authoring::GeneratedSlangStage::Compute,
                           "LightTiles", "computeMain", "__asharia_LightTiles_compute") ||
            asharia::shader_authoring::toString(
                asharia::shader_authoring::GeneratedSlangStage::Compute) != "compute") {
            logFailure("generated Slang entry manifest is not deterministic.");
            return false;
        }

        if (!hasLineMapSection(
                generated, asharia::shader_authoring::GeneratedSlangSection::MaterialParameterBlock,
                "material-parameters") ||
            !hasLineMapSection(
                generated, asharia::shader_authoring::GeneratedSlangSection::ExternalSlangReference,
                "Unlit.slang") ||
            !hasLineMapSection(generated,
                               asharia::shader_authoring::GeneratedSlangSection::PassWrapper,
                               "Forward")) {
            logFailure("generated Slang line map is missing expected entries.");
            return false;
        }

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

    bool smokeGeneratedRawSlangMapping() {
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
      return Material.tint;
    }
  }
}
)ashader";

        const auto parsed = asharia::shader_authoring::parseAshaderDocument(kSource);
        if (!expectNoErrors(parsed, "generated raw slang .ashader")) {
            return false;
        }

        const auto generated = asharia::shader_authoring::buildGeneratedSlang(
            *parsed.document, asharia::shader_authoring::GeneratedSlangOptions{
                                  .sourceName = "Debug.ashader",
                                  .generatedName = "Debug.generated.slang",
                              });

        if (generated.source.find("#line") == std::string::npos ||
            generated.source.find("return Material.tint;") == std::string::npos ||
            !hasLineMapSection(generated,
                               asharia::shader_authoring::GeneratedSlangSection::RawSlangBlock,
                               "raw-slang")) {
            logFailure("generated raw Slang block or line mapping is missing.");
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

    bool smokeGeneratedSlangDiagnostics() {
        constexpr std::string_view kSource = R"ashader(
schema 2
shader "missing.entry" {
  properties { float value = 1 }
  pass "Forward" { tag "SceneForward" slang "Missing.slang" }
}
)ashader";

        const auto parsed = asharia::shader_authoring::parseAshaderDocument(kSource);
        if (!parsed.document) {
            logFailure("generated Slang diagnostic fixture did not parse.");
            return false;
        }

        const auto generated = asharia::shader_authoring::buildGeneratedSlang(*parsed.document);
        if (!generatedContainsCode(
                generated, asharia::shader_authoring::AshaderDiagnosticCode::MissingPassEntry)) {
            logFailure("generated Slang builder did not report missing pass entry.");
            return false;
        }

        return true;
    }

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    if (!smokeUnlitDocument() || !smokeGeneratedSlangSkeleton() || !smokeRawSlangBlock() ||
        !smokeGeneratedRawSlangMapping() || !smokeDiagnostics() ||
        !smokeGeneratedSlangDiagnostics()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
