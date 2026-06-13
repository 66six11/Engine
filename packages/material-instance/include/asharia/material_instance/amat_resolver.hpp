#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/material_instance/amat_document.hpp"

namespace asharia::material_instance {

    enum class AmatDiagnosticSeverity {
        Warning,
        Error,
    };

    enum class AmatDiagnosticTarget {
        Document,
        MaterialType,
        Property,
        Import,
    };

    enum class AmatDiagnosticCode {
        MaterialTypeMismatch,
        UnknownProperty,
        PropertyTypeMismatch,
        StaleMaterialTypeHash,
        StaleSignatureHash,
        InvalidOverride,
    };

    enum class AmatOverrideDiffKind {
        Defaulted,
        Overridden,
        Invalid,
    };

    struct AmatDiagnostic {
        AmatDiagnosticSeverity severity{AmatDiagnosticSeverity::Error};
        AmatDiagnosticCode code{AmatDiagnosticCode::InvalidOverride};
        AmatDiagnosticTarget target{AmatDiagnosticTarget::Document};
        std::string propertyId;
        std::string message;
    };

    struct AmatOverrideDiff {
        AmatOverrideDiffKind kind{AmatOverrideDiffKind::Defaulted};
        std::string propertyId;
        shader_authoring::AshaderPropertyType declaredType{
            shader_authoring::AshaderPropertyType::Float};
        std::optional<shader_authoring::AshaderPropertyType> overrideType;
    };

    struct AmatResolveOptions {
        std::optional<std::uint64_t> currentMaterialTypeHash;
        std::optional<std::uint64_t> currentSignatureHash;
    };

    struct AmatResolveResult {
        std::vector<AmatOverrideDiff> overrides;
        std::vector<AmatDiagnostic> diagnostics;
    };

    [[nodiscard]] std::string_view toString(AmatDiagnosticSeverity severity) noexcept;
    [[nodiscard]] std::string_view toString(AmatDiagnosticCode code) noexcept;
    [[nodiscard]] std::string_view toString(AmatDiagnosticTarget target) noexcept;
    [[nodiscard]] std::string_view toString(AmatOverrideDiffKind kind) noexcept;
    [[nodiscard]] bool hasErrors(const std::vector<AmatDiagnostic>& diagnostics);

    [[nodiscard]] AmatResolveResult
    resolveAmatOverrides(const AmatDocument& document,
                         const shader_authoring::AshaderDocument& shader,
                         const AmatResolveOptions& options = {});

} // namespace asharia::material_instance
