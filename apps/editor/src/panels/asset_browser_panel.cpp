#include "panels/asset_browser_panel.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_asset_icon.hpp"
#include "editor_i18n.hpp"
#include "editor_ui.hpp"

namespace {

    constexpr ImGuiTableFlags kAssetTableFlags = ImGuiTableFlags_RowBg |
                                                 ImGuiTableFlags_BordersInnerH |
                                                 ImGuiTableFlags_SizingStretchProp |
                                                 ImGuiTableFlags_Resizable;

    struct AssetBrowserRow {
        std::string_view name;
        std::string_view path;
        std::string_view assetType;
        std::string_view importerId;
        std::string_view extension;
        bool folder{};
        asharia::editor::EditorAssetIconDiagnosticState diagnostic{
            asharia::editor::EditorAssetIconDiagnosticState::None};
        std::string_view stateKey;
        std::string_view stateFallback;
        asharia::editor::EditorUiTone stateTone{asharia::editor::EditorUiTone::Muted};
    };

    constexpr std::array<AssetBrowserRow, 7> kSyntheticAssetRows{{
        AssetBrowserRow{
            .name = "Materials",
            .path = "Assets/Materials",
            .assetType = "folder",
            .importerId = {},
            .extension = {},
            .folder = true,
            .diagnostic = asharia::editor::EditorAssetIconDiagnosticState::None,
            .stateKey = "assetBrowser.state.folder",
            .stateFallback = "folder",
            .stateTone = asharia::editor::EditorUiTone::Muted,
        },
        AssetBrowserRow{
            .name = "brushed_metal.amat",
            .path = "Assets/Materials/brushed_metal.amat",
            .assetType = "material",
            .importerId = "asharia.material",
            .extension = ".amat",
            .folder = false,
            .diagnostic = asharia::editor::EditorAssetIconDiagnosticState::None,
            .stateKey = "assetBrowser.state.ready",
            .stateFallback = "ready",
            .stateTone = asharia::editor::EditorUiTone::Success,
        },
        AssetBrowserRow{
            .name = "grid.slang",
            .path = "Assets/Shaders/grid.slang",
            .assetType = "shader",
            .importerId = "asharia.shader-slang",
            .extension = ".slang",
            .folder = false,
            .diagnostic = asharia::editor::EditorAssetIconDiagnosticState::None,
            .stateKey = "assetBrowser.state.ready",
            .stateFallback = "ready",
            .stateTone = asharia::editor::EditorUiTone::Success,
        },
        AssetBrowserRow{
            .name = "cube.mesh",
            .path = "Assets/Meshes/cube.mesh",
            .assetType = "mesh",
            .importerId = "asharia.mesh-placeholder",
            .extension = ".mesh",
            .folder = false,
            .diagnostic = asharia::editor::EditorAssetIconDiagnosticState::None,
            .stateKey = "assetBrowser.state.synthetic",
            .stateFallback = "synthetic",
            .stateTone = asharia::editor::EditorUiTone::Info,
        },
        AssetBrowserRow{
            .name = "checker.png",
            .path = "Assets/Textures/checker.png",
            .assetType = "texture",
            .importerId = "asharia.texture-placeholder",
            .extension = ".png",
            .folder = false,
            .diagnostic = asharia::editor::EditorAssetIconDiagnosticState::None,
            .stateKey = "assetBrowser.state.synthetic",
            .stateFallback = "synthetic",
            .stateTone = asharia::editor::EditorUiTone::Info,
        },
        AssetBrowserRow{
            .name = "readme.md",
            .path = "Assets/readme.md",
            .assetType = "text",
            .importerId = {},
            .extension = ".md",
            .folder = false,
            .diagnostic = asharia::editor::EditorAssetIconDiagnosticState::None,
            .stateKey = "assetBrowser.state.ready",
            .stateFallback = "ready",
            .stateTone = asharia::editor::EditorUiTone::Success,
        },
        AssetBrowserRow{
            .name = "missing-source",
            .path = "Assets/Missing/missing-source",
            .assetType = {},
            .importerId = {},
            .extension = {},
            .folder = false,
            .diagnostic = asharia::editor::EditorAssetIconDiagnosticState::Missing,
            .stateKey = "assetBrowser.state.missing",
            .stateFallback = "missing",
            .stateTone = asharia::editor::EditorUiTone::Warning,
        },
    }};

    void text(std::string_view value) {
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
    }

