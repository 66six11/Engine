#include "editor_asset_icon.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <imgui.h>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/core/error.hpp"
#include "asharia/core/log.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] EditorIconTint tint(float red, float green, float blue) {
            return EditorIconTint{.red = red, .green = green, .blue = blue, .alpha = 1.0F};
        }

        [[nodiscard]] EditorIconDescriptor icon(std::string_view iconId, EditorIconTint iconTint,
                                                std::string_view tooltipKey,
                                                std::string_view tooltipFallback) {
            return EditorIconDescriptor{
                .id = EditorIconId{.value = std::string{iconId}},
                .tint = iconTint,
                .tooltipKey = std::string{tooltipKey},
                .tooltipFallback = std::string{tooltipFallback},
            };
        }

        [[nodiscard]] std::string normalizedExtension(std::string_view extension) {
            std::string normalized;
            normalized.reserve(extension.size() + 1U);
            if (!extension.empty() && extension.front() != '.') {
                normalized.push_back('.');
            }
            for (char character : extension) {
                normalized.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character))));
            }
            return normalized;
        }

        [[nodiscard]] std::string normalizedToken(std::string_view value) {
            std::string normalized;
            normalized.reserve(value.size());
            for (char character : value) {
                normalized.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character))));
            }
            return normalized;
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

            const std::string assetType = normalizedToken(query.assetType);
            const std::string importerId = normalizedToken(query.importerId);
            const std::string extension = normalizedExtension(query.extension);

            if (anyOf(assetType, {"material", "asharia.material"}) ||
                extension == ".amat" || importerId.contains("material")) {
                return icon("lucide.palette", tint(0.78F, 0.58F, 0.92F), "icon.material",
                            "Material");
            }
            if (anyOf(assetType, {"mesh", "model", "geometry"}) || extension == ".mesh" ||
                extension == ".gltf" || extension == ".glb" || importerId.contains("mesh")) {
                return icon("lucide.box", tint(0.45F, 0.72F, 0.92F), "icon.mesh", "Mesh");
            }
            if (anyOf(assetType, {"texture", "image"}) ||
                anyOf(extension, {".png", ".jpg", ".jpeg", ".dds", ".ktx", ".ktx2", ".tga"})) {
                return icon("lucide.image", tint(0.45F, 0.84F, 0.68F), "icon.image", "Image");
            }
            if (assetType == "shader" || anyOf(extension, {".slang", ".hlsl", ".glsl"})) {
                return icon("lucide.braces", tint(0.92F, 0.72F, 0.36F), "icon.shader", "Shader");
            }
            if (anyOf(extension, {".cpp", ".hpp", ".h", ".json", ".cmake", ".py", ".ps1"})) {
                return icon("lucide.file-code-2", tint(0.66F, 0.78F, 0.96F), "icon.code",
                            "Code");
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
            return ImGui::GetColorU32(ImVec4{iconTint.red, iconTint.green, iconTint.blue,
                                             iconTint.alpha});
        }

        [[nodiscard]] bool iconIs(const EditorIconDescriptor& descriptor,
                                  std::string_view iconId) {
            return descriptor.id.value == iconId;
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
            drawList.AddLine(iconPoint(min, size, 0.38F, 0.28F),
                             iconPoint(min, size, 0.18F, 0.50F), color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.18F, 0.50F),
                             iconPoint(min, size, 0.38F, 0.72F), color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.62F, 0.28F),
                             iconPoint(min, size, 0.82F, 0.50F), color, stroke);
            drawList.AddLine(iconPoint(min, size, 0.82F, 0.50F),
                             iconPoint(min, size, 0.62F, 0.72F), color, stroke);
        }

        void drawPaletteShape(ImDrawList& drawList, ImVec2 min, float size, ImU32 color,
                              float stroke) {
            const ImVec2 center = iconPoint(min, size, 0.50F, 0.52F);
            drawList.AddCircle(center, size * 0.38F, color, 20, stroke);
            drawList.AddCircleFilled(iconPoint(min, size, 0.36F, 0.38F), size * 0.04F, color);
            drawList.AddCircleFilled(iconPoint(min, size, 0.52F, 0.34F), size * 0.04F, color);
            drawList.AddCircleFilled(iconPoint(min, size, 0.64F, 0.50F), size * 0.04F, color);
            drawList.AddCircle(iconPoint(min, size, 0.56F, 0.68F), size * 0.07F, color, 12,
                               stroke);
        }

        void drawBoxShape(ImDrawList& drawList, ImVec2 min, float size, IconDrawStyle style) {
            const ImVec2 topLeft = iconPoint(min, size, 0.20F, 0.36F);
            const ImVec2 topCenter = iconPoint(min, size, 0.50F, 0.20F);
            const ImVec2 topRight = iconPoint(min, size, 0.80F, 0.36F);
            const ImVec2 bottomRight = iconPoint(min, size, 0.80F, 0.70F);
            const ImVec2 bottomCenter = iconPoint(min, size, 0.50F, 0.86F);
            const ImVec2 bottomLeft = iconPoint(min, size, 0.20F, 0.70F);
            const ImVec2 middle = iconPoint(min, size, 0.50F, 0.52F);
            std::array<ImVec2, 6> points{
                topLeft, topCenter, topRight, bottomRight, bottomCenter, bottomLeft};
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

        void drawGlyphById(ImDrawList& drawList, const EditorIconDescriptor& descriptor,
                           ImVec2 min, float size, ImU32 color, float stroke) {
            if (iconIs(descriptor, "lucide.folder")) {
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
                drawBoxShape(drawList, min, size,
                             IconDrawStyle{.color = color, .stroke = stroke});
            } else if (iconIs(descriptor, "lucide.triangle-alert") ||
                       iconIs(descriptor, "lucide.circle-alert")) {
                drawDiagnosticShape(drawList, min, size, color, stroke,
                                    DiagnosticIconMarker::Exclamation);
            } else if (iconIs(descriptor, "lucide.circle-help")) {
                drawDiagnosticShape(drawList, min, size, color, stroke,
                                    DiagnosticIconMarker::Question);
            } else {
                drawFileShape(drawList, min, size, color, stroke);
            }
        }

    } // namespace

    asharia::VoidResult EditorAssetIconRegistry::registerResolver(
        std::string resolverId, EditorAssetIconResolver resolver) {
        if (resolverId.empty()) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Editor asset icon resolver id must not be empty"}};
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

    EditorIconDescriptor EditorAssetIconRegistry::resolveAssetIcon(
        const EditorAssetIconQuery& query) const {
        for (const ResolverEntry& entry : resolvers_) {
            if (std::optional<EditorIconDescriptor> resolved = entry.resolver(query)) {
                return *resolved;
            }
        }
        return builtInAssetIcon(query);
    }

    std::size_t EditorAssetIconRegistry::resolverCount() const noexcept {
        return resolvers_.size();
    }

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

    bool validateEditorAssetIconSmoke() {
        EditorAssetIconRegistry registry;
        if (registry
                .resolveAssetIcon(EditorAssetIconQuery{
                    .folder = true,
                    .assetType = {},
                    .importerId = {},
                    .extension = {},
                    .diagnostic = EditorAssetIconDiagnosticState::None,
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
                })
                .id.value != "lucide.image") {
            asharia::logError("Editor asset icon smoke missed image extension fallback.");
            return false;
        }
        if (registry
                .resolveAssetIcon(EditorAssetIconQuery{
                    .folder = false,
                    .assetType = "material",
                    .importerId = {},
                    .extension = {},
                    .diagnostic = EditorAssetIconDiagnosticState::None,
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
                })
                .id.value != "lucide.circle-help") {
            asharia::logError("Editor asset icon smoke missed missing diagnostic fallback.");
            return false;
        }

        auto custom = registry.registerResolver(
            "smoke.shader-icon",
            [](const EditorAssetIconQuery& query) -> std::optional<EditorIconDescriptor> {
                if (normalizedExtension(query.extension) != ".slang") {
                    return std::nullopt;
                }
                return icon("lucide.sparkles", tint(0.94F, 0.82F, 0.42F),
                            "icon.shader.custom", "Custom shader");
            });
        if (!custom) {
            asharia::logError(custom.error().message);
            return false;
        }
        if (registry.resolverCount() != 1U ||
            registry
                    .resolveAssetIcon(EditorAssetIconQuery{
                        .folder = false,
                        .assetType = {},
                        .importerId = {},
                        .extension = ".slang",
                        .diagnostic = EditorAssetIconDiagnosticState::None,
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

        return true;
    }

} // namespace asharia::editor
