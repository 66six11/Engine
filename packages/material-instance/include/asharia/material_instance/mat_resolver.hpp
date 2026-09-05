#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/material_instance/mat_document.hpp"

namespace asharia::material_instance {

    enum class MatDiagnosticSeverity {
        Warning,
        Error,
    };

    enum class MatDiagnosticTarget {
        Document,
        MaterialType,
        Property,
        Import,
    };

    enum class MatDiagnosticCode {
        MaterialTypeMismatch,
        UnknownProperty,
        PropertyTypeMismatch,
        StaleMaterialTypeHash,
        StaleSignatureHash,
        InvalidOverride,
    };

    enum class MatOverrideDiffKind {
        Defaulted,
        Overridden,
        Invalid,
    };

    struct MatDiagnostic {
        MatDiagnosticSeverity severity{MatDiagnosticSeverity::Error};
        MatDiagnosticCode code{MatDiagnosticCode::InvalidOverride};
        MatDiagnosticTarget target{MatDiagnosticTarget::Document};
        std::string propertyId;
        std::string message;
    };

    struct MatOverrideDiff {
        MatOverrideDiffKind kind{MatOverrideDiffKind::Defaulted};
        std::string propertyId;
        shader_authoring::ShaderPropertyType declaredType{
            shader_authoring::ShaderPropertyType::Float};
        std::optional<shader_authoring::ShaderPropertyType> overrideType;
    };

    struct MatResolveOptions {
        std::optional<std::uint64_t> currentMaterialTypeHash;
        std::optional<std::uint64_t> currentSignatureHash;
    };

    struct MatResolveResult {
        std::vector<MatOverrideDiff> overrides;
        std::vector<MatDiagnostic> diagnostics;
    };

    [[nodiscard]] std::string_view toString(MatDiagnosticSeverity severity) noexcept;
    [[nodiscard]] std::string_view toString(MatDiagnosticCode code) noexcept;
    [[nodiscard]] std::string_view toString(MatDiagnosticTarget target) noexcept;
    [[nodiscard]] std::string_view toString(MatOverrideDiffKind kind) noexcept;
    [[nodiscard]] bool hasErrors(const std::vector<MatDiagnostic>& diagnostics);

    [[nodiscard]] MatResolveResult
    resolveMatOverrides(const MatDocument& document,
                        const shader_authoring::ShaderDocument& shader,
                        const MatResolveOptions& options = {});

} // namespace asharia::material_instance
