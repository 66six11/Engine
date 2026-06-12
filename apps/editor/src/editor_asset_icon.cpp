#include "editor_asset_icon.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/core/error.hpp"
#include "asharia/core/log.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] EditorIconTint tint(float red, float green, float blue) noexcept {
            return editorIconTint(red, green, blue);
        }

        [[nodiscard]] EditorIconDescriptor icon(std::string_view iconId, EditorIconTint iconTint,
                                                std::string_view tooltipKey,
                                                std::string_view tooltipFallback) {
            return makeLucideEditorIconDescriptor(iconId, iconTint, tooltipKey, tooltipFallback);
        }

        [[nodiscard]] bool anyOf(std::string_view value,
                                 std::initializer_list<std::string_view> candidates) {
            return std::ranges::any_of(
                candidates, [value](std::string_view candidate) { return value == candidate; });
        }

        [[nodiscard]] EditorIconDescriptor builtInAssetIcon(const EditorAssetIconQuery& query) {
            if (query.folder) {
                return icon("lucide.folder", tint(0.88F, 0.68F, 0.34F), "icon.folder", "Folder");
            }

            const std::string assetType = normalizeEditorAssetIconToken(query.assetType);
            const std::string importerId = normalizeEditorAssetIconToken(query.importerId);
            const std::string extension = normalizeEditorAssetIconExtension(query.extension);
            const std::string importProfile = normalizeEditorAssetIconToken(query.importProfile);
            const std::string assetRole = normalizeEditorAssetIconToken(query.assetRole);

            if (anyOf(assetType, {"material", "asharia.material"}) || extension == ".amat" ||
                importerId.contains("material")) {
                return icon("lucide.palette", tint(0.78F, 0.58F, 0.92F), "icon.material",
                            "Material");
            }
            if (anyOf(assetType, {"mesh", "model", "geometry"}) || extension == ".mesh" ||
                extension == ".gltf" || extension == ".glb" || importerId.contains("mesh")) {
                return icon("lucide.box", tint(0.45F, 0.72F, 0.92F), "icon.mesh", "Mesh");
            }
            if (assetRole.contains("sprite") && !assetRole.contains("spritesheet")) {
                return icon("lucide.image", tint(0.56F, 0.82F, 0.96F), "icon.sprite", "Sprite");
            }
            if (importProfile == "sprite-sheet" || assetRole.contains("spritesheet") ||
                query.subAssetCount > 0U) {
                return icon("lucide.image", tint(0.50F, 0.86F, 0.76F), "icon.spriteSheet",
                            "Sprite sheet");
            }
            if (importProfile == "texture-cube" || assetRole.contains("texturecube")) {
                return icon("lucide.box", tint(0.50F, 0.78F, 0.92F), "icon.textureCube",
                            "Texture cube");
            }
            if (importProfile == "skybox" || assetRole.contains("skybox")) {
                return icon("lucide.image", tint(0.58F, 0.74F, 0.96F), "icon.skybox", "Skybox");
            }
            if (importProfile == "texture2d" || assetRole.contains("texture2d") ||
                anyOf(assetType, {"texture", "image"}) ||
                anyOf(extension, {".png", ".jpg", ".jpeg", ".dds", ".ktx", ".ktx2", ".tga"})) {
                return icon("lucide.image", tint(0.45F, 0.84F, 0.68F), "icon.image", "Image");
            }
            if (assetType == "shader" || anyOf(extension, {".slang", ".hlsl", ".glsl"})) {
                return icon("lucide.braces", tint(0.92F, 0.72F, 0.36F), "icon.shader", "Shader");
            }
            if (anyOf(extension, {".cpp", ".hpp", ".h", ".json", ".cmake", ".py", ".ps1"})) {
                return icon("lucide.file-code-2", tint(0.66F, 0.78F, 0.96F), "icon.code", "Code");
            }
            if (anyOf(extension, {".txt", ".md", ".toml", ".ini"})) {
                return icon("lucide.file-text", tint(0.72F, 0.76F, 0.82F), "icon.text", "Text");
            }

            if (query.diagnostic == EditorAssetIconDiagnosticState::Missing) {
                return icon("lucide.circle-help", tint(0.95F, 0.62F, 0.34F), "icon.missing",
                            "Missing");
            }
            if (query.diagnostic == EditorAssetIconDiagnosticState::Invalid) {
                return icon("lucide.triangle-alert", tint(0.96F, 0.42F, 0.36F), "icon.invalid",
                            "Invalid");
            }
            if (query.diagnostic == EditorAssetIconDiagnosticState::Warning) {
                return icon("lucide.circle-alert", tint(0.96F, 0.76F, 0.32F), "icon.warning",
                            "Warning");
            }

            return icon("lucide.file", tint(0.74F, 0.78F, 0.84F), "icon.file", "File");
        }

        [[nodiscard]] ImU32 tintColor(const EditorIconTint& iconTint) {
            return ImGui::GetColorU32(
                ImVec4{iconTint.red, iconTint.green, iconTint.blue, iconTint.alpha});
        }

        [[nodiscard]] bool iconIs(const EditorIconDescriptor& descriptor, std::string_view iconId) {
            return descriptor.id.value == iconId;
        }

        struct TextValue {
            std::string_view value;
        };

        struct TextNeedle {
            std::string_view value;
        };

        [[nodiscard]] bool normalizedContains(TextValue value, TextNeedle needle) {
            if (needle.value.empty()) {
                return true;
            }
            const std::string normalizedValue = normalizeEditorAssetIconToken(value.value);
            const std::string normalizedNeedle = normalizeEditorAssetIconToken(needle.value);
            return !normalizedNeedle.empty() && normalizedValue.contains(normalizedNeedle);
        }

        [[nodiscard]] bool normalizedEquals(TextValue value, TextNeedle expected) {
            if (expected.value.empty()) {
                return true;
            }
            return normalizeEditorAssetIconToken(value.value) ==
                   normalizeEditorAssetIconToken(expected.value);
        }

        [[nodiscard]] bool extensionEquals(TextValue value, TextNeedle expected) {
            if (expected.value.empty()) {
                return true;
            }
            return normalizeEditorAssetIconExtension(value.value) ==
                   normalizeEditorAssetIconExtension(expected.value);
        }

        [[nodiscard]] asharia::VoidResult validateIconRule(std::string_view resolverId,
                                                           const EditorAssetIconRule& rule) {
            if (rule.descriptor.id.value.empty()) {
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 0,
                                   "Editor asset icon rule descriptor id must not be empty: " +
                                       std::string{resolverId}}};
            }
            for (const char character : rule.descriptor.id.value) {
                const auto byte = static_cast<unsigned char>(character);
                if (std::iscntrl(byte) != 0 || std::isspace(byte) != 0 || character == '<' ||
                    character == '>') {
                    return std::unexpected{
                        asharia::Error{asharia::ErrorDomain::Core, 0,
                                       "Editor asset icon rule descriptor id is not stable: " +
                                           std::string{resolverId}}};
                }
            }
            return {};
        }

        [[nodiscard]] bool iconDescriptorIsStable(const EditorIconDescriptor& descriptor) {
            if (descriptor.id.value.empty()) {
                return false;
            }
            return std::ranges::none_of(descriptor.id.value, [](const char character) {
                const auto byte = static_cast<unsigned char>(character);
                return std::iscntrl(byte) != 0 || std::isspace(byte) != 0 || character == '<' ||
                       character == '>';
            });
        }

        [[nodiscard]] ImVec2 iconPoint(ImVec2 min, float size, float xScale, float yScale) {
            return ImVec2{min.x + (size * xScale), min.y + (size * yScale)};
        }

        struct IconDrawStyle {
            ImU32 color{};
            float stroke{};
        };

        void drawFileShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                           float stroke) {
            const ImVec2 max{min.x + (size * 0.78F), min.y + size};
            const ImVec2 fold = iconPoint(min, size, 0.56F, 0.22F);
            const ImVec2 topRight = iconPoint(min, size, 0.78F, 0.22F);
            drawList.AddRect(min, max, color, size * 0.08F, 0, stroke);
            drawList.AddLine(fold, topRight, color, stroke);
            drawList.AddLine(fold, ImVec2{fold.x, min.y}, color, stroke);
        }

        void drawFolderShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                             float stroke) {
            const ImVec2 tabMin = iconPoint(min, size, 0.0F, 0.18F);
            const ImVec2 tabMax = iconPoint(min, size, 0.42F, 0.38F);
            const ImVec2 bodyMin = iconPoint(min, size, 0.0F, 0.30F);
            const ImVec2 bodyMax = iconPoint(min, size, 1.0F, 0.86F);
            drawList.AddRect(tabMin, tabMax, color, size * 0.08F, 0, stroke);
            drawList.AddRect(bodyMin, bodyMax, color, size * 0.08F, 0, stroke);
        }

        void drawImageShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                            float stroke) {
            drawFileShape(drawList, min, size, color, stroke);
            const ImVec2 mountainLeft = iconPoint(min, size, 0.12F, 0.78F);
            const ImVec2 mountainPeak = iconPoint(min, size, 0.34F, 0.54F);
            const ImVec2 mountainRight = iconPoint(min, size, 0.54F, 0.78F);
            drawList.AddTriangle(mountainLeft, mountainPeak, mountainRight, color, stroke);
            drawList.AddCircleFilled(iconPoint(min, size, 0.52F, 0.44F), size * 0.045F, color);
        }

        void drawTextShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                           float stroke) {
            drawFileShape(drawList, min, size, color, stroke);
            for (float rowFraction : {0.46F, 0.62F, 0.78F}) {
                drawList.AddLine(iconPoint(min, size, 0.16F, rowFraction),
                                 iconPoint(min, size, 0.60F, rowFraction), color, stroke);
            }
        }

        void drawCodeShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                           float stroke) {
            drawList.AddLine(iconPoint(min, size, 0.38F, 0.28F), iconPoint(min, size, 0.18F, 0.50F),
                             color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.18F, 0.50F), iconPoint(min, size, 0.38F, 0.72F),
                             color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.62F, 0.28F), iconPoint(min, size, 0.82F, 0.50F),
                             color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.82F, 0.50F), iconPoint(min, size, 0.62F, 0.72F),
                             color, stroke);
        }

        void drawPaletteShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                              float stroke) {
            const ImVec2 center = iconPoint(min, size, 0.50F, 0.52F);
            drawList.AddCircle(center, size * 0.38F, color, 20, stroke);
            drawList.AddCircleFilled(iconPoint(min, size, 0.36F, 0.38F), size * 0.04F, color);
            drawList.AddCircleFilled(iconPoint(min, size, 0.52F, 0.34F), size * 0.04F, color);
            drawList.AddCircleFilled(iconPoint(min, size, 0.64F, 0.50F), size * 0.04F, color);
            drawList.AddCircle(iconPoint(min, size, 0.56F, 0.68F), size * 0.07F, color, 12, stroke);
        }

        void drawBoxShape(ImDrawList& drawList, ImVec2 min, float size, IconDrawStyle style) {
            const ImVec2 topLeft = iconPoint(min, size, 0.20F, 0.36F);
            const ImVec2 topCenter = iconPoint(min, size, 0.50F, 0.20F);
            const ImVec2 topRight = iconPoint(min, size, 0.80F, 0.36F);
            const ImVec2 bottomRight = iconPoint(min, size, 0.80F, 0.70F);
            const ImVec2 bottomCenter = iconPoint(min, size, 0.50F, 0.86F);
            const ImVec2 bottomLeft = iconPoint(min, size, 0.20F, 0.70F);
            const ImVec2 middle = iconPoint(min, size, 0.50F, 0.52F);
            std::array<ImVec2, 6> points{topLeft,     topCenter,    topRight,
                                         bottomRight, bottomCenter, bottomLeft};
            drawList.AddPolyline(points.data(), static_cast<int>(points.size()), style.color,
                                 ImDrawFlags_Closed, style.stroke);
            drawList.AddLine(topCenter, bottomCenter, style.color, style.stroke);
            drawList.AddLine(topLeft, middle, style.color, style.stroke);
            drawList.AddLine(topRight, middle, style.color, style.stroke);
        }

        enum class DiagnosticIconMarker : std::uint8_t {
            Exclamation,
            Question,
        };

        void drawDiagnosticShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                                 float stroke, DiagnosticIconMarker marker) {
            if (marker == DiagnosticIconMarker::Exclamation) {
                drawList.AddTriangle(iconPoint(min, size, 0.50F, 0.16F),
                                     iconPoint(min, size, 0.86F, 0.82F),
                                     iconPoint(min, size, 0.14F, 0.82F), color, stroke);
            } else {
                drawList.AddCircle(iconPoint(min, size, 0.50F, 0.50F), size * 0.36F, color, 24,
                                   stroke);
            }
            drawList.AddText(iconPoint(min, size, 0.43F, 0.24F), color,
                             marker == DiagnosticIconMarker::Question ? "?" : "!");
        }

        void drawXShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color, float stroke) {
            drawList.AddLine(iconPoint(min, size, 0.24F, 0.24F), iconPoint(min, size, 0.76F, 0.76F),
                             color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.76F, 0.24F), iconPoint(min, size, 0.24F, 0.76F),
                             color, stroke);
        }

        void drawCopyShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                           float stroke) {
            const ImVec2 backMin = iconPoint(min, size, 0.32F, 0.12F);
            const ImVec2 backMax = iconPoint(min, size, 0.82F, 0.62F);
            const ImVec2 frontMin = iconPoint(min, size, 0.18F, 0.30F);
            const ImVec2 frontMax = iconPoint(min, size, 0.68F, 0.88F);
            drawList.AddRect(backMin, backMax, color, size * 0.06F, 0, stroke);
            drawList.AddRect(frontMin, frontMax, color, size * 0.06F, 0, stroke);
        }

        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        void drawPlayShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                           float stroke) {
            std::array<ImVec2, 3> points{iconPoint(min, size, 0.26F, 0.18F),
                                         iconPoint(min, size, 0.26F, 0.82F),
                                         iconPoint(min, size, 0.82F, 0.50F)};
            drawList.AddTriangle(points[0], points[1], points[2], color, stroke);
        }

        void drawPauseShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                            float stroke) {
            drawList.AddLine(iconPoint(min, size, 0.34F, 0.20F), iconPoint(min, size, 0.34F, 0.80F),
                             color, stroke * 1.4F);
            drawList.AddLine(iconPoint(min, size, 0.66F, 0.20F), iconPoint(min, size, 0.66F, 0.80F),
                             color, stroke * 1.4F);
        }

        void drawStopShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                           float stroke) {
            drawList.AddRect(iconPoint(min, size, 0.24F, 0.24F), iconPoint(min, size, 0.76F, 0.76F),
                             color, size * 0.04F, 0, stroke);
        }

        void drawStepShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                           float stroke) {
            std::array<ImVec2, 3> points{iconPoint(min, size, 0.20F, 0.20F),
                                         iconPoint(min, size, 0.20F, 0.80F),
                                         iconPoint(min, size, 0.64F, 0.50F)};
            drawList.AddTriangle(points[0], points[1], points[2], color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.76F, 0.22F), iconPoint(min, size, 0.76F, 0.78F),
                             color, stroke);
        }

        void drawSearchShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                             float stroke) {
            drawList.AddCircle(iconPoint(min, size, 0.42F, 0.42F), size * 0.24F, color, 24, stroke);
            drawList.AddLine(iconPoint(min, size, 0.60F, 0.60F), iconPoint(min, size, 0.82F, 0.82F),
                             color, stroke);
        }

        void drawCameraShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                             float stroke) {
            drawList.AddRect(iconPoint(min, size, 0.14F, 0.30F), iconPoint(min, size, 0.86F, 0.78F),
                             color, size * 0.06F, 0, stroke);
            drawList.AddRect(iconPoint(min, size, 0.28F, 0.20F), iconPoint(min, size, 0.54F, 0.34F),
                             color, size * 0.04F, 0, stroke);
            drawList.AddCircle(iconPoint(min, size, 0.52F, 0.54F), size * 0.16F, color, 20, stroke);
        }

        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        void drawRefreshShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                              float stroke) {
            drawList.PathArcTo(iconPoint(min, size, 0.50F, 0.50F), size * 0.34F, 0.28F, 4.95F, 24);
            drawList.PathStroke(color, 0, stroke);
            std::array<ImVec2, 3> arrow{iconPoint(min, size, 0.72F, 0.16F),
                                        iconPoint(min, size, 0.84F, 0.16F),
                                        iconPoint(min, size, 0.82F, 0.30F)};
            drawList.AddPolyline(arrow.data(), static_cast<int>(arrow.size()), color, 0, stroke);
        }

        void drawSettingsShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                               float stroke) {
            const ImVec2 center = iconPoint(min, size, 0.50F, 0.50F);
            drawList.AddCircle(center, size * 0.18F, color, 20, stroke);
            constexpr float kPi = std::numbers::pi_v<float>;
            for (float angle : {0.0F, kPi / 3.0F, (kPi * 2.0F) / 3.0F, kPi, (kPi * 4.0F) / 3.0F,
                                (kPi * 5.0F) / 3.0F}) {
                const float inner = size * 0.30F;
                const float outer = size * 0.42F;
                drawList.AddLine(ImVec2{center.x + (std::cos(angle) * inner),
                                        center.y + (std::sin(angle) * inner)},
                                 ImVec2{center.x + (std::cos(angle) * outer),
                                        center.y + (std::sin(angle) * outer)},
                                 color, stroke);
            }
        }

        void drawLayoutShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                             float stroke) {
            const ImVec2 outerMin = iconPoint(min, size, 0.14F, 0.18F);
            const ImVec2 outerMax = iconPoint(min, size, 0.86F, 0.82F);
            drawList.AddRect(outerMin, outerMax, color, size * 0.05F, 0, stroke);
            drawList.AddLine(iconPoint(min, size, 0.38F, 0.18F), iconPoint(min, size, 0.38F, 0.82F),
                             color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.38F, 0.48F), iconPoint(min, size, 0.86F, 0.48F),
                             color, stroke);
        }

        void drawListTreeShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                               float stroke) {
            for (float yFraction : {0.24F, 0.50F, 0.76F}) {
                drawList.AddCircleFilled(iconPoint(min, size, 0.18F, yFraction), size * 0.035F,
                                         color);
                drawList.AddLine(iconPoint(min, size, 0.30F, yFraction),
                                 iconPoint(min, size, 0.84F, yFraction), color, stroke);
            }
            drawList.AddLine(iconPoint(min, size, 0.18F, 0.24F), iconPoint(min, size, 0.18F, 0.76F),
                             color, stroke);
        }

        void drawEyeShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color, float stroke) {
            const ImVec2 center = iconPoint(min, size, 0.50F, 0.50F);
            std::array<ImVec2, 4> outline{
                iconPoint(min, size, 0.12F, 0.50F), iconPoint(min, size, 0.34F, 0.28F),
                iconPoint(min, size, 0.66F, 0.28F), iconPoint(min, size, 0.88F, 0.50F)};
            drawList.AddPolyline(outline.data(), static_cast<int>(outline.size()), color, 0,
                                 stroke);
            std::array<ImVec2, 4> lower{
                iconPoint(min, size, 0.12F, 0.50F), iconPoint(min, size, 0.34F, 0.72F),
                iconPoint(min, size, 0.66F, 0.72F), iconPoint(min, size, 0.88F, 0.50F)};
            drawList.AddPolyline(lower.data(), static_cast<int>(lower.size()), color, 0, stroke);
            drawList.AddCircle(center, size * 0.10F, color, 16, stroke);
        }

        void drawLockShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                           float stroke) {
            drawList.AddRect(iconPoint(min, size, 0.22F, 0.44F), iconPoint(min, size, 0.78F, 0.84F),
                             color, size * 0.05F, 0, stroke);
            constexpr float kPi = std::numbers::pi_v<float>;
            drawList.PathArcTo(iconPoint(min, size, 0.50F, 0.46F), size * 0.22F, kPi, kPi * 2.0F,
                               16);
            drawList.PathStroke(color, 0, stroke);
        }

        void drawPinShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color, float stroke) {
            drawList.AddLine(iconPoint(min, size, 0.34F, 0.18F), iconPoint(min, size, 0.66F, 0.50F),
                             color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.66F, 0.18F), iconPoint(min, size, 0.34F, 0.50F),
                             color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.50F, 0.50F), iconPoint(min, size, 0.50F, 0.86F),
                             color, stroke);
        }

        void drawGridShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                           float stroke) {
            drawList.AddRect(iconPoint(min, size, 0.18F, 0.18F), iconPoint(min, size, 0.82F, 0.82F),
                             color, size * 0.04F, 0, stroke);
            for (float xFraction : {0.39F, 0.61F}) {
                drawList.AddLine(iconPoint(min, size, xFraction, 0.18F),
                                 iconPoint(min, size, xFraction, 0.82F), color, stroke);
            }
            for (float yFraction : {0.39F, 0.61F}) {
                drawList.AddLine(iconPoint(min, size, 0.18F, yFraction),
                                 iconPoint(min, size, 0.82F, yFraction), color, stroke);
            }
        }

        void drawMoveShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                           float stroke) {
            const ImVec2 center = iconPoint(min, size, 0.50F, 0.50F);
            drawList.AddLine(iconPoint(min, size, 0.16F, 0.50F), iconPoint(min, size, 0.84F, 0.50F),
                             color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.50F, 0.16F), iconPoint(min, size, 0.50F, 0.84F),
                             color, stroke);
            drawList.AddCircleFilled(center, size * 0.04F, color);
        }

        void drawFilterShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                             float stroke) {
            std::array<ImVec2, 4> points{
                iconPoint(min, size, 0.18F, 0.22F), iconPoint(min, size, 0.82F, 0.22F),
                iconPoint(min, size, 0.58F, 0.54F), iconPoint(min, size, 0.58F, 0.82F)};
            drawList.AddPolyline(points.data(), static_cast<int>(points.size()), color, 0, stroke);
            drawList.AddLine(iconPoint(min, size, 0.58F, 0.82F), iconPoint(min, size, 0.42F, 0.72F),
                             color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.42F, 0.72F), iconPoint(min, size, 0.42F, 0.54F),
                             color, stroke);
        }

        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        void drawGlyphById(ImDrawList& drawList, const EditorIconDescriptor& descriptor, ImVec2 min,
                           float size, ImU32 color, float stroke) {
            if (iconIs(descriptor, "lucide.folder") || iconIs(descriptor, "lucide.folder-open")) {
                drawFolderShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.image")) {
                drawImageShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.file-text")) {
                drawTextShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.file-code-2") ||
                       iconIs(descriptor, "lucide.braces") ||
                       iconIs(descriptor, "lucide.sparkles")) {
                drawCodeShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.palette")) {
                drawPaletteShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.box")) {
                drawBoxShape(drawList, min, size, IconDrawStyle{.color = color, .stroke = stroke});
            } else if (iconIs(descriptor, "lucide.triangle-alert") ||
                       iconIs(descriptor, "lucide.circle-alert")) {
                drawDiagnosticShape(drawList, min, size, color, stroke,
                                    DiagnosticIconMarker::Exclamation);
            } else if (iconIs(descriptor, "lucide.circle-help")) {
                drawDiagnosticShape(drawList, min, size, color, stroke,
                                    DiagnosticIconMarker::Question);
            } else if (iconIs(descriptor, "lucide.x")) {
                drawXShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.copy")) {
                drawCopyShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.play")) {
                drawPlayShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.pause")) {
                drawPauseShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.square")) {
                drawStopShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.step-forward")) {
                drawStepShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.search")) {
                drawSearchShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.camera")) {
                drawCameraShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.rotate-ccw") ||
                       iconIs(descriptor, "lucide.refresh-cw")) {
                drawRefreshShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.settings")) {
                drawSettingsShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.layout-dashboard")) {
                drawLayoutShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.list-tree")) {
                drawListTreeShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.eye")) {
                drawEyeShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.lock")) {
                drawLockShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.pin")) {
                drawPinShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.grid-3x3")) {
                drawGridShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.move-3d")) {
                drawMoveShape(drawList, min, size, color, stroke);
            } else if (iconIs(descriptor, "lucide.filter")) {
                drawFilterShape(drawList, min, size, color, stroke);
            } else {
                drawFileShape(drawList, min, size, color, stroke);
            }
        }

    } // namespace

    EditorIconTint editorIconTint(float red, float green, float blue, float alpha) noexcept {
        return EditorIconTint{.red = red, .green = green, .blue = blue, .alpha = alpha};
    }

    EditorIconDescriptor makeLucideEditorIconDescriptor(std::string_view lucideName,
                                                        EditorIconTint iconTint,
                                                        std::string_view tooltipKey,
                                                        std::string_view tooltipFallback) {
        std::string iconId{lucideName};
        if (!iconId.starts_with("lucide.")) {
            iconId = "lucide." + iconId;
        }
        return EditorIconDescriptor{
            .id = EditorIconId{.value = std::move(iconId)},
            .tint = iconTint,
            .tooltipKey = std::string{tooltipKey},
            .tooltipFallback = std::string{tooltipFallback},
        };
    }

    std::string normalizeEditorAssetIconExtension(std::string_view extension) {
        std::string normalized;
        normalized.reserve(extension.size() + 1U);
        if (!extension.empty() && extension.front() != '.') {
            normalized.push_back('.');
        }
        for (char character : extension) {
            normalized.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
        return normalized;
    }

    std::string normalizeEditorAssetIconToken(std::string_view value) {
        std::string normalized;
        normalized.reserve(value.size());
        for (char character : value) {
            normalized.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
        return normalized;
    }

    EditorAssetIconDiagnosticState editorAssetIconDiagnosticStateForProductState(
        asharia::asset::AssetCatalogProductState productState) noexcept {
        switch (productState) {
        case asharia::asset::AssetCatalogProductState::MissingProduct:
            return EditorAssetIconDiagnosticState::Missing;
        case asharia::asset::AssetCatalogProductState::StaleProduct:
            return EditorAssetIconDiagnosticState::Warning;
        case asharia::asset::AssetCatalogProductState::InvalidProduct:
            return EditorAssetIconDiagnosticState::Invalid;
        case asharia::asset::AssetCatalogProductState::NotTracked:
        case asharia::asset::AssetCatalogProductState::Ready:
            return EditorAssetIconDiagnosticState::None;
        }
        return EditorAssetIconDiagnosticState::None;
    }

    EditorAssetIconQuery
    makeEditorAssetIconQuery(const asharia::asset::AssetCatalogViewEntry& row) {
        return EditorAssetIconQuery{
            .folder = false,
            .assetType = row.assetTypeName,
            .importerId = row.importerName,
            .extension = row.extension,
            .diagnostic = editorAssetIconDiagnosticStateForProductState(row.productState),
            .sourcePath = row.sourcePath,
            .displayName = row.displayName,
            .guidText = row.guidText,
            .importProfile = row.importProfileName,
            .assetRole = row.assetRoleName,
            .subAssetCount = row.subAssets.size(),
        };
    }

    bool editorAssetIconRuleMatches(const EditorAssetIconRule& rule,
                                    const EditorAssetIconQuery& query) {
        if (rule.descriptor.id.value.empty()) {
            return false;
        }
        if (rule.folder && *rule.folder != query.folder) {
            return false;
        }
        if (rule.diagnostic && *rule.diagnostic != query.diagnostic) {
            return false;
        }
        if (rule.minimumSubAssetCount && query.subAssetCount < *rule.minimumSubAssetCount) {
            return false;
        }
        if (!normalizedContains(TextValue{query.assetType}, TextNeedle{rule.assetTypeContains})) {
            return false;
        }
        if (!normalizedContains(TextValue{query.importerId}, TextNeedle{rule.importerIdContains})) {
            return false;
        }
        if (!extensionEquals(TextValue{query.extension}, TextNeedle{rule.extension})) {
            return false;
        }
        if (!normalizedContains(TextValue{query.sourcePath}, TextNeedle{rule.sourcePathContains})) {
            return false;
        }
        if (!normalizedContains(TextValue{query.displayName},
                                TextNeedle{rule.displayNameContains})) {
            return false;
        }
        if (!normalizedEquals(TextValue{query.guidText}, TextNeedle{rule.guidText})) {
            return false;
        }
        if (!normalizedEquals(TextValue{query.importProfile}, TextNeedle{rule.importProfile})) {
            return false;
        }
        return normalizedContains(TextValue{query.assetRole}, TextNeedle{rule.assetRoleContains});
    }

    EditorAssetIconResolver makeEditorAssetIconRuleResolver(EditorAssetIconRule rule) {
        return [rule = std::move(rule)](
                   const EditorAssetIconQuery& query) -> std::optional<EditorIconDescriptor> {
            if (!editorAssetIconRuleMatches(rule, query)) {
                return std::nullopt;
            }
            return rule.descriptor;
        };
    }

    asharia::VoidResult
    EditorAssetIconRegistry::registerResolver(std::string resolverId,
                                              EditorAssetIconResolver resolver) {
        if (resolverId.empty()) {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::Core, 0, "Editor asset icon resolver id must not be empty"}};
        }
        if (!resolver) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Editor asset icon resolver must not be empty: " + resolverId}};
        }
        const auto found = std::ranges::find_if(
            resolvers_, [&](const ResolverEntry& entry) { return entry.id == resolverId; });
        if (found != resolvers_.end()) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Editor asset icon resolver is already registered: " + resolverId}};
        }

        resolvers_.push_back(
            ResolverEntry{.id = std::move(resolverId), .resolver = std::move(resolver)});
        return {};
    }

    asharia::VoidResult
    EditorAssetIconRegistry::registerOrReplaceResolver(std::string resolverId,
                                                       EditorAssetIconResolver resolver) {
        if (resolverId.empty()) {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::Core, 0, "Editor asset icon resolver id must not be empty"}};
        }
        if (!resolver) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Editor asset icon resolver must not be empty: " + resolverId}};
        }

        const auto found = std::ranges::find_if(
            resolvers_, [&](const ResolverEntry& entry) { return entry.id == resolverId; });
        if (found != resolvers_.end()) {
            found->resolver = std::move(resolver);
            return {};
        }

        resolvers_.push_back(
            ResolverEntry{.id = std::move(resolverId), .resolver = std::move(resolver)});
        return {};
    }

    asharia::VoidResult EditorAssetIconRegistry::registerRule(std::string resolverId,
                                                              EditorAssetIconRule rule) {
        if (const asharia::VoidResult valid = validateIconRule(resolverId, rule); !valid) {
            return std::unexpected{valid.error()};
        }
        return registerResolver(std::move(resolverId),
                                makeEditorAssetIconRuleResolver(std::move(rule)));
    }

    asharia::VoidResult EditorAssetIconRegistry::registerOrReplaceRule(std::string resolverId,
                                                                       EditorAssetIconRule rule) {
        if (const asharia::VoidResult valid = validateIconRule(resolverId, rule); !valid) {
            return std::unexpected{valid.error()};
        }
        return registerOrReplaceResolver(std::move(resolverId),
                                         makeEditorAssetIconRuleResolver(std::move(rule)));
    }

    bool EditorAssetIconRegistry::unregisterResolver(std::string_view resolverId) {
        const auto found =
            std::ranges::find_if(resolvers_, [resolverId](const ResolverEntry& entry) {
                return entry.id == resolverId;
            });
        if (found == resolvers_.end()) {
            return false;
        }
        resolvers_.erase(found);
        return true;
    }

    EditorIconDescriptor
    EditorAssetIconRegistry::resolveAssetIcon(const EditorAssetIconQuery& query) const {
        for (const ResolverEntry& entry : resolvers_) {
            if (std::optional<EditorIconDescriptor> resolved = entry.resolver(query)) {
                if (iconDescriptorIsStable(*resolved)) {
                    return *resolved;
                }
                asharia::logWarning("Editor asset icon resolver returned an invalid descriptor: " +
                                    entry.id);
            }
        }
        return builtInAssetIcon(query);
    }

    std::size_t EditorAssetIconRegistry::resolverCount() const noexcept {
        return resolvers_.size();
    }

    namespace {

        [[nodiscard]] bool
        smokeTextureProfileAssetIconFallbacks(EditorAssetIconRegistry& registry) {
            if (registry
                    .resolveAssetIcon(EditorAssetIconQuery{
                        .folder = false,
                        .assetType = {},
                        .importerId = {},
                        .extension = ".png",
                        .diagnostic = EditorAssetIconDiagnosticState::None,
                        .sourcePath = {},
                        .displayName = {},
                        .guidText = {},
                        .importProfile = "sprite-sheet",
                        .assetRole = {},
                        .subAssetCount = 2U,
                    })
                    .tooltipKey != "icon.spriteSheet") {
                asharia::logError("Editor asset icon smoke missed sprite-sheet profile fallback.");
                return false;
            }
            if (registry
                    .resolveAssetIcon(EditorAssetIconQuery{
                        .folder = false,
                        .assetType = {},
                        .importerId = {},
                        .extension = ".png",
                        .diagnostic = EditorAssetIconDiagnosticState::None,
                        .sourcePath = {},
                        .displayName = {},
                        .guidText = {},
                        .importProfile = "sprite-sheet",
                        .assetRole = "com.asharia.asset.Sprite",
                        .subAssetCount = 0U,
                    })
                    .tooltipKey != "icon.sprite") {
                asharia::logError("Editor asset icon smoke missed sprite role fallback.");
                return false;
            }
            if (registry
                    .resolveAssetIcon(EditorAssetIconQuery{
                        .folder = false,
                        .assetType = {},
                        .importerId = {},
                        .extension = ".ktx2",
                        .diagnostic = EditorAssetIconDiagnosticState::None,
                        .sourcePath = {},
                        .displayName = {},
                        .guidText = {},
                        .importProfile = "texture-cube",
                        .assetRole = {},
                        .subAssetCount = 0U,
                    })
                    .tooltipKey != "icon.textureCube") {
                asharia::logError("Editor asset icon smoke missed texture-cube profile fallback.");
                return false;
            }
            if (registry
                    .resolveAssetIcon(EditorAssetIconQuery{
                        .folder = false,
                        .assetType = {},
                        .importerId = {},
                        .extension = ".hdr",
                        .diagnostic = EditorAssetIconDiagnosticState::None,
                        .sourcePath = {},
                        .displayName = {},
                        .guidText = {},
                        .importProfile = "skybox",
                        .assetRole = {},
                        .subAssetCount = 0U,
                    })
                    .tooltipKey != "icon.skybox") {
                asharia::logError("Editor asset icon smoke missed skybox profile fallback.");
                return false;
            }

            auto spriteProfileCustom = registry.registerResolver(
                "smoke.sprite-profile-icon",
                [](const EditorAssetIconQuery& query) -> std::optional<EditorIconDescriptor> {
                    if (normalizeEditorAssetIconToken(query.importProfile) != "sprite-sheet" ||
                        query.subAssetCount == 0U) {
                        return std::nullopt;
                    }
                    return makeLucideEditorIconDescriptor(
                        "sparkles", editorIconTint(0.50F, 0.86F, 0.76F), "icon.spriteSheet.custom",
                        "Custom sprite sheet");
                });
            if (!spriteProfileCustom) {
                asharia::logError(spriteProfileCustom.error().message);
                return false;
            }
            if (registry
                    .resolveAssetIcon(EditorAssetIconQuery{
                        .folder = false,
                        .assetType = {},
                        .importerId = {},
                        .extension = ".png",
                        .diagnostic = EditorAssetIconDiagnosticState::None,
                        .sourcePath = {},
                        .displayName = {},
                        .guidText = {},
                        .importProfile = "sprite-sheet",
                        .assetRole = {},
                        .subAssetCount = 3U,
                    })
                    .id.value != "lucide.sparkles") {
                asharia::logError("Editor asset icon smoke missed profile-based custom override.");
                return false;
            }
            if (!registry.unregisterResolver("smoke.sprite-profile-icon")) {
                asharia::logError("Editor asset icon smoke missed profile resolver unregister.");
                return false;
            }
            if (registry
                    .resolveAssetIcon(EditorAssetIconQuery{
                        .folder = false,
                        .assetType = {},
                        .importerId = {},
                        .extension = ".png",
                        .diagnostic = EditorAssetIconDiagnosticState::None,
                        .sourcePath = {},
                        .displayName = {},
                        .guidText = {},
                        .importProfile = "sprite-sheet",
                        .assetRole = {},
                        .subAssetCount = 3U,
                    })
                    .tooltipKey != "icon.spriteSheet") {
                asharia::logError(
                    "Editor asset icon smoke lost profile fallback after unregister.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool smokeRuleBasedAssetIconOverrides(EditorAssetIconRegistry& registry) {
            auto pngSlicesRule = registry.registerRule(
                "smoke.png-slices-icon", EditorAssetIconRule{
                                             .folder = false,
                                             .diagnostic = EditorAssetIconDiagnosticState::None,
                                             .minimumSubAssetCount = 2U,
                                             .assetTypeContains = {},
                                             .importerIdContains = {},
                                             .extension = "PNG",
                                             .sourcePathContains = {},
                                             .displayNameContains = {},
                                             .guidText = {},
                                             .importProfile = {},
                                             .assetRoleContains = {},
                                             .descriptor = makeLucideEditorIconDescriptor(
                                                 "sparkles", editorIconTint(0.50F, 0.86F, 0.76F),
                                                 "icon.pngSlices.custom", "Custom PNG slices"),
                                         });
            if (!pngSlicesRule) {
                asharia::logError(pngSlicesRule.error().message);
                return false;
            }

            if (registry
                    .resolveAssetIcon(EditorAssetIconQuery{
                        .folder = false,
                        .assetType = {},
                        .importerId = {},
                        .extension = ".png",
                        .diagnostic = EditorAssetIconDiagnosticState::None,
                        .sourcePath = "Assets/Textures/Hero.png",
                        .displayName = "Hero.png",
                        .guidText = {},
                        .importProfile = {},
                        .assetRole = {},
                        .subAssetCount = 3U,
                    })
                    .id.value != "lucide.sparkles") {
                asharia::logError("Editor asset icon smoke missed rule-based PNG override.");
                return false;
            }
            if (registry
                    .resolveAssetIcon(EditorAssetIconQuery{
                        .folder = false,
                        .assetType = {},
                        .importerId = {},
                        .extension = ".png",
                        .diagnostic = EditorAssetIconDiagnosticState::None,
                        .sourcePath = "Assets/Textures/Hero.png",
                        .displayName = "Hero.png",
                        .guidText = {},
                        .importProfile = {},
                        .assetRole = {},
                        .subAssetCount = 1U,
                    })
                    .id.value == "lucide.sparkles") {
                asharia::logError(
                    "Editor asset icon smoke applied a minimum-sub-asset rule too broadly.");
                return false;
            }

            auto generatedShaderRule = registry.registerOrReplaceRule(
                "smoke.png-slices-icon", EditorAssetIconRule{
                                             .folder = false,
                                             .diagnostic = EditorAssetIconDiagnosticState::None,
                                             .minimumSubAssetCount = {},
                                             .assetTypeContains = {},
                                             .importerIdContains = {},
                                             .extension = ".slang",
                                             .sourcePathContains = "Generated/Shaders",
                                             .displayNameContains = {},
                                             .guidText = {},
                                             .importProfile = {},
                                             .assetRoleContains = {},
                                             .descriptor = makeLucideEditorIconDescriptor(
                                                 "file-code-2", editorIconTint(0.66F, 0.78F, 0.96F),
                                                 "icon.shader.generated.rule", "Generated shader"),
                                         });
            if (!generatedShaderRule) {
                asharia::logError(generatedShaderRule.error().message);
                return false;
            }
            if (registry
                    .resolveAssetIcon(EditorAssetIconQuery{
                        .folder = false,
                        .assetType = {},
                        .importerId = {},
                        .extension = ".png",
                        .diagnostic = EditorAssetIconDiagnosticState::None,
                        .sourcePath = "Assets/Textures/Hero.png",
                        .displayName = "Hero.png",
                        .guidText = {},
                        .importProfile = {},
                        .assetRole = {},
                        .subAssetCount = 3U,
                    })
                    .tooltipKey != "icon.spriteSheet") {
                asharia::logError("Editor asset icon smoke lost fallback after rule replacement.");
                return false;
            }
            if (registry
                    .resolveAssetIcon(EditorAssetIconQuery{
                        .folder = false,
                        .assetType = {},
                        .importerId = {},
                        .extension = ".slang",
                        .diagnostic = EditorAssetIconDiagnosticState::None,
                        .sourcePath = "Assets/Generated/Shaders/grid.slang",
                        .displayName = "grid.slang",
                        .guidText = {},
                        .importProfile = {},
                        .assetRole = {},
                        .subAssetCount = 0U,
                    })
                    .tooltipKey != "icon.shader.generated.rule") {
                asharia::logError("Editor asset icon smoke missed replaced rule override.");
                return false;
            }
            if (registry.registerRule("smoke.invalid-empty-rule", EditorAssetIconRule{})) {
                asharia::logError("Editor asset icon smoke accepted an empty rule descriptor.");
                return false;
            }
            if (!registry.unregisterResolver("smoke.png-slices-icon")) {
                asharia::logError("Editor asset icon smoke missed rule unregister behavior.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool smokeInvalidAssetIconDescriptors(EditorAssetIconRegistry& registry) {
            auto invalidResolver = registry.registerResolver(
                "smoke.invalid-icon",
                [](const EditorAssetIconQuery&) -> std::optional<EditorIconDescriptor> {
                    return EditorIconDescriptor{};
                });
            if (!invalidResolver ||
                registry.resolveAssetIcon(EditorAssetIconQuery{
                                              .folder = false,
                                              .assetType = {},
                                              .importerId = {},
                                              .extension = ".png",
                                              .diagnostic = EditorAssetIconDiagnosticState::None,
                                              .sourcePath = {},
                                              .displayName = {},
                                              .guidText = {},
                                              .importProfile = {},
                                              .assetRole = {},
                                              .subAssetCount = 0U,
                                          })
                        .id.value != "lucide.image") {
                asharia::logError(
                    "Editor asset icon smoke accepted an invalid resolver descriptor.");
                return false;
            }

            auto svgLikeResolver = registry.registerOrReplaceResolver(
                "smoke.invalid-icon",
                [](const EditorAssetIconQuery&) -> std::optional<EditorIconDescriptor> {
                    return EditorIconDescriptor{
                        .id = EditorIconId{.value = "<svg>"},
                        .tint = editorIconTint(1.0F, 1.0F, 1.0F),
                        .tooltipKey = "icon.invalid.svg",
                        .tooltipFallback = "Invalid SVG",
                    };
                });
            if (!svgLikeResolver ||
                registry.resolveAssetIcon(EditorAssetIconQuery{
                                              .folder = false,
                                              .assetType = {},
                                              .importerId = {},
                                              .extension = ".slang",
                                              .diagnostic = EditorAssetIconDiagnosticState::None,
                                              .sourcePath = {},
                                              .displayName = {},
                                              .guidText = {},
                                              .importProfile = {},
                                              .assetRole = {},
                                              .subAssetCount = 0U,
                                          })
                        .id.value != "lucide.braces" ||
                !registry.unregisterResolver("smoke.invalid-icon")) {
                asharia::logError("Editor asset icon smoke accepted an SVG-like icon descriptor.");
                return false;
            }

            if (registry.registerRule("smoke.invalid-svg-rule",
                                      EditorAssetIconRule{
                                          .folder = {},
                                          .diagnostic = {},
                                          .minimumSubAssetCount = {},
                                          .assetTypeContains = {},
                                          .importerIdContains = {},
                                          .extension = {},
                                          .sourcePathContains = {},
                                          .displayNameContains = {},
                                          .guidText = {},
                                          .importProfile = {},
                                          .assetRoleContains = {},
                                          .descriptor =
                                              EditorIconDescriptor{
                                                  .id = EditorIconId{.value = "<svg>"},
                                                  .tint = editorIconTint(1.0F, 1.0F, 1.0F),
                                                  .tooltipKey = "icon.invalid.svg.rule",
                                                  .tooltipFallback = "Invalid SVG rule",
                                              },
                                      })) {
                asharia::logError(
                    "Editor asset icon smoke accepted an invalid rule descriptor id.");
                return false;
            }

            return true;
        }

    } // namespace

    void drawEditorIconGlyph(const EditorIconDescriptor& descriptor, float size) {
        size = std::max(size, 8.0F);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float stroke = std::max(1.0F, std::round(size / 12.0F));
        const ImU32 color = tintColor(descriptor.tint);
        drawGlyphById(*drawList, descriptor, cursor, size, color, stroke);
        ImGui::Dummy(ImVec2{size, size});
        if (!descriptor.tooltipFallback.empty() && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(descriptor.tooltipFallback.c_str());
            ImGui::EndTooltip();
        }
    }

    bool drawEditorIconButton(const EditorIconDescriptor& descriptor, std::string_view stableId,
                              float size) {
        size = std::max(size, 8.0F);
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const ImVec2 buttonSize{size + (style.FramePadding.x * 2.0F),
                                size + (style.FramePadding.y * 2.0F)};
        std::string buttonId{"##"};
        buttonId.append(stableId.data(), stableId.size());
        const bool pressed = ImGui::InvisibleButton(buttonId.c_str(), buttonSize);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImGuiCol backgroundColor = ImGuiCol_Button;
        if (active) {
            backgroundColor = ImGuiCol_ButtonActive;
        } else if (hovered) {
            backgroundColor = ImGuiCol_ButtonHovered;
        }
        const ImU32 background = ImGui::GetColorU32(backgroundColor);
        drawList->AddRectFilled(cursor, ImVec2{cursor.x + buttonSize.x, cursor.y + buttonSize.y},
                                background, style.FrameRounding);
        drawList->AddRect(cursor, ImVec2{cursor.x + buttonSize.x, cursor.y + buttonSize.y},
                          ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding);

        const float stroke = std::max(1.0F, std::round(size / 12.0F));
        const ImU32 color = tintColor(descriptor.tint);
        const ImVec2 iconMin{cursor.x + ((buttonSize.x - size) * 0.5F),
                             cursor.y + ((buttonSize.y - size) * 0.5F)};
        drawGlyphById(*drawList, descriptor, iconMin, size, color, stroke);

        if (!descriptor.tooltipFallback.empty() && hovered) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(descriptor.tooltipFallback.c_str());
            ImGui::EndTooltip();
        }
        return pressed;
    }

    bool validateEditorAssetIconSmoke() {
        EditorAssetIconRegistry registry;
        if (registry
                .resolveAssetIcon(EditorAssetIconQuery{
                    .folder = true,
                    .assetType = {},
                    .importerId = {},
                    .extension = {},
                    .diagnostic = EditorAssetIconDiagnosticState::None,
                    .sourcePath = {},
                    .displayName = {},
                    .guidText = {},
                    .importProfile = {},
                    .assetRole = {},
                    .subAssetCount = 0U,
                })
                .id.value != "lucide.folder") {
            asharia::logError("Editor asset icon smoke missed folder Lucide fallback.");
            return false;
        }
        if (registry
                .resolveAssetIcon(EditorAssetIconQuery{
                    .folder = false,
                    .assetType = {},
                    .importerId = {},
                    .extension = ".png",
                    .diagnostic = EditorAssetIconDiagnosticState::None,
                    .sourcePath = {},
                    .displayName = {},
                    .guidText = {},
                    .importProfile = {},
                    .assetRole = {},
                    .subAssetCount = 0U,
                })
                .id.value != "lucide.image") {
            asharia::logError("Editor asset icon smoke missed image extension fallback.");
            return false;
        }
        if (!smokeTextureProfileAssetIconFallbacks(registry)) {
            return false;
        }
        if (registry
                .resolveAssetIcon(EditorAssetIconQuery{
                    .folder = false,
                    .assetType = "material",
                    .importerId = {},
                    .extension = {},
                    .diagnostic = EditorAssetIconDiagnosticState::None,
                    .sourcePath = {},
                    .displayName = {},
                    .guidText = {},
                    .importProfile = {},
                    .assetRole = {},
                    .subAssetCount = 0U,
                })
                .id.value != "lucide.palette") {
            asharia::logError("Editor asset icon smoke missed material type fallback.");
            return false;
        }
        if (registry
                .resolveAssetIcon(EditorAssetIconQuery{
                    .folder = false,
                    .assetType = {},
                    .importerId = {},
                    .extension = ".unknown",
                    .diagnostic = EditorAssetIconDiagnosticState::None,
                    .sourcePath = {},
                    .displayName = {},
                    .guidText = {},
                    .importProfile = {},
                    .assetRole = {},
                    .subAssetCount = 0U,
                })
                .id.value != "lucide.file") {
            asharia::logError("Editor asset icon smoke missed generic file fallback.");
            return false;
        }
        if (registry
                .resolveAssetIcon(EditorAssetIconQuery{
                    .folder = false,
                    .assetType = {},
                    .importerId = {},
                    .extension = {},
                    .diagnostic = EditorAssetIconDiagnosticState::Missing,
                    .sourcePath = {},
                    .displayName = {},
                    .guidText = {},
                    .importProfile = {},
                    .assetRole = {},
                    .subAssetCount = 0U,
                })
                .id.value != "lucide.circle-help") {
            asharia::logError("Editor asset icon smoke missed missing diagnostic fallback.");
            return false;
        }
        if (makeLucideEditorIconDescriptor("x", editorIconTint(0.78F, 0.80F, 0.84F), "icon.clear",
                                           "Clear")
                .id.value != "lucide.x") {
            asharia::logError("Editor asset icon smoke missed Lucide x id normalization.");
            return false;
        }
        if (makeLucideEditorIconDescriptor("copy", editorIconTint(0.72F, 0.76F, 0.82F), "icon.copy",
                                           "Copy")
                .id.value != "lucide.copy") {
            asharia::logError("Editor asset icon smoke missed Lucide copy id normalization.");
            return false;
        }

        if (!smokeInvalidAssetIconDescriptors(registry)) {
            return false;
        }

        auto custom = registry.registerResolver(
            "smoke.shader-icon",
            [](const EditorAssetIconQuery& query) -> std::optional<EditorIconDescriptor> {
                if (normalizeEditorAssetIconExtension(query.extension) != ".slang") {
                    return std::nullopt;
                }
                return makeLucideEditorIconDescriptor("sparkles",
                                                      editorIconTint(0.94F, 0.82F, 0.42F),
                                                      "icon.shader.custom", "Custom shader");
            });
        if (!custom) {
            asharia::logError(custom.error().message);
            return false;
        }
        if (registry.resolverCount() != 1U ||
            registry.resolveAssetIcon(EditorAssetIconQuery{
                                          .folder = false,
                                          .assetType = {},
                                          .importerId = {},
                                          .extension = ".slang",
                                          .diagnostic = EditorAssetIconDiagnosticState::None,
                                          .sourcePath = {},
                                          .displayName = {},
                                          .guidText = {},
                                          .importProfile = {},
                                          .assetRole = {},
                                          .subAssetCount = 0U,
                                      })
                    .id.value != "lucide.sparkles") {
            asharia::logError("Editor asset icon smoke missed custom resolver override.");
            return false;
        }
        if (registry.registerResolver(
                "smoke.shader-icon",
                [](const EditorAssetIconQuery&) -> std::optional<EditorIconDescriptor> {
                    return std::nullopt;
                })) {
            asharia::logError("Editor asset icon smoke accepted a duplicate resolver id.");
            return false;
        }

        auto replaced = registry.registerOrReplaceResolver(
            "smoke.shader-icon",
            [](const EditorAssetIconQuery& query) -> std::optional<EditorIconDescriptor> {
                if (!query.sourcePath.contains("Generated/Shaders") ||
                    normalizeEditorAssetIconExtension(query.extension) != ".slang") {
                    return std::nullopt;
                }
                return makeLucideEditorIconDescriptor("file-code-2",
                                                      editorIconTint(0.66F, 0.78F, 0.96F),
                                                      "icon.shader.generated", "Generated shader");
            });
        if (!replaced) {
            asharia::logError(replaced.error().message);
            return false;
        }
        if (registry.resolverCount() != 1U ||
            registry.resolveAssetIcon(EditorAssetIconQuery{
                                          .folder = false,
                                          .assetType = {},
                                          .importerId = {},
                                          .extension = ".slang",
                                          .diagnostic = EditorAssetIconDiagnosticState::None,
                                          .sourcePath = "Assets/Generated/Shaders/grid.slang",
                                          .displayName = "grid.slang",
                                          .guidText = "5d3cdcbf-7396-40d0-b497-4fa2fe54f92a",
                                          .importProfile = {},
                                          .assetRole = {},
                                          .subAssetCount = 0U,
                                      })
                    .id.value != "lucide.file-code-2") {
            asharia::logError("Editor asset icon smoke missed custom resolver replacement.");
            return false;
        }
        if (registry
                .resolveAssetIcon(EditorAssetIconQuery{
                    .folder = false,
                    .assetType = {},
                    .importerId = {},
                    .extension = ".slang",
                    .diagnostic = EditorAssetIconDiagnosticState::None,
                    .sourcePath = "Assets/Shaders/grid.slang",
                    .displayName = "grid.slang",
                    .guidText = "5d3cdcbf-7396-40d0-b497-4fa2fe54f92a",
                    .importProfile = {},
                    .assetRole = {},
                    .subAssetCount = 0U,
                })
                .id.value != "lucide.braces") {
            asharia::logError("Editor asset icon smoke lost fallback after resolver replacement.");
            return false;
        }
        if (!registry.unregisterResolver("smoke.shader-icon") ||
            registry.unregisterResolver("smoke.shader-icon") || registry.resolverCount() != 0U) {
            asharia::logError("Editor asset icon smoke missed resolver unregister behavior.");
            return false;
        }
        if (!smokeRuleBasedAssetIconOverrides(registry)) {
            return false;
        }

        return true;
    }

} // namespace asharia::editor
