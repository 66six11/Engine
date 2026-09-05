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

    enum class ShaderDiagnosticSeverity {
        Warning,
        Error,
    };

    enum class ShaderDiagnosticTarget {
        File,
        Shader,
        Property,
        Pass,
        SlangReference,
        RawSlangBlock,
        GraphReference,
    };

    enum class ShaderDiagnosticCode {
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

    struct ShaderDiagnostic {
        ShaderDiagnosticSeverity severity{ShaderDiagnosticSeverity::Error};
        ShaderDiagnosticCode code{ShaderDiagnosticCode::UnexpectedToken};
        ShaderDiagnosticTarget target{ShaderDiagnosticTarget::File};
        SourceSpan span{};
        std::string message;
    };

    enum class ShaderPropertyType {
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

    enum class ShaderPropertyDefaultKind {
        None,
        Number,
        Integer,
        Boolean,
        Vector,
    };

    struct ShaderPropertyDefault {
        ShaderPropertyDefaultKind kind{ShaderPropertyDefaultKind::None};
        std::string text;
        std::vector<std::string> elements;
        SourceSpan span{};
    };

    struct ShaderPropertyDecl {
        ShaderPropertyType type{ShaderPropertyType::Float};
        std::string typeName;
        std::string name;
        ShaderPropertyDefault defaultValue{};
        SourceSpan span{};
        SourceSpan typeSpan{};
        SourceSpan nameSpan{};
    };

    struct ShaderSourceReference {
        std::string path;
        SourceSpan span{};
    };

    struct ShaderRawSlangBlock {
        std::string text;
        SourceSpan span{};
        SourceSpan bodySpan{};
    };

    struct ShaderPassDecl {
        std::string name;
        std::optional<std::string> tag;
        std::optional<std::string> vertexEntry;
        std::optional<std::string> fragmentEntry;
        std::optional<std::string> computeEntry;
        std::optional<std::string> cullMode;
        std::optional<std::string> depthTest;
        std::optional<bool> depthWrite;
        std::optional<std::string> blendMode;
        std::vector<ShaderSourceReference> slangFiles;
        std::vector<ShaderSourceReference> graphFiles;
        SourceSpan span{};
        SourceSpan nameSpan{};
    };

    struct ShaderDocument {
        std::uint32_t schemaVersion{0};
        std::string shaderTypeId;
        std::vector<ShaderPropertyDecl> properties;
        std::vector<ShaderPassDecl> passes;
        std::vector<ShaderSourceReference> slangFiles;
        std::vector<ShaderSourceReference> graphFiles;
        std::optional<ShaderRawSlangBlock> rawSlang;
        SourceSpan fullSpan{};
    };

    struct ShaderParseResult {
        std::optional<ShaderDocument> document;
        std::vector<ShaderDiagnostic> diagnostics;
    };

    std::string_view toString(ShaderDiagnosticSeverity severity);
    std::string_view toString(ShaderDiagnosticCode code);
    std::string_view toString(ShaderPropertyType type);
    bool hasErrors(const std::vector<ShaderDiagnostic>& diagnostics);

} // namespace asharia::shader_authoring
