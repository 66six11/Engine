#include "asharia/shader_authoring/ashader_generated_slang.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace asharia::shader_authoring {

    namespace {

        bool isMaterialParameter(AshaderPropertyType type) {
            switch (type) {
            case AshaderPropertyType::Float:
            case AshaderPropertyType::Float2:
            case AshaderPropertyType::Float3:
            case AshaderPropertyType::Float4:
            case AshaderPropertyType::Color:
            case AshaderPropertyType::Int:
            case AshaderPropertyType::UInt:
            case AshaderPropertyType::Bool:
                return true;
            case AshaderPropertyType::Texture2D:
            case AshaderPropertyType::Sampler:
                return false;
            }
            return false;
        }

        std::string_view slangTypeName(AshaderPropertyType type) {
            switch (type) {
            case AshaderPropertyType::Float:
                return "float";
            case AshaderPropertyType::Float2:
                return "float2";
            case AshaderPropertyType::Float3:
                return "float3";
            case AshaderPropertyType::Float4:
            case AshaderPropertyType::Color:
                return "float4";
            case AshaderPropertyType::Int:
                return "int";
            case AshaderPropertyType::UInt:
                return "uint";
            case AshaderPropertyType::Bool:
                return "bool";
            case AshaderPropertyType::Texture2D:
                return "Texture2D<float4>";
            case AshaderPropertyType::Sampler:
                return "SamplerState";
            }
            return "unknown";
        }

        std::string sanitizeIdentifier(std::string_view value) {
            std::string result;
            result.reserve(value.size());
            for (char character : value) {
                const auto unsignedCharacter = static_cast<unsigned char>(character);
                if (std::isalnum(unsignedCharacter) != 0 || character == '_') {
                    result.push_back(character);
                } else {
                    result.push_back('_');
                }
            }
            if (result.empty()) {
                result = "unnamed";
            }
            if (std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
                result.insert(result.begin(), '_');
            }
            return result;
        }

        std::uint32_t countLines(std::string_view text) {
            if (text.empty()) {
                return 0;
            }
            const auto newlineCount = static_cast<std::uint32_t>(std::ranges::count(text, '\n'));
            return text.back() == '\n' ? newlineCount : newlineCount + 1U;
        }

        class SlangEmitter {
        public:
            void appendLine(std::string_view line = {}) {
                source_ += line;
                source_ += '\n';
                ++nextLine_;
            }

            void appendBlock(std::string_view block) {
                source_ += block;
                if (!block.empty() && block.back() != '\n') {
                    source_ += '\n';
                }
                const std::uint32_t lines = countLines(block);
                nextLine_ += std::max<std::uint32_t>(lines, 1U);
            }

            [[nodiscard]] std::uint32_t nextLine() const {
                return nextLine_;
            }

            [[nodiscard]] const std::string& source() const {
                return source_;
            }

        private:
            std::string source_;
            std::uint32_t nextLine_{1};
        };

        void addDiagnostic(std::vector<AshaderDiagnostic>& diagnostics, AshaderDiagnosticCode code,
                           AshaderDiagnosticTarget target, SourceSpan span, std::string message) {
            diagnostics.push_back(AshaderDiagnostic{
                .severity = AshaderDiagnosticSeverity::Error,
                .code = code,
                .target = target,
                .span = span,
                .message = std::move(message),
            });
        }

        void addLineMap(std::vector<GeneratedSlangLineMapEntry>& lineMap,
                        GeneratedSlangSection section, std::string label, std::uint32_t beginLine,
                        std::uint32_t endLine, std::string sourceName, SourceSpan sourceSpan) {
            lineMap.push_back(GeneratedSlangLineMapEntry{
                .section = section,
                .label = std::move(label),
                .generatedBeginLine = beginLine,
                .generatedEndLine = endLine,
                .sourceName = std::move(sourceName),
                .sourceSpan = sourceSpan,
            });
        }

        bool passHasEntry(const AshaderPassDecl& pass) {
            return pass.vertexEntry.has_value() || pass.fragmentEntry.has_value() ||
                   pass.computeEntry.has_value();
        }

        void addEntryPoint(GeneratedSlangResult& result, const AshaderPassDecl& pass,
                           GeneratedSlangStage stage, std::string_view sourceEntryName,
                           std::string_view generatedWrapperName) {
            result.entryPoints.push_back(GeneratedSlangEntryPoint{
                .passName = pass.name,
                .stage = stage,
                .sourceEntryName = std::string{sourceEntryName},
                .compileEntryName = std::string{sourceEntryName},
                .generatedWrapperName = std::string{generatedWrapperName},
                .sourceSpan = pass.span,
            });
        }

        void appendMaterialParameters(SlangEmitter& emitter, GeneratedSlangResult& result,
                                      const AshaderDocument& document,
                                      const GeneratedSlangOptions& options) {
            const bool hasMaterialParameters =
                std::ranges::any_of(document.properties, [](const auto& property) {
                    return isMaterialParameter(property.type);
                });
            if (!hasMaterialParameters) {
                return;
            }

            const std::uint32_t beginLine = emitter.nextLine();
            emitter.appendLine("struct " + options.materialParameterStructName + " {");
            for (const auto& property : document.properties) {
                if (!isMaterialParameter(property.type)) {
                    continue;
                }
                emitter.appendLine("    " + std::string{slangTypeName(property.type)} + " " +
                                   property.name + ";");
                result.bindings.push_back(GeneratedSlangBinding{
                    .name = property.name,
                    .type = property.type,
                    .set = options.materialSet,
                    .binding = options.materialParameterBinding,
                    .inMaterialParameterBlock = true,
                });
            }
            emitter.appendLine("};");
            emitter.appendLine();
            emitter.appendLine("[[vk::binding(" + std::to_string(options.materialParameterBinding) +
                               ", " + std::to_string(options.materialSet) + ")]]");
            emitter.appendLine("ConstantBuffer<" + options.materialParameterStructName + "> " +
                               options.materialParameterBindingName + ";");
            const std::uint32_t endLine = emitter.nextLine() - 1U;
            emitter.appendLine();

            addLineMap(result.lineMap, GeneratedSlangSection::MaterialParameterBlock,
                       "material-parameters", beginLine, endLine, options.sourceName,
                       document.fullSpan);
        }

        void appendResourceDeclarations(SlangEmitter& emitter, GeneratedSlangResult& result,
                                        const AshaderDocument& document,
                                        const GeneratedSlangOptions& options) {
            std::uint32_t nextBinding = options.firstResourceBinding;
            for (const auto& property : document.properties) {
                if (isMaterialParameter(property.type)) {
                    continue;
                }

                const std::uint32_t beginLine = emitter.nextLine();
                emitter.appendLine("[[vk::binding(" + std::to_string(nextBinding) + ", " +
                                   std::to_string(options.materialSet) + ")]]");
                emitter.appendLine(std::string{slangTypeName(property.type)} + " " + property.name +
                                   ";");
                const std::uint32_t endLine = emitter.nextLine() - 1U;
                emitter.appendLine();

                result.bindings.push_back(GeneratedSlangBinding{
                    .name = property.name,
                    .type = property.type,
                    .set = options.materialSet,
                    .binding = nextBinding,
                    .inMaterialParameterBlock = false,
                });
                addLineMap(result.lineMap, GeneratedSlangSection::ResourceDeclaration,
                           property.name, beginLine, endLine, options.sourceName, property.span);
                ++nextBinding;
            }
        }

        void appendMaterialAccessShim(SlangEmitter& emitter, GeneratedSlangResult& result,
                                      const AshaderDocument& document,
                                      const GeneratedSlangOptions& options) {
            const bool hasMaterialParameters =
                std::ranges::any_of(document.properties, [](const auto& property) {
                    return isMaterialParameter(property.type);
                });
            if (!hasMaterialParameters) {
                return;
            }

            const std::uint32_t beginLine = emitter.nextLine();
            emitter.appendLine("#define Material " + options.materialParameterBindingName);
            const std::uint32_t endLine = emitter.nextLine() - 1U;
            emitter.appendLine();
            addLineMap(result.lineMap, GeneratedSlangSection::MaterialAccessShim,
                       "material-access-shim", beginLine, endLine, options.sourceName,
                       document.fullSpan);
        }

        void appendExternalSlangReferences(SlangEmitter& emitter, GeneratedSlangResult& result,
                                           const AshaderDocument& document,
                                           const GeneratedSlangOptions& options) {
            for (const auto& reference : document.slangFiles) {
                const std::uint32_t beginLine = emitter.nextLine();
                emitter.appendLine("// External Slang reference: " + reference.path);
                emitter.appendLine(
                    "// Cook/compile slice will resolve this file; this builder does not read it.");
                const std::uint32_t endLine = emitter.nextLine() - 1U;
                emitter.appendLine();
                addLineMap(result.lineMap, GeneratedSlangSection::ExternalSlangReference,
                           reference.path, beginLine, endLine, options.sourceName, reference.span);
            }
        }

        void appendRawSlangBlock(SlangEmitter& emitter, GeneratedSlangResult& result,
                                 const AshaderDocument& document,
                                 const GeneratedSlangOptions& options) {
            if (!document.rawSlang) {
                return;
            }

            const auto& rawSlang = *document.rawSlang;
            const std::uint32_t beginLine = emitter.nextLine();
            emitter.appendLine("#line " + std::to_string(rawSlang.bodySpan.begin.line) + " \"" +
                               options.sourceName + "\"");
            const std::uint32_t rawBeginLine = emitter.nextLine();
            emitter.appendBlock(rawSlang.text);
            const std::uint32_t rawEndLine = emitter.nextLine() - 1U;
            emitter.appendLine("#line " + std::to_string(emitter.nextLine() + 1U) + " \"" +
                               options.generatedName + "\"");
            emitter.appendLine();

            addLineMap(result.lineMap, GeneratedSlangSection::RawSlangBlock, "raw-slang",
                       rawBeginLine, rawEndLine, options.sourceName, rawSlang.bodySpan);
            addLineMap(result.lineMap, GeneratedSlangSection::Header, "raw-slang-line-directive",
                       beginLine, beginLine, options.sourceName, rawSlang.span);
        }

        void appendPassWrappers(SlangEmitter& emitter, GeneratedSlangResult& result,
                                const AshaderDocument& document,
                                const GeneratedSlangOptions& options) {
            for (const auto& pass : document.passes) {
                if (!passHasEntry(pass)) {
                    addDiagnostic(result.diagnostics, AshaderDiagnosticCode::MissingPassEntry,
                                  AshaderDiagnosticTarget::Pass, pass.span,
                                  std::string{"pass '"} + pass.name +
                                      "' requires vertex, fragment, or compute entry");
                    continue;
                }

                const std::string passId = sanitizeIdentifier(pass.name);
                const std::uint32_t beginLine = emitter.nextLine();
                emitter.appendLine("// Pass wrapper: " + pass.name);
                if (pass.tag) {
                    emitter.appendLine("// Tag: " + *pass.tag);
                }
                if (pass.vertexEntry) {
                    const std::string wrapperName = "__asharia_" + passId + "_vertex";
                    emitter.appendLine("void " + wrapperName + "() {");
                    emitter.appendLine("    " + *pass.vertexEntry + "();");
                    emitter.appendLine("}");
                    addEntryPoint(result, pass, GeneratedSlangStage::Vertex, *pass.vertexEntry,
                                  wrapperName);
                }
                if (pass.fragmentEntry) {
                    const std::string wrapperName = "__asharia_" + passId + "_fragment";
                    emitter.appendLine("void " + wrapperName + "() {");
                    emitter.appendLine("    " + *pass.fragmentEntry + "();");
                    emitter.appendLine("}");
                    addEntryPoint(result, pass, GeneratedSlangStage::Fragment, *pass.fragmentEntry,
                                  wrapperName);
                }
                if (pass.computeEntry) {
                    const std::string wrapperName = "__asharia_" + passId + "_compute";
                    emitter.appendLine("void " + wrapperName + "() {");
                    emitter.appendLine("    " + *pass.computeEntry + "();");
                    emitter.appendLine("}");
                    addEntryPoint(result, pass, GeneratedSlangStage::Compute, *pass.computeEntry,
                                  wrapperName);
                }
                const std::uint32_t endLine = emitter.nextLine() - 1U;
                emitter.appendLine();

                addLineMap(result.lineMap, GeneratedSlangSection::PassWrapper, pass.name, beginLine,
                           endLine, options.sourceName, pass.span);
            }
        }

    } // namespace

    std::string_view toString(GeneratedSlangSection section) {
        switch (section) {
        case GeneratedSlangSection::Header:
            return "header";
        case GeneratedSlangSection::MaterialParameterBlock:
            return "material-parameter-block";
        case GeneratedSlangSection::ResourceDeclaration:
            return "resource-declaration";
        case GeneratedSlangSection::MaterialAccessShim:
            return "material-access-shim";
        case GeneratedSlangSection::ExternalSlangReference:
            return "external-slang-reference";
        case GeneratedSlangSection::RawSlangBlock:
            return "raw-slang-block";
        case GeneratedSlangSection::PassWrapper:
            return "pass-wrapper";
        }
        return "unknown";
    }

    std::string_view toString(GeneratedSlangStage stage) {
        switch (stage) {
        case GeneratedSlangStage::Vertex:
            return "vertex";
        case GeneratedSlangStage::Fragment:
            return "fragment";
        case GeneratedSlangStage::Compute:
            return "compute";
        }
        return "unknown";
    }

    GeneratedSlangResult buildGeneratedSlang(const AshaderDocument& document,
                                             const GeneratedSlangOptions& options) {
        GeneratedSlangResult result{};
        SlangEmitter emitter{};

        const std::uint32_t headerBeginLine = emitter.nextLine();
        emitter.appendLine("// Generated Slang skeleton for " + document.shaderTypeId);
        emitter.appendLine("// This file is generated from .ashader authoring data.");
        const std::uint32_t headerEndLine = emitter.nextLine() - 1U;
        emitter.appendLine();
        addLineMap(result.lineMap, GeneratedSlangSection::Header, "generated-header",
                   headerBeginLine, headerEndLine, options.sourceName, document.fullSpan);

        if (document.schemaVersion != 2U) {
            addDiagnostic(result.diagnostics, AshaderDiagnosticCode::UnsupportedSchema,
                          AshaderDiagnosticTarget::File, document.fullSpan,
                          "generated Slang skeleton requires .ashader schema 2");
        }
        if (document.shaderTypeId.empty()) {
            addDiagnostic(result.diagnostics, AshaderDiagnosticCode::GeneratedSlangUnsupportedInput,
                          AshaderDiagnosticTarget::Shader, document.fullSpan,
                          "generated Slang skeleton requires a shader stable type id");
        }

        appendMaterialParameters(emitter, result, document, options);
        appendResourceDeclarations(emitter, result, document, options);
        appendMaterialAccessShim(emitter, result, document, options);
        appendExternalSlangReferences(emitter, result, document, options);
        appendRawSlangBlock(emitter, result, document, options);
        appendPassWrappers(emitter, result, document, options);

        result.source = emitter.source();
        return result;
    }

} // namespace asharia::shader_authoring