    [[nodiscard]] std::string lower(std::string_view value) {
        std::string lowered;
        lowered.reserve(value.size());
        for (char character : value) {
            lowered.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
        return lowered;
    }

    [[nodiscard]] bool rowMatchesFilter(const AssetBrowserRow& row, std::string_view filter) {
        if (filter.empty()) {
            return true;
        }
        const std::string needle = lower(filter);
        const std::string name = lower(row.name);
        const std::string path = lower(row.path);
        const std::string type = lower(row.assetType);
        return name.contains(needle) || path.contains(needle) || type.contains(needle);
    }

    [[nodiscard]] asharia::editor::EditorAssetIconQuery iconQueryForRow(
        const AssetBrowserRow& row) {
        return asharia::editor::EditorAssetIconQuery{
            .folder = row.folder,
            .assetType = std::string{row.assetType},
            .importerId = std::string{row.importerId},
            .extension = std::string{row.extension},
            .diagnostic = row.diagnostic,
        };
    }

    [[nodiscard]] std::string label(const asharia::editor::EditorI18n& i18n,
                                    std::string_view key, std::string_view stableId,
                                    std::string_view fallback) {
        return i18n.label(asharia::editor::EditorI18nLabelDesc{
            .key = key,
            .stableId = stableId,
            .fallback = fallback,
        });
    }

    [[nodiscard]] std::string textValue(const asharia::editor::EditorI18n& i18n,
                                        std::string_view key, std::string_view fallback) {
        return std::string{i18n.text(asharia::editor::EditorI18nTextQuery{
            .key = key,
            .fallback = fallback,
        })};
    }

    void drawAssetRowIcon(const asharia::editor::EditorAssetIconRegistry& icons,
                          const AssetBrowserRow& row) {
        const asharia::editor::EditorIconDescriptor descriptor =
            icons.resolveAssetIcon(iconQueryForRow(row));
        asharia::editor::drawEditorIconGlyph(descriptor, 16.0F);
    }

    void drawAssetBrowserRows(const asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                              std::string_view filter) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        if (!ImGui::BeginTable("asset-browser-table", 4, kAssetTableFlags)) {
            return;
        }

        const std::string nameColumn = textValue(i18n, "assetBrowser.column.name", "Name");
        const std::string typeColumn = textValue(i18n, "assetBrowser.column.type", "Type");
        const std::string importerColumn =
            textValue(i18n, "assetBrowser.column.importer", "Importer");
        const std::string stateColumn = textValue(i18n, "assetBrowser.column.state", "State");
        ImGui::TableSetupColumn(nameColumn.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.50F);
        ImGui::TableSetupColumn(typeColumn.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.18F);
        ImGui::TableSetupColumn(importerColumn.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.20F);
        ImGui::TableSetupColumn(stateColumn.c_str(), ImGuiTableColumnFlags_WidthFixed, 88.0F);
        ImGui::TableHeadersRow();

        std::size_t visibleRows = 0;
        for (const AssetBrowserRow& row : kSyntheticAssetRows) {
            if (!rowMatchesFilter(row, filter)) {
                continue;
            }
            ++visibleRows;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            drawAssetRowIcon(context.icons, row);
            ImGui::SameLine();
            text(row.name);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                text(row.path);
                ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(1);
            if (row.folder) {
                text(textValue(i18n, "assetBrowser.type.folder", "folder"));
            } else {
                text(row.assetType);
            }

            ImGui::TableSetColumnIndex(2);
            text(row.importerId.empty() ? "-" : row.importerId);

            ImGui::TableSetColumnIndex(3);
            asharia::editor::drawEditorUiStatusPill(
                i18n.text(asharia::editor::EditorI18nTextQuery{
                    .key = row.stateKey,
                    .fallback = row.stateFallback,
                }),
                row.stateTone);
        }

        if (visibleRows == 0U) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            text(i18n.text(asharia::editor::EditorI18nTextQuery{
                .key = "assetBrowser.noRows",
                .fallback = "No assets",
            }));
        }

        ImGui::EndTable();
    }

} // namespace

namespace asharia::editor {

    const EditorPanelDesc& AssetBrowserPanel::desc() const {
        return desc_;
    }

    void AssetBrowserPanel::drawAssetBrowserPanel(EditorAssetBrowserPanelDrawContext& context,
                                                  EditorPanelState& state) {
        static_cast<void>(state);

        const EditorI18n& i18n = context.ui.i18n;
        ImGui::SetNextItemWidth(-1.0F);
        const std::string filterLabel =
            label(i18n, "assetBrowser.filter", "asset-browser-filter", "Filter");
        ImGui::InputText(filterLabel.c_str(), filter_.data(), filter_.size());
        ImGui::Separator();

        drawAssetBrowserRows(context, std::string_view{filter_.data()});
    }

} // namespace asharia::editor
