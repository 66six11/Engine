#include "asharia/material_instance/mat_resolver.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "asharia/material_instance/mat_io.hpp"

namespace asharia::material_instance {
    namespace {

        using shader_authoring::ShaderDocument;
        using shader_authoring::ShaderPropertyDecl;
        using shader_authoring::ShaderPropertyType;

        void addDiagnostic(MatResolveResult& result, MatDiagnosticSeverity severity,
                           MatDiagnosticCode code, MatDiagnosticTarget target,
                           std::string propertyId, std::string message) {
            result.diagnostics.push_back(MatDiagnostic{
                .severity = severity,
                .code = code,
                .target = target,
                .propertyId = std::move(propertyId),
                .message = std::move(message),
            });
        }

        [[nodiscard]] const MatPropertyOverride* findOverride(const MatDocument& document,
                                                              std::string_view propertyId) {
            const auto found = std::ranges::find(document.properties, propertyId,
                                                 &MatPropertyOverride::propertyId);
            if (found == document.properties.end()) {
                return nullptr;
            }
            return &*found;
        }

        [[nodiscard]] const ShaderPropertyDecl* findProperty(const ShaderDocument& shader,
                                                              std::string_view propertyId) {
            const auto found =
                std::ranges::find(shader.properties, propertyId, &ShaderPropertyDecl::name);
            if (found == shader.properties.end()) {
                return nullptr;
            }
            return &*found;
        }

    } // namespace

    std::string_view toString(MatDiagnosticSeverity severity) noexcept {
        switch (severity) {
        case MatDiagnosticSeverity::Warning:
            return "warning";
        case MatDiagnosticSeverity::Error:
            return "error";
        }
        return "unknown";
    }

    std::string_view toString(MatDiagnosticCode code) noexcept {
        switch (code) {
        case MatDiagnosticCode::MaterialTypeMismatch:
            return "material_type_mismatch";
        case MatDiagnosticCode::UnknownProperty:
            return "unknown_property";
        case MatDiagnosticCode::PropertyTypeMismatch:
            return "property_type_mismatch";
        case MatDiagnosticCode::StaleMaterialTypeHash:
            return "stale_material_type_hash";
        case MatDiagnosticCode::StaleSignatureHash:
            return "stale_signature_hash";
        case MatDiagnosticCode::InvalidOverride:
            return "invalid_override";
        }
        return "unknown";
    }

    std::string_view toString(MatDiagnosticTarget target) noexcept {
        switch (target) {
        case MatDiagnosticTarget::Document:
            return "document";
        case MatDiagnosticTarget::MaterialType:
            return "material_type";
        case MatDiagnosticTarget::Property:
            return "property";
        case MatDiagnosticTarget::Import:
            return "import";
        }
        return "unknown";
    }

    std::string_view toString(MatOverrideDiffKind kind) noexcept {
        switch (kind) {
        case MatOverrideDiffKind::Defaulted:
            return "defaulted";
        case MatOverrideDiffKind::Overridden:
            return "overridden";
        case MatOverrideDiffKind::Invalid:
            return "invalid";
        }
        return "unknown";
    }

    bool hasErrors(const std::vector<MatDiagnostic>& diagnostics) {
        return std::ranges::any_of(diagnostics, [](const MatDiagnostic& diagnostic) {
            return diagnostic.severity == MatDiagnosticSeverity::Error;
        });
    }

    MatResolveResult resolveMatOverrides(const MatDocument& document, const ShaderDocument& shader,
                                         const MatResolveOptions& options) {
        MatResolveResult result;
        if (auto valid = validateMatDocument(document); !valid) {
            addDiagnostic(result, MatDiagnosticSeverity::Error, MatDiagnosticCode::InvalidOverride,
                          MatDiagnosticTarget::Document, {}, std::move(valid.error().message));
            return result;
        }
        result.overrides.reserve(shader.properties.size());

        if (document.materialType.stableTypeId != shader.shaderTypeId) {
            addDiagnostic(
                result, MatDiagnosticSeverity::Error, MatDiagnosticCode::MaterialTypeMismatch,
                MatDiagnosticTarget::MaterialType, {},
                "Mat stableTypeId '" + document.materialType.stableTypeId +
                    "' does not match .shader shader type '" + shader.shaderTypeId + "'.");
        }

        if (options.currentMaterialTypeHash &&
            document.materialType.expectedTypeHash != *options.currentMaterialTypeHash) {
            addDiagnostic(result, MatDiagnosticSeverity::Warning,
                          MatDiagnosticCode::StaleMaterialTypeHash,
                          MatDiagnosticTarget::MaterialType, {},
                          "Mat expectedTypeHash is stale for shader type '" +
                              document.materialType.stableTypeId + "'.");
        }

        if (options.currentSignatureHash &&
            document.import.lastCookedSignatureHash != *options.currentSignatureHash) {
            addDiagnostic(result, MatDiagnosticSeverity::Warning,
                          MatDiagnosticCode::StaleSignatureHash, MatDiagnosticTarget::Import, {},
                          "Mat lastCookedSignatureHash is stale for shader type '" +
                              document.materialType.stableTypeId + "'.");
        }

        for (const ShaderPropertyDecl& property : shader.properties) {
            const MatPropertyOverride* overrideValue = findOverride(document, property.name);
            if (overrideValue == nullptr) {
                result.overrides.push_back(MatOverrideDiff{
                    .kind = MatOverrideDiffKind::Defaulted,
                    .propertyId = property.name,
                    .declaredType = property.type,
                    .overrideType = std::nullopt,
                });
                continue;
            }

            if (overrideValue->type != property.type) {
                addDiagnostic(result, MatDiagnosticSeverity::Error,
                              MatDiagnosticCode::PropertyTypeMismatch,
                              MatDiagnosticTarget::Property, overrideValue->propertyId,
                              "Mat property '" + overrideValue->propertyId + "' has type '" +
                                  std::string{shader_authoring::toString(overrideValue->type)} +
                                  "' but .shader declares '" +
                                  std::string{shader_authoring::toString(property.type)} + "'.");
                result.overrides.push_back(MatOverrideDiff{
                    .kind = MatOverrideDiffKind::Invalid,
                    .propertyId = property.name,
                    .declaredType = property.type,
                    .overrideType = overrideValue->type,
                });
                continue;
            }

            result.overrides.push_back(MatOverrideDiff{
                .kind = MatOverrideDiffKind::Overridden,
                .propertyId = property.name,
                .declaredType = property.type,
                .overrideType = overrideValue->type,
            });
        }

        for (const MatPropertyOverride& overrideValue : document.properties) {
            if (findProperty(shader, overrideValue.propertyId) != nullptr) {
                continue;
            }
            addDiagnostic(result, MatDiagnosticSeverity::Error, MatDiagnosticCode::UnknownProperty,
                          MatDiagnosticTarget::Property, overrideValue.propertyId,
                          "Mat property '" + overrideValue.propertyId +
                              "' does not exist in .shader shader type '" + shader.shaderTypeId +
                              "'.");
        }

        return result;
    }

} // namespace asharia::material_instance
