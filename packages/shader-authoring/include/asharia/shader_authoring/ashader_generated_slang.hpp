#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/shader_authoring/ashader_document.hpp"

namespace asharia::shader_authoring {

    enum class GeneratedSlangSection {
        Header,
        MaterialParameterBlock,
        ResourceDeclaration,
        MaterialAccessShim,
        ExternalSlangReference,
        RawSlangBlock,
        PassWrapper,
    };

    struct GeneratedSlangLineMapEntry {
        GeneratedSlangSection section{GeneratedSlangSection::Header};
        std::string label;
        std::uint32_t generatedBeginLine{1};
        std::uint32_t generatedEndLine{1};
        std::string sourceName;
        SourceSpan sourceSpan{};
    };

    struct GeneratedSlangBinding {
        std::string name;
        AshaderPropertyType type{AshaderPropertyType::Float};
        std::uint32_t set{0};
        std::uint32_t binding{0};
        bool inMaterialParameterBlock{false};
    };

    struct GeneratedSlangOptions {
        std::string sourceName{"<ashader>"};
        std::string generatedName{"<generated-ashader>"};
        std::string materialParameterStructName{"__AshariaMaterialParams"};
        std::string materialParameterBindingName{"__ashariaMaterial"};
        std::uint32_t materialSet{0};
        std::uint32_t materialParameterBinding{0};
        std::uint32_t firstResourceBinding{1};
    };

    struct GeneratedSlangResult {
        std::string source;
        std::vector<GeneratedSlangLineMapEntry> lineMap;
        std::vector<GeneratedSlangBinding> bindings;
        std::vector<AshaderDiagnostic> diagnostics;
    };

    std::string_view toString(GeneratedSlangSection section);
    GeneratedSlangResult buildGeneratedSlang(const AshaderDocument& document,
                                             const GeneratedSlangOptions& options = {});

} // namespace asharia::shader_authoring
