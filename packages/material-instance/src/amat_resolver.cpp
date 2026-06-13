#include "asharia/material_instance/amat_resolver.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace asharia::material_instance {
    namespace {

        using shader_authoring::AshaderDocument;
        using shader_authoring::AshaderPropertyDecl;
        using shader_authoring::AshaderPropertyType;

        void addDiagnostic(AmatResolveResult& result, AmatDiagnosticSeverity severity,
                           AmatDiagnosticCode code, AmatDiagnosticTarget target,
                           std::string propertyId, std::string message) {
            result.diagnostics.push_back(AmatDiagnostic{
                .severity = severity,
                .code = code,
                .target = target,
                .propertyId = std::move(propertyId),
                .message = std::move(message),
            });
        }

        [[nodiscard]] const AmatPropertyOverride* findOverride(const AmatDocument& document,
                                                               std::string_view propertyId) {
            const auto found = std::ranges::find(document.properties, propertyId,
                                                 &AmatPropertyOverride::propertyId);
            if (found == document.properties.end()) {
                return nullptr;
            }
            return &*found;
        }

        [[nodiscard]] const AshaderPropertyDecl* findProperty(const AshaderDocument& shader,
                                                              std::string_view propertyId) {
            const auto found =
                std::ranges::find(shader.properties, propertyId, &AshaderPropertyDecl::name);
            if (found == shader.properties.end()) {
                return nullptr;
            }
            return &*found;
        }

    } // namespace

    std::string_view toString(AmatDiagnosticSeverity severity) noexcept {
        switch (severity) {
        case AmatDiagnosticSeverity::Warning:
            return "warning";
        case AmatDiagnosticSeverity::Error:
            return "error";
        }
        return "unknown";
    }

    std::string_view toString(AmatDiagnosticCode code) noexcept {
        switch (code) {
        case AmatDiagnosticCode::MaterialTypeMismatch:
            return "material_type_mismatch";
        case AmatDiagnosticCode::UnknownProperty:
            return "unknown_property";
        case AmatDiagnosticCode::PropertyTypeMismatch:
            return "property_type_mismatch";
        case AmatDiagnosticCode::StaleMaterialTypeHash:
            return "stale_material_type_hash";
        case AmatDiagnosticCode::StaleSignatureHash:
            return "stale_signature_hash";
        case AmatDiagnosticCode::InvalidOverride:
            return "invalid_override";
        }
        return "unknown";
    }

    std::string_view toString(AmatDiagnosticTarget target) noexcept {
        switch (target) {
        case AmatDiagnosticTarget::Document:
            return "document";
        case AmatDiagnosticTarget::MaterialType:
            return "material_type";
        case AmatDiagnosticTarget::Property:
            return "property";
        case AmatDiagnosticTarget::Import:
            return "import";
        }
        return "unknown";
    }

    std::string_view toString(AmatOverrideDiffKind kind) noexcept {
        switch (kind) {
        case AmatOverrideDiffKind::Defaulted:
            return "defaulted";
        case AmatOverrideDiffKind::Overridden:
            return "overridden";
        case AmatOverrideDiffKind::Invalid:
            return "invalid";
        }
        return "unknown";
    }

    bool hasErrors(const std::vector<AmatDiagnostic>& diagnostics) {
        return std::ranges::any_of(diagnostics, [](const AmatDiagnostic& diagnostic) {
            return diagnostic.severity == AmatDiagnosticSeverity::Error;
        });
    }

    AmatResolveResult resolveAmatOverrides(const AmatDocument& document,
                                           const AshaderDocument& shader,
                                           const AmatResolveOptions& options) {
        AmatResolveResult result;
        result.overrides.reserve(shader.properties.size());

        if (document.materialType.stableTypeId != shader.shaderTypeId) {
            addDiagnostic(
                result, AmatDiagnosticSeverity::Error, AmatDiagnosticCode::MaterialTypeMismatch,
                AmatDiagnosticTarget::MaterialType, {},
                "Amat stableTypeId '" + document.materialType.stableTypeId +
                    "' does not match .ashader shader type '" + shader.shaderTypeId + "'.");
        }

        if (options.currentMaterialTypeHash &&
            document.materialType.expectedTypeHash != *options.currentMaterialTypeHash) {
            addDiagnostic(result, AmatDiagnosticSeverity::Warning,
                          AmatDiagnosticCode::StaleMaterialTypeHash,
                          AmatDiagnosticTarget::MaterialType, {},
                          "Amat expectedTypeHash is stale for shader type '" +
                              document.materialType.stableTypeId + "'.");
        }

        if (options.currentSignatureHash &&
            document.import.lastCookedSignatureHash != *options.currentSignatureHash) {
            addDiagnostic(result, AmatDiagnosticSeverity::Warning,
                          AmatDiagnosticCode::StaleSignatureHash, AmatDiagnosticTarget::Import, {},
                          "Amat lastCookedSignatureHash is stale for shader type '" +
                              document.materialType.stableTypeId + "'.");
        }

        for (const AshaderPropertyDecl& property : shader.properties) {
            const AmatPropertyOverride* overrideValue = findOverride(document, property.name);
            if (overrideValue == nullptr) {
                result.overrides.push_back(AmatOverrideDiff{
                    .kind = AmatOverrideDiffKind::Defaulted,
                    .propertyId = property.name,
                    .declaredType = property.type,
                    .overrideType = std::nullopt,
                });
                continue;
            }

            if (overrideValue->type != property.type) {
                addDiagnostic(result, AmatDiagnosticSeverity::Error,
                              AmatDiagnosticCode::PropertyTypeMismatch,
                              AmatDiagnosticTarget::Property, overrideValue->propertyId,
                              "Amat property '" + overrideValue->propertyId + "' has type '" +
                                  std::string{shader_authoring::toString(overrideValue->type)} +
                                  "' but .ashader declares '" +
                                  std::string{shader_authoring::toString(property.type)} + "'.");
                result.overrides.push_back(AmatOverrideDiff{
                    .kind = AmatOverrideDiffKind::Invalid,
                    .propertyId = property.name,
                    .declaredType = property.type,
                    .overrideType = overrideValue->type,
                });
                continue;
            }

            result.overrides.push_back(AmatOverrideDiff{
                .kind = AmatOverrideDiffKind::Overridden,
                .propertyId = property.name,
                .declaredType = property.type,
                .overrideType = overrideValue->type,
            });
        }

        for (const AmatPropertyOverride& overrideValue : document.properties) {
            if (findProperty(shader, overrideValue.propertyId) != nullptr) {
                continue;
            }
            addDiagnostic(
                result, AmatDiagnosticSeverity::Error, AmatDiagnosticCode::UnknownProperty,
                AmatDiagnosticTarget::Property, overrideValue.propertyId,
                "Amat property '" + overrideValue.propertyId +
                    "' does not exist in .ashader shader type '" + shader.shaderTypeId + "'.");
        }

        return result;
    }

} // namespace asharia::material_instance
