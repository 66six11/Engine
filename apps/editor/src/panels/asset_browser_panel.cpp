#include "panels/asset_browser_panel.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <string_view>

#include "asharia/asset_core/asset_catalog_view.hpp"
#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/asset_core/asset_product.hpp"

#include "editor_asset_icon.hpp"
#include "editor_i18n.hpp"
#include "editor_ui.hpp"

namespace {

    constexpr ImGuiTableFlags kAssetTableFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_Resizable;

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

    struct FixtureSourceDesc {
        std::string_view guidText;
        std::string_view assetTypeName;
        std::string_view sourcePath;
        std::string_view importerName;
        std::uint64_t sourceHash{};
        std::uint64_t settingsHash{};
    };

    [[nodiscard]] asharia::asset::SourceAssetRecord sourceRecord(const FixtureSourceDesc& desc) {
        auto guid = asharia::asset::parseAssetGuid(desc.guidText);
        return asharia::asset::SourceAssetRecord{
            .guid = guid ? *guid : asharia::asset::AssetGuid{},
            .assetType = asharia::asset::makeAssetTypeId(desc.assetTypeName),
            .assetTypeName = std::string{desc.assetTypeName},
            .sourcePath = std::string{desc.sourcePath},
            .importerId = asharia::asset::makeImporterId(desc.importerName),
            .importerName = std::string{desc.importerName},
            .importerVersion = asharia::asset::ImporterVersion{1},
            .sourceHash = desc.sourceHash,
            .settingsHash = desc.settingsHash,
        };
    }

    [[nodiscard]] asharia::asset::AssetProductRecord
    productRecord(const asharia::asset::SourceAssetRecord& source, std::uint64_t dependencyHash,
                  std::uint64_t targetProfileHash, std::string_view relativeProductPath,
                  std::uint64_t productSizeBytes) {
        const asharia::asset::AssetProductKey productKey =
            asharia::asset::makeAssetProductKey(source, dependencyHash, targetProfileHash);
        return asharia::asset::AssetProductRecord{
            .key = productKey,
            .relativeProductPath = std::string{relativeProductPath},
            .productSizeBytes = productSizeBytes,
            .productHash = asharia::asset::hashAssetProductKey(productKey),
        };
    }

    [[nodiscard]] asharia::asset::AssetCatalogView makeAssetBrowserCatalogView() {
        constexpr std::string_view kMaterialTypeName = "com.asharia.asset.Material";
        constexpr std::string_view kMeshTypeName = "com.asharia.asset.Mesh";
        constexpr std::string_view kShaderTypeName = "com.asharia.asset.Shader";
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kTextTypeName = "com.asharia.asset.Text";
        const asharia::asset::SourceAssetRecord material = sourceRecord(FixtureSourceDesc{
            .guidText = "b8373128-8e46-44e1-a5a4-df4c2ef9d2ad",
            .assetTypeName = kMaterialTypeName,
            .sourcePath = "Assets/Materials/brushed_metal.amat",
            .importerName = "asharia.material",
            .sourceHash = 0x1001ULL,
            .settingsHash = 0x2001ULL,
        });
        const asharia::asset::SourceAssetRecord shader = sourceRecord(FixtureSourceDesc{
            .guidText = "13a10d4b-6987-48d1-ad27-ae4055e5a936",
            .assetTypeName = kShaderTypeName,
            .sourcePath = "Assets/Shaders/grid.slang",
            .importerName = "asharia.shader-slang",
            .sourceHash = 0x1002ULL,
            .settingsHash = 0x2002ULL,
        });
        asharia::asset::SourceAssetRecord staleMesh = sourceRecord(FixtureSourceDesc{
            .guidText = "1135c477-65aa-4d44-92f1-f208fc6142ad",
            .assetTypeName = kMeshTypeName,
            .sourcePath = "Assets/Meshes/cube.mesh",
            .importerName = "asharia.mesh-placeholder",
            .sourceHash = 0x1003ULL,
            .settingsHash = 0x2003ULL,
        });
        const asharia::asset::SourceAssetRecord texture = sourceRecord(FixtureSourceDesc{
            .guidText = "cd9c0f3d-20e2-4028-a3e9-c3f42d3fd515",
            .assetTypeName = kTextureTypeName,
            .sourcePath = "Assets/Textures/checker.png",
            .importerName = "asharia.texture-placeholder",
            .sourceHash = 0x1004ULL,
            .settingsHash = 0x2004ULL,
        });
        const asharia::asset::SourceAssetRecord note = sourceRecord(FixtureSourceDesc{
            .guidText = "f98f9d88-237f-4e8a-a4b6-9977d3a1fc2b",
            .assetTypeName = kTextTypeName,
            .sourcePath = "Assets/readme.md",
            .importerName = "asharia.text-placeholder",
            .sourceHash = 0x1005ULL,
            .settingsHash = 0x2005ULL,
        });

        asharia::asset::AssetCatalog catalog;
        if (!catalog.addSource(shader) || !catalog.addSource(note) || !catalog.addSource(texture) ||
            !catalog.addSource(material) || !catalog.addSource(staleMesh)) {
            return {};
        }

        const std::uint64_t targetProfile =
            asharia::asset::makeAssetTargetProfileHash("editor-preview");
        asharia::asset::SourceAssetRecord oldMesh = staleMesh;
        oldMesh.sourceHash ^= 0x40ULL;
        const std::array<asharia::asset::AssetProductRecord, 4> products{
            productRecord(material, 0x3001ULL, targetProfile, "materials/brushed_metal.product",
                          512),
            productRecord(shader, 0x3002ULL, targetProfile, "shaders/grid.product", 256),
            productRecord(texture, 0x3004ULL, targetProfile, "textures/checker.product", 1024),
            productRecord(oldMesh, 0x3003ULL, targetProfile, "meshes/cube.old.product", 2048),
        };

        return asharia::asset::buildAssetCatalogView(
            catalog, products, asharia::asset::AssetCatalogViewOptions{.requireProducts = true});
    }

