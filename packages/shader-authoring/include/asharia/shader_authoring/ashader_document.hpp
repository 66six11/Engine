#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asharia::shader_authoring {

    struct SourcePosition {
        std::uint32_t line{1};
        std::uint32_t column{1};
        std::uint32_t offset{0};
    };

    struct SourceSpan {
        SourcePosition begin{};
        SourcePosition end{};
    };

    enum class AshaderDiagnosticSeverity {
        Warning,
        Error,
    };

    enum class AshaderDiagnosticTarget {
        File,
        Shader,
        Property,
        Pass,
        SlangReference,
        RawSlangBlock,
        GraphReference,
    };

    enum class AshaderDiagnosticCode {
        ExpectedToken,
        UnexpectedToken,
        UnsupportedSchema,
        DuplicateProperty,
        UnknownPropertyType,
        InvalidDefaultValue,
        MissingPassEntry,
        MissingSlangReference,
        UnbalancedRawSlangBlock,
        GeneratedSlangUnsupportedInput,
    };

    struct AshaderDiagnostic {
        AshaderDiagnosticSeverity severity{AshaderDiagnosticSeverity::Error};
        AshaderDiagnosticCode code{AshaderDiagnosticCode::UnexpectedToken};
        AshaderDiagnosticTarget target{AshaderDiagnosticTarget::File};
        SourceSpan span{};
        std::string message;
    };

    enum class AshaderPropertyType {
        Float,
        Float2,
        Float3,
        Float4,
        Color,
        Int,
        UInt,
        Bool,
        Texture2D,
        Sampler,
    };

    enum class AshaderPropertyDefaultKind {
        None,
        Number,
        Integer,
        Boolean,
        Vector,
    };

    struct AshaderPropertyDefault {
        AshaderPropertyDefaultKind kind{AshaderPropertyDefaultKind::None};
        std::string text;
        std::vector<std::string> elements;
        SourceSpan span{};
    };

    struct AshaderPropertyDecl {
        AshaderPropertyType type{AshaderPropertyType::Float};
        std::string typeName;
        std::string name;
        AshaderPropertyDefault defaultValue{};
        SourceSpan span{};
        SourceSpan typeSpan{};
        SourceSpan nameSpan{};
    };

    struct AshaderSourceReference {
        std::string path;
        SourceSpan span{};
    };

    struct AshaderRawSlangBlock {
        std::string text;
        SourceSpan span{};
        SourceSpan bodySpan{};
    };

    struct AshaderPassDecl {
        std::string name;
        std::optional<std::string> tag;
        std::optional<std::string> vertexEntry;
        std::optional<std::string> fragmentEntry;
        std::optional<std::string> computeEntry;
        std::optional<std::string> cullMode;
        std::optional<std::string> depthTest;
        std::optional<bool> depthWrite;
        std::optional<std::string> blendMode;
        std::vector<AshaderSourceReference> slangFiles;
        std::vector<AshaderSourceReference> graphFiles;
        SourceSpan span{};
        SourceSpan nameSpan{};
    };

    struct AshaderDocument {
        std::uint32_t schemaVersion{0};
        std::string shaderTypeId;
        std::vector<AshaderPropertyDecl> properties;
        std::vector<AshaderPassDecl> passes;
        std::vector<AshaderSourceReference> slangFiles;
        std::vector<AshaderSourceReference> graphFiles;
        std::optional<AshaderRawSlangBlock> rawSlang;
        SourceSpan fullSpan{};
    };

    struct AshaderParseResult {
        std::optional<AshaderDocument> document;
        std::vector<AshaderDiagnostic> diagnostics;
    };

    std::string_view toString(AshaderDiagnosticSeverity severity);
    std::string_view toString(AshaderDiagnosticCode code);
    std::string_view toString(AshaderPropertyType type);
    bool hasErrors(const std::vector<AshaderDiagnostic>& diagnostics);

} // namespace asharia::shader_authoring