    [[nodiscard]] const asharia::asset::AssetCatalogView& assetBrowserCatalogView() {
        static const asharia::asset::AssetCatalogView view = makeAssetBrowserCatalogView();
        return view;
    }

    [[nodiscard]] bool rowMatchesFilter(const asharia::asset::AssetCatalogViewEntry& row,
                                        std::string_view filter) {
        if (filter.empty()) {
            return true;
        }
        const std::string needle = lower(filter);
        const std::string name = lower(row.displayName);
        const std::string path = lower(row.sourcePath);
        const std::string type = lower(row.assetTypeName);
        const std::string importer = lower(row.importerName);
        return name.contains(needle) || path.contains(needle) || type.contains(needle) ||
               importer.contains(needle);
    }

    [[nodiscard]] asharia::editor::EditorAssetIconDiagnosticState
    diagnosticStateForRow(const asharia::asset::AssetCatalogViewEntry& row) {
        switch (row.productState) {
        case asharia::asset::AssetCatalogProductState::MissingProduct:
            return asharia::editor::EditorAssetIconDiagnosticState::Missing;
        case asharia::asset::AssetCatalogProductState::StaleProduct:
            return asharia::editor::EditorAssetIconDiagnosticState::Warning;
        case asharia::asset::AssetCatalogProductState::InvalidProduct:
            return asharia::editor::EditorAssetIconDiagnosticState::Invalid;
        case asharia::asset::AssetCatalogProductState::NotTracked:
        case asharia::asset::AssetCatalogProductState::Ready:
            return asharia::editor::EditorAssetIconDiagnosticState::None;
        }
        return asharia::editor::EditorAssetIconDiagnosticState::None;
    }

    [[nodiscard]] asharia::editor::EditorAssetIconQuery
    iconQueryForRow(const asharia::asset::AssetCatalogViewEntry& row) {
        return asharia::editor::EditorAssetIconQuery{
            .folder = false,
            .assetType = row.assetTypeName,
            .importerId = row.importerName,
            .extension = row.extension,
            .diagnostic = diagnosticStateForRow(row),
        };
    }

    [[nodiscard]] asharia::editor::EditorUiTone
    toneForState(asharia::asset::AssetCatalogProductState state) {
        switch (state) {
        case asharia::asset::AssetCatalogProductState::Ready:
            return asharia::editor::EditorUiTone::Success;
        case asharia::asset::AssetCatalogProductState::MissingProduct:
        case asharia::asset::AssetCatalogProductState::StaleProduct:
            return asharia::editor::EditorUiTone::Warning;
        case asharia::asset::AssetCatalogProductState::InvalidProduct:
            return asharia::editor::EditorUiTone::Danger;
        case asharia::asset::AssetCatalogProductState::NotTracked:
            return asharia::editor::EditorUiTone::Info;
        }
        return asharia::editor::EditorUiTone::Muted;
    }

    [[nodiscard]] std::string_view stateTextKey(asharia::asset::AssetCatalogProductState state) {
        switch (state) {
        case asharia::asset::AssetCatalogProductState::Ready:
            return "assetBrowser.state.ready";
        case asharia::asset::AssetCatalogProductState::MissingProduct:
            return "assetBrowser.state.missingProduct";
        case asharia::asset::AssetCatalogProductState::StaleProduct:
            return "assetBrowser.state.staleProduct";
        case asharia::asset::AssetCatalogProductState::InvalidProduct:
            return "assetBrowser.state.invalidProduct";
        case asharia::asset::AssetCatalogProductState::NotTracked:
            return "assetBrowser.state.notTracked";
        }
        return "assetBrowser.state.notTracked";
    }

    [[nodiscard]] std::string_view stateFallback(asharia::asset::AssetCatalogProductState state) {
        switch (state) {
        case asharia::asset::AssetCatalogProductState::Ready:
            return "ready";
        case asharia::asset::AssetCatalogProductState::MissingProduct:
            return "missing product";
        case asharia::asset::AssetCatalogProductState::StaleProduct:
            return "stale product";
        case asharia::asset::AssetCatalogProductState::InvalidProduct:
            return "invalid product";
        case asharia::asset::AssetCatalogProductState::NotTracked:
            return "not tracked";
        }
        return "not tracked";
    }

    [[nodiscard]] std::string label(const asharia::editor::EditorI18n& i18n, std::string_view key,
                                    std::string_view stableId, std::string_view fallback) {
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
                          const asharia::asset::AssetCatalogViewEntry& row) {
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
        ImGui::TableSetupColumn(stateColumn.c_str(), ImGuiTableColumnFlags_WidthFixed, 104.0F);
        ImGui::TableHeadersRow();

        std::size_t visibleRows = 0;
        for (const asharia::asset::AssetCatalogViewEntry& row : assetBrowserCatalogView().entries) {
            if (!rowMatchesFilter(row, filter)) {
                continue;
            }
            ++visibleRows;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            drawAssetRowIcon(context.icons, row);
            ImGui::SameLine();
            text(row.displayName);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                text(row.sourcePath);
                ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(1);
            text(row.assetTypeName);

            ImGui::TableSetColumnIndex(2);
            text(row.importerName.empty() ? "-" : row.importerName);

            ImGui::TableSetColumnIndex(3);
            asharia::editor::drawEditorUiStatusPill(i18n.text(asharia::editor::EditorI18nTextQuery{
                                                        .key = stateTextKey(row.productState),
                                                        .fallback = stateFallback(row.productState),
                                                    }),
                                                    toneForState(row.productState));
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
