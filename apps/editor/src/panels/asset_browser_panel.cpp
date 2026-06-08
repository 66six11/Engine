#include "panels/asset_browser_panel.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <imgui.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_catalog_view.hpp"
#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"

#include "editor_asset_catalog.hpp"
#include "editor_asset_icon.hpp"
#include "editor_asset_import_settings_command.hpp"
#include "editor_command.hpp"
#include "editor_i18n.hpp"
#include "editor_ui.hpp"

namespace {

    constexpr ImGuiTableFlags kAssetTableFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable;
    constexpr ImGuiTableFlags kDiagnosticTableFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    constexpr std::array<asharia::asset::AssetCatalogProductState, 5> kProductStateFilterOrder{
        asharia::asset::AssetCatalogProductState::Ready,
        asharia::asset::AssetCatalogProductState::MissingProduct,
        asharia::asset::AssetCatalogProductState::StaleProduct,
        asharia::asset::AssetCatalogProductState::InvalidProduct,
        asharia::asset::AssetCatalogProductState::NotTracked,
    };

    struct AssetBrowserSummary {
        std::size_t totalRows{};
        std::size_t visibleRows{};
        std::size_t readyRows{};
        std::size_t missingRows{};
        std::size_t staleRows{};
        std::size_t invalidRows{};
        std::size_t notTrackedRows{};
        std::size_t rowDiagnostics{};
    };

    struct AssetBrowserSelectionQuery {
        std::string_view filter;
        std::string_view folderScope;
        std::string_view assetTypeFilter;
        std::string_view importProfileFilter;
        std::string_view productStateFilter;
        std::string_view selectedAssetKey;
    };

    struct AssetBrowserVisibilityQuery {
        std::string_view filter;
        std::string_view folderScope;
        std::string_view assetTypeFilter;
        std::string_view importProfileFilter;
        std::string_view productStateFilter;
    };

    struct AssetBrowserPathScopeQuery {
        std::string_view sourcePath;
        std::string_view folderScope;
    };

    struct AssetBrowserFolderScopeState {
        std::string* selectedFolderScope{};
        std::string* selectedAssetTypeFilter{};
        std::string* selectedImportProfileFilter{};
        std::string* selectedProductStateFilter{};
        std::string* selectedAssetKey{};
        std::string* selectedSubAssetStableId{};
    };

    struct AssetBrowserNavigationTreeState {
        std::string* selectedFolderScope{};
        std::string* selectedAssetTypeFilter{};
        std::string* selectedImportProfileFilter{};
        std::string* selectedProductStateFilter{};
        std::string* selectedAssetKey{};
        std::string* selectedNavigationKey{};
        std::string* selectedSubAssetStableId{};
    };

    struct AssetBrowserImportProfileFilterQuery {
        std::string_view folderScope;
        std::string_view assetTypeFilter;
    };

    struct AssetBrowserProductStateFilterQuery {
        std::string_view folderScope;
        std::string_view assetTypeFilter;
        std::string_view importProfileFilter;
    };

    struct AssetBrowserCopyButtonQuery {
        std::string_view value;
        std::string_view stableId;
        std::string_view tooltipKey;
        std::string_view tooltipFallback;
    };

    struct AssetBrowserRowsState {
        std::string* selectedAssetKey{};
        std::string* selectedNavigationKey{};
        std::string* selectedSubAssetStableId{};
    };

    struct AssetBrowserImportSettingsUiState {
        std::string* selectionKey{};
        std::string* textureProfileDraft{};
        std::string* textureProfileBaseline{};
        std::string* message{};
        bool* messageIsError{};
    };

    struct AssetBrowserTextureProfileOption {
        std::string_view value;
        std::string_view textKey;
        std::string_view fallback;
    };

    constexpr std::array<AssetBrowserTextureProfileOption, 4> kTextureProfileOptions{
        AssetBrowserTextureProfileOption{
            .value = asharia::asset::kTextureImportProfileTexture2D,
            .textKey = "assetBrowser.importSettings.profile.texture2d",
            .fallback = "Texture2D",
        },
        AssetBrowserTextureProfileOption{
            .value = asharia::asset::kTextureImportProfileSpriteSheet,
            .textKey = "assetBrowser.importSettings.profile.spriteSheet",
            .fallback = "Sprite Sheet",
        },
        AssetBrowserTextureProfileOption{
            .value = asharia::asset::kTextureImportProfileTextureCube,
            .textKey = "assetBrowser.importSettings.profile.textureCube",
            .fallback = "Texture Cube",
        },
        AssetBrowserTextureProfileOption{
            .value = asharia::asset::kTextureImportProfileSkybox,
            .textKey = "assetBrowser.importSettings.profile.skybox",
            .fallback = "Skybox",
        },
    };

    struct AssetBrowserTextCompareQuery {
        std::string_view left;
        std::string_view right;
    };

    enum class AssetBrowserSortColumn : std::uint8_t {
        Name,
        Type,
        ImportProfile,
        Importer,
        State,
    };

    struct AssetBrowserRowCompareQuery {
        const asharia::asset::AssetCatalogViewEntry* left{};
        const asharia::asset::AssetCatalogViewEntry* right{};
        AssetBrowserSortColumn column{AssetBrowserSortColumn::Name};
    };

    struct AssetBrowserRowSortCompareQuery {
        const asharia::asset::AssetCatalogViewEntry* left{};
        const asharia::asset::AssetCatalogViewEntry* right{};
        const ImGuiTableColumnSortSpecs* sortSpec{};
    };

    void text(std::string_view value) {
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
    }

    [[nodiscard]] std::string pathUtf8String(const std::filesystem::path& path) {
        const std::u8string value = path.generic_u8string();
        return std::string{value.begin(), value.end()};
    }

    [[nodiscard]] std::string_view textOrDash(const std::string& value) {
        return value.empty() ? std::string_view{"-"} : std::string_view{value};
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

    [[nodiscard]] std::string_view
    displayNameForRow(const asharia::asset::AssetCatalogViewEntry& row) {
        return row.displayName.empty() ? std::string_view{row.sourcePath}
                                       : std::string_view{row.displayName};
    }

    [[nodiscard]] int compareText(AssetBrowserTextCompareQuery query) {
        const std::size_t size = std::min(query.left.size(), query.right.size());
        for (std::size_t index = 0U; index < size; ++index) {
            const auto leftChar = static_cast<unsigned char>(
                std::tolower(static_cast<unsigned char>(query.left[index])));
            const auto rightChar = static_cast<unsigned char>(
                std::tolower(static_cast<unsigned char>(query.right[index])));
            if (leftChar < rightChar) {
                return -1;
            }
            if (leftChar > rightChar) {
                return 1;
            }
        }
        if (query.left.size() < query.right.size()) {
            return -1;
        }
        if (query.left.size() > query.right.size()) {
            return 1;
        }
        return 0;
    }

    [[nodiscard]] std::size_t productStateSortRank(asharia::asset::AssetCatalogProductState state) {
        std::size_t rank = 0U;
        for (const asharia::asset::AssetCatalogProductState orderedState :
             kProductStateFilterOrder) {
            if (orderedState == state) {
                return rank;
            }
            ++rank;
        }
        return kProductStateFilterOrder.size();
    }

    [[nodiscard]] bool textContainsFilter(std::string_view value, std::string_view loweredFilter) {
        return !value.empty() && lower(value).contains(loweredFilter);
    }

    [[nodiscard]] bool
    diagnosticMatchesFilter(const asharia::asset::AssetCatalogDiagnostic& diagnostic,
                            std::string_view loweredFilter) {
        return textContainsFilter(asharia::asset::assetCatalogDiagnosticCodeName(diagnostic.code),
                                  loweredFilter) ||
               textContainsFilter(diagnostic.sourcePath, loweredFilter) ||
               textContainsFilter(diagnostic.message, loweredFilter);
    }

    [[nodiscard]] bool
    subAssetMatchesFilter(const asharia::asset::AssetCatalogSubAssetViewEntry& subAsset,
                          std::string_view loweredFilter) {
        return textContainsFilter(subAsset.stableId, loweredFilter) ||
               textContainsFilter(subAsset.displayName, loweredFilter) ||
               textContainsFilter(subAsset.assetRoleName, loweredFilter);
    }

    [[nodiscard]] bool rowMatchesFilter(const asharia::asset::AssetCatalogViewEntry& row,
                                        std::string_view filter) {
        if (filter.empty()) {
            return true;
        }
        const std::string needle = lower(filter);
        if (textContainsFilter(row.displayName, needle) ||
            textContainsFilter(row.sourcePath, needle) ||
            textContainsFilter(row.assetTypeName, needle) ||
            textContainsFilter(row.importerName, needle) ||
            textContainsFilter(row.importProfileName, needle) ||
            textContainsFilter(row.assetRoleName, needle) ||
            textContainsFilter(row.extension, needle) || textContainsFilter(row.guidText, needle) ||
            textContainsFilter(asharia::asset::assetCatalogProductStateName(row.productState),
                               needle)) {
            return true;
        }
        if (std::ranges::any_of(
                row.subAssets,
                [needle](const asharia::asset::AssetCatalogSubAssetViewEntry& subAsset) {
                    return subAssetMatchesFilter(subAsset, needle);
                })) {
            return true;
        }
        return std::ranges::any_of(
            row.diagnostics, [needle](const asharia::asset::AssetCatalogDiagnostic& diagnostic) {
                return diagnosticMatchesFilter(diagnostic, needle);
            });
    }

    [[nodiscard]] std::string_view folderNameForScope(std::string_view scope) {
        const std::size_t slash = scope.find_last_of('/');
        if (slash == std::string_view::npos) {
            return scope;
        }
        return scope.substr(slash + 1U);
    }

    [[nodiscard]] std::string_view folderScopeForSourcePath(std::string_view sourcePath) {
        const std::size_t slash = sourcePath.find_last_of('/');
        if (slash == std::string_view::npos) {
            return {};
        }
        return sourcePath.substr(0U, slash);
    }

    [[nodiscard]] std::vector<std::string> folderBreadcrumbScopes(std::string_view folderScope) {
        std::vector<std::string> scopes;
        std::size_t start = 0U;
        while (start < folderScope.size()) {
            const std::size_t slash = folderScope.find('/', start);
            if (slash == std::string_view::npos) {
                scopes.emplace_back(folderScope);
                break;
            }
            if (slash != 0U) {
                scopes.emplace_back(folderScope.substr(0U, slash));
            }
            start = slash + 1U;
        }
        return scopes;
    }

    [[nodiscard]] bool startsWithFolderScope(AssetBrowserPathScopeQuery query) {
        if (query.folderScope.empty()) {
            return true;
        }
        if (!query.sourcePath.starts_with(query.folderScope)) {
            return false;
        }
        return query.sourcePath.size() == query.folderScope.size() ||
               query.sourcePath[query.folderScope.size()] == '/';
    }

    [[nodiscard]] bool rowMatchesVisibility(const asharia::asset::AssetCatalogViewEntry& row,
                                            AssetBrowserVisibilityQuery query) {
        return startsWithFolderScope(AssetBrowserPathScopeQuery{
                   .sourcePath = row.sourcePath,
                   .folderScope = query.folderScope,
               }) &&
               (query.assetTypeFilter.empty() || row.assetTypeName == query.assetTypeFilter) &&
               (query.importProfileFilter.empty() ||
                row.importProfileName == query.importProfileFilter) &&
               (query.productStateFilter.empty() ||
                asharia::asset::assetCatalogProductStateName(row.productState) ==
                    query.productStateFilter) &&
               rowMatchesFilter(row, query.filter);
    }

    [[nodiscard]] std::string
    childFolderScopeForRow(const asharia::asset::AssetCatalogViewEntry& row,
                           std::string_view folderScope) {
        if (!startsWithFolderScope(AssetBrowserPathScopeQuery{
                .sourcePath = row.sourcePath,
                .folderScope = folderScope,
            })) {
            return {};
        }
        std::string_view rest = row.sourcePath;
        if (!folderScope.empty()) {
            rest.remove_prefix(folderScope.size());
            if (rest.empty()) {
                return {};
            }
            rest.remove_prefix(1U);
        }
        const std::size_t slash = rest.find('/');
        if (slash == std::string_view::npos || slash == 0U) {
            return {};
        }
        std::string childScope{folderScope};
        if (!childScope.empty()) {
            childScope += '/';
        }
        const std::string_view childName = rest.substr(0, slash);
        childScope.append(childName.data(), childName.size());
        return childScope;
    }

    [[nodiscard]] std::vector<std::string>
    childFolderScopes(const asharia::asset::AssetCatalogView& catalogView,
                      std::string_view folderScope) {
        std::vector<std::string> scopes;
        for (const asharia::asset::AssetCatalogViewEntry& row : catalogView.entries) {
            std::string childScope = childFolderScopeForRow(row, folderScope);
            if (!childScope.empty()) {
                scopes.push_back(std::move(childScope));
            }
        }
        std::ranges::sort(scopes);
        const auto uniqueEnd = std::ranges::unique(scopes);
        scopes.erase(uniqueEnd.begin(), uniqueEnd.end());
        return scopes;
    }

    [[nodiscard]] std::vector<std::string>
    assetTypeFilters(const asharia::asset::AssetCatalogView& catalogView,
                     std::string_view folderScope) {
        std::vector<std::string> filters;
        for (const asharia::asset::AssetCatalogViewEntry& row : catalogView.entries) {
            if (!startsWithFolderScope(AssetBrowserPathScopeQuery{
                    .sourcePath = row.sourcePath,
                    .folderScope = folderScope,
                }) ||
                row.assetTypeName.empty()) {
                continue;
            }
            filters.push_back(row.assetTypeName);
        }
        std::ranges::sort(filters);
        const auto uniqueEnd = std::ranges::unique(filters);
        filters.erase(uniqueEnd.begin(), uniqueEnd.end());
        return filters;
    }

    [[nodiscard]] bool assetTypeFilterExists(const asharia::asset::AssetCatalogView& catalogView,
                                             std::string_view folderScope,
                                             std::string_view assetTypeFilter) {
        if (assetTypeFilter.empty()) {
            return true;
        }
        return std::ranges::any_of(
            catalogView.entries,
            [folderScope, assetTypeFilter](const asharia::asset::AssetCatalogViewEntry& row) {
                return startsWithFolderScope(AssetBrowserPathScopeQuery{
                           .sourcePath = row.sourcePath,
                           .folderScope = folderScope,
                       }) &&
                       row.assetTypeName == assetTypeFilter;
            });
    }

    [[nodiscard]] bool
    rowMatchesImportProfileFilterScope(const asharia::asset::AssetCatalogViewEntry& row,
                                       AssetBrowserImportProfileFilterQuery query) {
        return startsWithFolderScope(AssetBrowserPathScopeQuery{
                   .sourcePath = row.sourcePath,
                   .folderScope = query.folderScope,
               }) &&
               (query.assetTypeFilter.empty() || row.assetTypeName == query.assetTypeFilter);
    }

    [[nodiscard]] std::vector<std::string>
    importProfileFilters(const asharia::asset::AssetCatalogView& catalogView,
                         AssetBrowserImportProfileFilterQuery query) {
        std::vector<std::string> filters;
        for (const asharia::asset::AssetCatalogViewEntry& row : catalogView.entries) {
            if (!rowMatchesImportProfileFilterScope(row, query) || row.importProfileName.empty()) {
                continue;
            }
            filters.push_back(row.importProfileName);
        }
        std::ranges::sort(filters);
        const auto uniqueEnd = std::ranges::unique(filters);
        filters.erase(uniqueEnd.begin(), uniqueEnd.end());
        return filters;
    }

    [[nodiscard]] bool
    importProfileFilterExists(const asharia::asset::AssetCatalogView& catalogView,
                              AssetBrowserImportProfileFilterQuery query,
                              std::string_view importProfileFilter) {
        if (importProfileFilter.empty()) {
            return true;
        }
        return std::ranges::any_of(
            catalogView.entries,
            [query, importProfileFilter](const asharia::asset::AssetCatalogViewEntry& row) {
                return rowMatchesImportProfileFilterScope(row, query) &&
                       row.importProfileName == importProfileFilter;
            });
    }

    [[nodiscard]] bool
    rowMatchesProductStateFilterScope(const asharia::asset::AssetCatalogViewEntry& row,
                                      AssetBrowserProductStateFilterQuery query) {
        return startsWithFolderScope(AssetBrowserPathScopeQuery{
                   .sourcePath = row.sourcePath,
                   .folderScope = query.folderScope,
               }) &&
               (query.assetTypeFilter.empty() || row.assetTypeName == query.assetTypeFilter) &&
               (query.importProfileFilter.empty() ||
                row.importProfileName == query.importProfileFilter);
    }

    [[nodiscard]] std::vector<asharia::asset::AssetCatalogProductState>
    productStateFilters(const asharia::asset::AssetCatalogView& catalogView,
                        AssetBrowserProductStateFilterQuery query) {
        std::vector<asharia::asset::AssetCatalogProductState> filters;
        for (const asharia::asset::AssetCatalogProductState state : kProductStateFilterOrder) {
            const bool stateExists = std::ranges::any_of(
                catalogView.entries,
                [query, state](const asharia::asset::AssetCatalogViewEntry& row) {
                    return rowMatchesProductStateFilterScope(row, query) &&
                           row.productState == state;
                });
            if (stateExists) {
                filters.push_back(state);
            }
        }
        return filters;
    }

    [[nodiscard]] bool productStateFilterExists(const asharia::asset::AssetCatalogView& catalogView,
                                                AssetBrowserProductStateFilterQuery query,
                                                std::string_view productStateFilter) {
        if (productStateFilter.empty()) {
            return true;
        }
        return std::ranges::any_of(
            catalogView.entries,
            [query, productStateFilter](const asharia::asset::AssetCatalogViewEntry& row) {
                return rowMatchesProductStateFilterScope(row, query) &&
                       asharia::asset::assetCatalogProductStateName(row.productState) ==
                           productStateFilter;
            });
    }

    [[nodiscard]] int compareRowsBySortColumn(AssetBrowserRowCompareQuery query) {
        switch (query.column) {
        case AssetBrowserSortColumn::Name:
            return compareText(AssetBrowserTextCompareQuery{
                .left = displayNameForRow(*query.left),
                .right = displayNameForRow(*query.right),
            });
        case AssetBrowserSortColumn::Type:
            return compareText(AssetBrowserTextCompareQuery{
                .left = query.left->assetTypeName,
                .right = query.right->assetTypeName,
            });
        case AssetBrowserSortColumn::ImportProfile:
            return compareText(AssetBrowserTextCompareQuery{
                .left = query.left->importProfileName,
                .right = query.right->importProfileName,
            });
        case AssetBrowserSortColumn::Importer:
            return compareText(AssetBrowserTextCompareQuery{
                .left = query.left->importerName,
                .right = query.right->importerName,
            });
        case AssetBrowserSortColumn::State: {
            const std::size_t leftRank = productStateSortRank(query.left->productState);
            const std::size_t rightRank = productStateSortRank(query.right->productState);
            if (leftRank < rightRank) {
                return -1;
            }
            if (leftRank > rightRank) {
                return 1;
            }
            return 0;
        }
        }
        return 0;
    }

    [[nodiscard]] AssetBrowserSortColumn sortColumnForIndex(int columnIndex) {
        switch (columnIndex) {
        case 1:
            return AssetBrowserSortColumn::Type;
        case 2:
            return AssetBrowserSortColumn::ImportProfile;
        case 3:
            return AssetBrowserSortColumn::Importer;
        case 4:
            return AssetBrowserSortColumn::State;
        case 0:
        default:
            return AssetBrowserSortColumn::Name;
        }
    }

    [[nodiscard]] int compareRowTieBreakers(AssetBrowserRowCompareQuery query) {
        const int sourcePathComparison = compareText(AssetBrowserTextCompareQuery{
            .left = query.left->sourcePath,
            .right = query.right->sourcePath,
        });
        if (sourcePathComparison != 0) {
            return sourcePathComparison;
        }
        return compareText(AssetBrowserTextCompareQuery{
            .left = query.left->guidText,
            .right = query.right->guidText,
        });
    }

    [[nodiscard]] int compareRowsForSort(AssetBrowserRowSortCompareQuery query) {
        const AssetBrowserSortColumn column = sortColumnForIndex(query.sortSpec->ColumnIndex);
        int comparison = compareRowsBySortColumn(AssetBrowserRowCompareQuery{
            .left = query.left,
            .right = query.right,
            .column = column,
        });
        if (comparison == 0) {
            comparison = compareRowTieBreakers(AssetBrowserRowCompareQuery{
                .left = query.left,
                .right = query.right,
                .column = column,
            });
        }
        if (query.sortSpec->SortDirection == ImGuiSortDirection_Descending) {
            comparison = -comparison;
        }
        return comparison;
    }

    [[nodiscard]] std::vector<const asharia::asset::AssetCatalogViewEntry*>
    visibleSortedRows(const asharia::asset::AssetCatalogView& catalogView,
                      AssetBrowserVisibilityQuery query, const ImGuiTableSortSpecs* sortSpecs) {
        std::vector<const asharia::asset::AssetCatalogViewEntry*> rows;
        for (const asharia::asset::AssetCatalogViewEntry& row : catalogView.entries) {
            if (rowMatchesVisibility(row, query)) {
                rows.push_back(&row);
            }
        }
        if (sortSpecs == nullptr || sortSpecs->SpecsCount == 0 || sortSpecs->Specs == nullptr) {
            return rows;
        }

        const ImGuiTableColumnSortSpecs sortSpec = *sortSpecs->Specs;
        std::ranges::stable_sort(rows,
                                 [sortSpec](const asharia::asset::AssetCatalogViewEntry* left,
                                            const asharia::asset::AssetCatalogViewEntry* right) {
                                     return compareRowsForSort(AssetBrowserRowSortCompareQuery{
                                                .left = left,
                                                .right = right,
                                                .sortSpec = &sortSpec,
                                            }) < 0;
                                 });
        return rows;
    }

    [[nodiscard]] std::string_view
    selectionKeyForRow(const asharia::asset::AssetCatalogViewEntry& row) {
        if (!row.guidText.empty()) {
            return row.guidText;
        }
        if (!row.sourcePath.empty()) {
            return row.sourcePath;
        }
        return row.displayName;
    }

    [[nodiscard]] const asharia::asset::AssetCatalogViewEntry*
    selectedVisibleRow(const asharia::asset::AssetCatalogView& catalogView,
                       AssetBrowserSelectionQuery query) {
        const asharia::asset::AssetCatalogViewEntry* firstVisible = nullptr;
        for (const asharia::asset::AssetCatalogViewEntry& row : catalogView.entries) {
            if (!rowMatchesVisibility(row, AssetBrowserVisibilityQuery{
                                               .filter = query.filter,
                                               .folderScope = query.folderScope,
                                               .assetTypeFilter = query.assetTypeFilter,
                                               .importProfileFilter = query.importProfileFilter,
                                               .productStateFilter = query.productStateFilter,
                                           })) {
                continue;
            }
            if (firstVisible == nullptr) {
                firstVisible = &row;
            }
            if (!query.selectedAssetKey.empty() &&
                selectionKeyForRow(row) == query.selectedAssetKey) {
                return &row;
            }
        }
        return firstVisible;
    }

    [[nodiscard]] const asharia::asset::AssetCatalogSubAssetViewEntry*
    findSubAssetByStableId(const asharia::asset::AssetCatalogViewEntry& row,
                           std::string_view stableId) {
        if (stableId.empty()) {
            return nullptr;
        }
        const auto found = std::ranges::find_if(
            row.subAssets,
            [stableId](const asharia::asset::AssetCatalogSubAssetViewEntry& subAsset) {
                return subAsset.stableId == stableId;
            });
        return found == row.subAssets.end() ? nullptr : &*found;
    }

    [[nodiscard]] AssetBrowserSummary
    summarizeVisibleRows(const asharia::asset::AssetCatalogView& catalogView,
                         AssetBrowserVisibilityQuery query) {
        AssetBrowserSummary summary{.totalRows = catalogView.entries.size()};
        for (const asharia::asset::AssetCatalogViewEntry& row : catalogView.entries) {
            if (!rowMatchesVisibility(row, query)) {
                continue;
            }
            ++summary.visibleRows;
            summary.rowDiagnostics += row.diagnostics.size();
            switch (row.productState) {
            case asharia::asset::AssetCatalogProductState::Ready:
                ++summary.readyRows;
                break;
            case asharia::asset::AssetCatalogProductState::MissingProduct:
                ++summary.missingRows;
                break;
            case asharia::asset::AssetCatalogProductState::StaleProduct:
                ++summary.staleRows;
                break;
            case asharia::asset::AssetCatalogProductState::InvalidProduct:
                ++summary.invalidRows;
                break;
            case asharia::asset::AssetCatalogProductState::NotTracked:
                ++summary.notTrackedRows;
                break;
            }
        }
        return summary;
    }

    [[nodiscard]] asharia::editor::EditorUiTone
    toneForDiagnosticSeverity(asharia::editor::EditorAssetCatalogDiagnosticSeverity severity) {
        switch (severity) {
        case asharia::editor::EditorAssetCatalogDiagnosticSeverity::Info:
            return asharia::editor::EditorUiTone::Info;
        case asharia::editor::EditorAssetCatalogDiagnosticSeverity::Warning:
            return asharia::editor::EditorUiTone::Warning;
        case asharia::editor::EditorAssetCatalogDiagnosticSeverity::Error:
            return asharia::editor::EditorUiTone::Danger;
        }
        return asharia::editor::EditorUiTone::Muted;
    }

    [[nodiscard]] std::string_view
    assetDiagnosticSeverityName(asharia::asset::AssetCatalogDiagnosticSeverity severity) {
        switch (severity) {
        case asharia::asset::AssetCatalogDiagnosticSeverity::Info:
            return "info";
        case asharia::asset::AssetCatalogDiagnosticSeverity::Warning:
            return "warning";
        case asharia::asset::AssetCatalogDiagnosticSeverity::Error:
            return "error";
        }
        return "info";
    }

    [[nodiscard]] asharia::editor::EditorUiTone
    toneForAssetDiagnosticSeverity(asharia::asset::AssetCatalogDiagnosticSeverity severity) {
        switch (severity) {
        case asharia::asset::AssetCatalogDiagnosticSeverity::Info:
            return asharia::editor::EditorUiTone::Info;
        case asharia::asset::AssetCatalogDiagnosticSeverity::Warning:
            return asharia::editor::EditorUiTone::Warning;
        case asharia::asset::AssetCatalogDiagnosticSeverity::Error:
            return asharia::editor::EditorUiTone::Danger;
        }
        return asharia::editor::EditorUiTone::Muted;
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

    [[nodiscard]] std::string
    targetProfileForSnapshot(const asharia::editor::EditorAssetBrowserPanelDrawContext& context) {
        if (context.catalogSnapshot != nullptr && !context.catalogSnapshot->targetProfile.empty()) {
            return context.catalogSnapshot->targetProfile;
        }
        return "editor-preview";
    }

    [[nodiscard]] std::string textureProfileLabel(const asharia::editor::EditorI18n& i18n,
                                                  std::string_view profile) {
        for (const AssetBrowserTextureProfileOption& option : kTextureProfileOptions) {
            if (option.value == profile) {
                return textValue(i18n, option.textKey, option.fallback);
            }
        }
        return profile.empty() ? std::string{"-"} : std::string{profile};
    }

    [[nodiscard]] asharia::asset::AssetGuid
    parseRowGuid(const asharia::asset::AssetCatalogViewEntry& row) {
        if (row.guidText.empty()) {
            return {};
        }
        auto parsed = asharia::asset::parseAssetGuid(row.guidText);
        return parsed ? *parsed : asharia::asset::AssetGuid{};
    }

    [[nodiscard]] const asharia::editor::EditorAssetPendingReimport*
    findPendingReimportForRow(const asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                              const asharia::asset::AssetCatalogViewEntry& row) {
        return context.pendingReimports.findPending(asharia::editor::EditorAssetReimportSourceKey{
            .guid = parseRowGuid(row),
            .sourcePath = row.sourcePath,
            .targetProfile = targetProfileForSnapshot(context),
        });
    }

    [[nodiscard]] std::size_t
    recordNewPendingRequests(asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                             std::size_t oldRequestCount) {
        const std::vector<asharia::editor::EditorAssetReimportRequest>& requests =
            context.reimportRequests.requests();
        if (oldRequestCount >= requests.size()) {
            return 0U;
        }

        const std::span<const asharia::editor::EditorAssetReimportRequest> allRequests{requests};
        const std::span<const asharia::editor::EditorAssetReimportRequest> newRequests =
            allRequests.subspan(oldRequestCount);
        context.pendingReimports.recordAll(newRequests);
        return newRequests.size();
    }

    [[nodiscard]] bool
    isTextureImportSettingsRow(const asharia::asset::AssetCatalogViewEntry& row) {
        return !row.importProfileName.empty() && !row.sourcePath.empty() && !row.guidText.empty();
    }

    [[nodiscard]] std::string joinChangedSettingKeys(std::span<const std::string> keys) {
        std::string joined;
        for (const std::string& key : keys) {
            if (!joined.empty()) {
                joined += ", ";
            }
            joined += key;
        }
        return joined;
    }

    [[nodiscard]] asharia::editor::EditorIconDescriptor
    localizedIconDescriptor(const asharia::editor::EditorI18n& i18n,
                            asharia::editor::EditorIconDescriptor descriptor) {
        if (!descriptor.tooltipKey.empty()) {
            descriptor.tooltipFallback =
                textValue(i18n, descriptor.tooltipKey, descriptor.tooltipFallback);
        }
        return descriptor;
    }

    [[nodiscard]] std::string countLabel(const asharia::editor::EditorI18n& i18n,
                                         std::string_view key, std::string_view fallback,
                                         std::size_t count) {
        return textValue(i18n, key, fallback) + ": " + std::to_string(count);
    }

    [[nodiscard]] std::string visibleCountLabel(const asharia::editor::EditorI18n& i18n,
                                                const AssetBrowserSummary& summary) {
        return textValue(i18n, "assetBrowser.summary.visible", "Visible") + ": " +
               std::to_string(summary.visibleRows) + "/" + std::to_string(summary.totalRows);
    }

    [[nodiscard]] float statusPillWidth(std::string_view label) {
        return ImGui::CalcTextSize(label.data(), label.data() + label.size()).x + 16.0F;
    }

    void drawWrappedStatusPill(std::string_view label, asharia::editor::EditorUiTone tone,
                               bool& firstPill) {
        if (!firstPill) {
            const float requiredWidth = statusPillWidth(label) + ImGui::GetStyle().ItemSpacing.x;
            if (requiredWidth <= ImGui::GetContentRegionAvail().x) {
                ImGui::SameLine();
            }
        }
        asharia::editor::drawEditorUiStatusPill(label, tone);
        firstPill = false;
    }

    void drawAssetRowStatePills(const asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                                const asharia::asset::AssetCatalogViewEntry& row) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const std::string productStateLabel{i18n.text(asharia::editor::EditorI18nTextQuery{
            .key = stateTextKey(row.productState),
            .fallback = stateFallback(row.productState),
        })};
        const asharia::editor::EditorAssetPendingReimport* pending =
            findPendingReimportForRow(context, row);
        const std::string pendingLabel =
            textValue(i18n, "assetBrowser.state.pendingReimport", "pending");
        const float contentWidth = ImGui::GetContentRegionAvail().x;
        const float combinedWidth = statusPillWidth(productStateLabel) +
                                    ImGui::GetStyle().ItemSpacing.x +
                                    (pending == nullptr ? 0.0F : statusPillWidth(pendingLabel));

        asharia::editor::drawEditorUiStatusPill(productStateLabel, toneForState(row.productState));
        if (pending == nullptr) {
            return;
        }

        if (combinedWidth <= contentWidth) {
            ImGui::SameLine();
        }
        asharia::editor::drawEditorUiStatusPill(pendingLabel,
                                                asharia::editor::EditorUiTone::Warning);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            text(textValue(i18n, "assetBrowser.importSettings.pending", "Pending reimport"));
            text(countLabel(i18n, "assetBrowser.importSettings.requests", "Requests",
                            pending->requestCount));
            if (!pending->request.targetProfile.empty()) {
                text(pending->request.targetProfile);
            }
            const std::string changedSettings =
                joinChangedSettingKeys(pending->request.changedSettingKeys);
            if (!changedSettings.empty()) {
                text(changedSettings);
            }
            ImGui::EndTooltip();
        }
    }

    void drawAssetRowIcon(const asharia::editor::EditorAssetIconRegistry& icons,
                          const asharia::editor::EditorI18n& i18n,
                          const asharia::asset::AssetCatalogViewEntry& row) {
        const asharia::editor::EditorIconDescriptor descriptor = localizedIconDescriptor(
            i18n, icons.resolveAssetIcon(asharia::editor::makeEditorAssetIconQuery(row)));
        asharia::editor::drawEditorIconGlyph(descriptor, 16.0F);
    }

    [[nodiscard]] asharia::editor::EditorAssetIconQuery
    subAssetIconQuery(const asharia::asset::AssetCatalogViewEntry& row,
                      const asharia::asset::AssetCatalogSubAssetViewEntry& subAsset) {
        asharia::editor::EditorAssetIconQuery query =
            asharia::editor::makeEditorAssetIconQuery(row);
        query.displayName = subAsset.displayName;
        query.assetRole = subAsset.assetRoleName;
        query.subAssetCount = 0U;
        return query;
    }

    void drawSubAssetIcon(const asharia::editor::EditorAssetIconRegistry& icons,
                          const asharia::editor::EditorI18n& i18n,
                          const asharia::asset::AssetCatalogViewEntry& row,
                          const asharia::asset::AssetCatalogSubAssetViewEntry& subAsset) {
        const asharia::editor::EditorIconDescriptor descriptor =
            localizedIconDescriptor(i18n, icons.resolveAssetIcon(subAssetIconQuery(row, subAsset)));
        asharia::editor::drawEditorIconGlyph(descriptor, 16.0F);
    }

    void drawFolderIcon(const asharia::editor::EditorAssetIconRegistry& icons,
                        const asharia::editor::EditorI18n& i18n) {
        asharia::editor::EditorAssetIconQuery query{};
        query.folder = true;
        const asharia::editor::EditorIconDescriptor descriptor =
            localizedIconDescriptor(i18n, icons.resolveAssetIcon(query));
        asharia::editor::drawEditorIconGlyph(descriptor, 16.0F);
    }

    [[nodiscard]] asharia::editor::EditorAssetIconQuery
    navigationNodeIconQuery(const asharia::editor::EditorAssetCatalogNavigationNode& node) {
        const bool folderNode =
            node.kind == asharia::editor::EditorAssetCatalogNavigationNodeKind::SourceRoot ||
            node.kind == asharia::editor::EditorAssetCatalogNavigationNodeKind::Folder;
        return asharia::editor::EditorAssetIconQuery{
            .folder = folderNode,
            .assetType = node.assetTypeName,
            .importerId = node.importerName,
            .extension = node.extension,
            .diagnostic =
                asharia::editor::editorAssetIconDiagnosticStateForProductState(node.productState),
            .sourcePath = node.sourcePath.empty() ? node.scopePath : node.sourcePath,
            .displayName = node.displayName,
            .guidText = node.guidText,
            .importProfile = node.importProfileName,
            .assetRole = node.assetRoleName,
            .subAssetCount = node.subAssetCount,
        };
    }

    void drawNavigationNodeIcon(const asharia::editor::EditorAssetIconRegistry& icons,
                                const asharia::editor::EditorI18n& i18n,
                                const asharia::editor::EditorAssetCatalogNavigationNode& node) {
        const asharia::editor::EditorIconDescriptor descriptor =
            localizedIconDescriptor(i18n, icons.resolveAssetIcon(navigationNodeIconQuery(node)));
        asharia::editor::drawEditorIconGlyph(descriptor, 16.0F);
    }

    void drawAssetBrowserSummary(const asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                                 AssetBrowserVisibilityQuery query) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const AssetBrowserSummary summary = summarizeVisibleRows(context.catalogView, query);
        std::size_t pendingRows = 0U;
        for (const asharia::asset::AssetCatalogViewEntry& row : context.catalogView.entries) {
            if (rowMatchesVisibility(row, query) &&
                findPendingReimportForRow(context, row) != nullptr) {
                ++pendingRows;
            }
        }
        bool firstPill = true;

        const std::string visible = visibleCountLabel(i18n, summary);
        drawWrappedStatusPill(visible, asharia::editor::EditorUiTone::Muted, firstPill);

        const std::string ready =
            countLabel(i18n, "assetBrowser.summary.ready", "Ready", summary.readyRows);
        drawWrappedStatusPill(ready, asharia::editor::EditorUiTone::Success, firstPill);

        const std::string missing =
            countLabel(i18n, "assetBrowser.summary.missing", "Missing", summary.missingRows);
        drawWrappedStatusPill(missing, asharia::editor::EditorUiTone::Warning, firstPill);

        const std::string stale =
            countLabel(i18n, "assetBrowser.summary.stale", "Stale", summary.staleRows);
        drawWrappedStatusPill(stale, asharia::editor::EditorUiTone::Warning, firstPill);

        const std::string invalid =
            countLabel(i18n, "assetBrowser.summary.invalid", "Invalid", summary.invalidRows);
        drawWrappedStatusPill(invalid, asharia::editor::EditorUiTone::Danger, firstPill);

        if (summary.notTrackedRows != 0U) {
            const std::string notTracked = countLabel(i18n, "assetBrowser.summary.notTracked",
                                                      "Not tracked", summary.notTrackedRows);
            drawWrappedStatusPill(notTracked, asharia::editor::EditorUiTone::Info, firstPill);
        }

        if (summary.rowDiagnostics != 0U || !context.catalogDiagnostics.empty()) {
            const std::string diagnostics =
                countLabel(i18n, "assetBrowser.summary.diagnostics", "Diagnostics",
                           summary.rowDiagnostics + context.catalogDiagnostics.size());
            drawWrappedStatusPill(diagnostics, asharia::editor::EditorUiTone::Info, firstPill);
        }

        if (pendingRows != 0U) {
            const std::string pending = countLabel(i18n, "assetBrowser.summary.pendingReimport",
                                                   "Pending reimport", pendingRows);
            drawWrappedStatusPill(pending, asharia::editor::EditorUiTone::Warning, firstPill);
        }
    }

    void drawAssetBrowserCatalogSource(
        const asharia::editor::EditorAssetBrowserPanelDrawContext& context) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const std::string header = textValue(i18n, "assetBrowser.catalogSource", "Catalog Source");
        asharia::editor::drawEditorUiSectionHeader(header);

        if (context.catalogSnapshot == nullptr) {
            asharia::editor::drawEditorUiStatusPill(
                textValue(i18n, "assetBrowser.catalogSource.fixture", "Fixture catalog"),
                asharia::editor::EditorUiTone::Muted);
            return;
        }

        const asharia::editor::EditorAssetCatalogSnapshot& snapshot = *context.catalogSnapshot;
        asharia::editor::drawEditorUiStatusPill(
            textValue(i18n, "assetBrowser.catalogSource.project", "Project snapshot"),
            asharia::editor::EditorUiTone::Info);
        ImGui::SameLine();
        asharia::editor::drawEditorUiStatusPill(
            snapshot.productManifestFile.empty()
                ? textValue(i18n, "assetBrowser.catalogSource.noManifest", "No product manifest")
                : textValue(i18n, "assetBrowser.catalogSource.manifest", "Product manifest"),
            snapshot.productManifestFile.empty() ? asharia::editor::EditorUiTone::Warning
                                                 : asharia::editor::EditorUiTone::Success);

        const std::string projectNameLabel =
            textValue(i18n, "assetBrowser.catalogSource.projectName", "Project");
        const std::string projectFileLabel =
            textValue(i18n, "assetBrowser.catalogSource.projectFile", "Project File");
        const std::string manifestLabel =
            textValue(i18n, "assetBrowser.catalogSource.productManifest", "Product Manifest");
        const std::string targetProfileLabel =
            textValue(i18n, "assetBrowser.catalogSource.targetProfile", "Target Profile");
        const std::string sourceRootsLabel =
            textValue(i18n, "assetBrowser.catalogSource.sourceRoots", "Source Roots");
        const std::string noManifestValue =
            textValue(i18n, "assetBrowser.catalogSource.none", "None");
        const std::string projectFileValue = pathUtf8String(snapshot.projectFile);
        const std::string productManifestValue = snapshot.productManifestFile.empty()
                                                     ? noManifestValue
                                                     : pathUtf8String(snapshot.productManifestFile);
        const std::string sourceRootsValue =
            std::to_string(snapshot.project.assetSourceRoots.size());

        if (asharia::editor::beginEditorUiPropertyTable("asset-browser-catalog-source", 128.0F)) {
            asharia::editor::drawEditorUiProperty(asharia::editor::EditorUiProperty{
                .label = std::string_view{projectNameLabel},
                .value = textOrDash(snapshot.project.projectName)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{projectFileLabel},
                                                  .value = textOrDash(projectFileValue)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{manifestLabel},
                                                  .value = std::string_view{productManifestValue}});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{targetProfileLabel},
                                                  .value = textOrDash(snapshot.targetProfile)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{sourceRootsLabel},
                                                  .value = std::string_view{sourceRootsValue}});
            asharia::editor::endEditorUiPropertyTable();
        }

        const std::vector<asharia::editor::EditorAssetCatalogResolvedSourceRoot> sourceRoots =
            asharia::editor::resolveEditorAssetCatalogSourceRoots(snapshot);
        if (sourceRoots.empty()) {
            return;
        }

        const std::string sourceRootNameColumn =
            textValue(i18n, "assetBrowser.catalogSource.rootName", "Root");
        const std::string sourceRootPrefixColumn =
            textValue(i18n, "assetBrowser.catalogSource.rootPrefix", "Prefix");
        const std::string sourceRootDirectoryColumn =
            textValue(i18n, "assetBrowser.catalogSource.rootDirectory", "Directory");
        if (!ImGui::BeginTable("asset-browser-source-roots", 3,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                   ImGuiTableFlags_SizingStretchProp)) {
            return;
        }
        ImGui::TableSetupColumn(sourceRootNameColumn.c_str(), ImGuiTableColumnFlags_WidthStretch,
                                0.24F, ImGui::GetID("asset-browser-source-root-name-column"));
        ImGui::TableSetupColumn(sourceRootPrefixColumn.c_str(), ImGuiTableColumnFlags_WidthStretch,
                                0.24F, ImGui::GetID("asset-browser-source-root-prefix-column"));
        ImGui::TableSetupColumn(sourceRootDirectoryColumn.c_str(),
                                ImGuiTableColumnFlags_WidthStretch, 0.52F,
                                ImGui::GetID("asset-browser-source-root-directory-column"));
        ImGui::TableHeadersRow();

        for (const asharia::editor::EditorAssetCatalogResolvedSourceRoot& root : sourceRoots) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            drawFolderIcon(context.icons, i18n);
            ImGui::SameLine();
            text(root.rootName.empty() ? "-" : root.rootName);
            ImGui::TableSetColumnIndex(1);
            text(root.sourcePathPrefix.empty() ? "-" : root.sourcePathPrefix);
            ImGui::TableSetColumnIndex(2);
            const std::string resolvedDirectory = pathUtf8String(root.resolvedDirectory);
            text(resolvedDirectory.empty() ? "-" : resolvedDirectory);
            if (ImGui::IsItemHovered() && !root.directory.empty()) {
                ImGui::BeginTooltip();
                text(pathUtf8String(root.directory));
                ImGui::EndTooltip();
            }
        }

        ImGui::EndTable();
    }

    void selectFolderScope(AssetBrowserFolderScopeState state, std::string_view folderScope) {
        if (folderScope.empty()) {
            state.selectedFolderScope->clear();
        } else {
            state.selectedFolderScope->assign(folderScope.data(), folderScope.size());
        }
        state.selectedAssetTypeFilter->clear();
        state.selectedImportProfileFilter->clear();
        state.selectedProductStateFilter->clear();
        state.selectedAssetKey->clear();
        if (state.selectedSubAssetStableId != nullptr) {
            state.selectedSubAssetStableId->clear();
        }
    }

    [[nodiscard]] std::string_view assetSelectionKeyForNavigationNode(
        const asharia::editor::EditorAssetCatalogNavigationNode& node) {
        if (!node.guidText.empty()) {
            return node.guidText;
        }
        if (!node.sourcePath.empty()) {
            return node.sourcePath;
        }
        return node.displayName;
    }

    void selectNavigationNode(AssetBrowserNavigationTreeState state,
                              const asharia::editor::EditorAssetCatalogNavigationNode& node) {
        state.selectedNavigationKey->assign(node.key);

        switch (node.kind) {
        case asharia::editor::EditorAssetCatalogNavigationNodeKind::SourceRoot:
        case asharia::editor::EditorAssetCatalogNavigationNodeKind::Folder:
            selectFolderScope(
                AssetBrowserFolderScopeState{
                    .selectedFolderScope = state.selectedFolderScope,
                    .selectedAssetTypeFilter = state.selectedAssetTypeFilter,
                    .selectedImportProfileFilter = state.selectedImportProfileFilter,
                    .selectedProductStateFilter = state.selectedProductStateFilter,
                    .selectedAssetKey = state.selectedAssetKey,
                    .selectedSubAssetStableId = state.selectedSubAssetStableId,
                },
                node.scopePath);
            return;
        case asharia::editor::EditorAssetCatalogNavigationNodeKind::Asset:
        case asharia::editor::EditorAssetCatalogNavigationNodeKind::SubAsset: {
            const std::string_view folderScope = folderScopeForSourcePath(node.sourcePath);
            state.selectedFolderScope->assign(folderScope.data(), folderScope.size());
            state.selectedAssetTypeFilter->clear();
            state.selectedImportProfileFilter->clear();
            state.selectedProductStateFilter->clear();
            const std::string_view assetSelectionKey = assetSelectionKeyForNavigationNode(node);
            state.selectedAssetKey->assign(assetSelectionKey.data(), assetSelectionKey.size());
            if (state.selectedSubAssetStableId == nullptr) {
                return;
            }
            if (node.kind == asharia::editor::EditorAssetCatalogNavigationNodeKind::SubAsset) {
                state.selectedSubAssetStableId->assign(node.stableId);
            } else {
                state.selectedSubAssetStableId->clear();
            }
            return;
        }
        }
    }

    [[nodiscard]] bool navigationNodeHasChildren(
        std::span<const asharia::editor::EditorAssetCatalogNavigationNode> nodes,
        std::string_view parentKey) {
        return std::ranges::any_of(
            nodes, [parentKey](const asharia::editor::EditorAssetCatalogNavigationNode& candidate) {
                return candidate.parentKey == parentKey;
            });
    }

    void
    drawNavigationTreeNode(const asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                           std::span<const asharia::editor::EditorAssetCatalogNavigationNode> nodes,
                           const asharia::editor::EditorAssetCatalogNavigationNode& node,
                           AssetBrowserNavigationTreeState state) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const bool hasChildren = navigationNodeHasChildren(nodes, node.key);
        const bool selected = *state.selectedNavigationKey == node.key;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasChildren) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        ImGui::PushID(node.key.c_str());
        const bool open = ImGui::TreeNodeEx("##asset-browser-navigation-node", flags);
        ImGui::SameLine();
        drawNavigationNodeIcon(context.icons, i18n, node);
        ImGui::SameLine();
        std::string label = node.displayName.empty() ? node.key : node.displayName;
        label += "###asset-browser-navigation-label";
        if (ImGui::Selectable(label.c_str(), selected)) {
            selectNavigationNode(state, node);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            text(node.sourcePath.empty() ? node.scopePath : node.sourcePath);
            if (!node.stableId.empty()) {
                text(node.stableId);
            }
            ImGui::EndTooltip();
        }

        if (hasChildren && open) {
            for (const asharia::editor::EditorAssetCatalogNavigationNode& child : nodes) {
                if (child.parentKey == node.key) {
                    drawNavigationTreeNode(context, nodes, child, state);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void
    drawNavigationTreeControls(const asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                               AssetBrowserNavigationTreeState state) {
        if (context.catalogSnapshot == nullptr) {
            return;
        }

        const std::vector<asharia::editor::EditorAssetCatalogNavigationNode> nodes =
            asharia::editor::makeEditorAssetCatalogNavigationNodes(*context.catalogSnapshot);
        if (nodes.empty()) {
            state.selectedNavigationKey->clear();
            return;
        }

        if (!state.selectedNavigationKey->empty() &&
            !std::ranges::any_of(
                nodes, [state](const asharia::editor::EditorAssetCatalogNavigationNode& node) {
                    return node.key == *state.selectedNavigationKey;
                })) {
            state.selectedNavigationKey->clear();
            if (state.selectedSubAssetStableId != nullptr) {
                state.selectedSubAssetStableId->clear();
            }
        }

        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const std::string header = textValue(i18n, "assetBrowser.navigation", "Navigation");
        asharia::editor::drawEditorUiSectionHeader(header);

        const std::string allLabel = textValue(i18n, "assetBrowser.scope.all", "All");
        std::string allButtonLabel = allLabel;
        allButtonLabel += "###asset-browser-navigation-all";
        if (ImGui::SmallButton(allButtonLabel.c_str())) {
            selectFolderScope(
                AssetBrowserFolderScopeState{
                    .selectedFolderScope = state.selectedFolderScope,
                    .selectedAssetTypeFilter = state.selectedAssetTypeFilter,
                    .selectedImportProfileFilter = state.selectedImportProfileFilter,
                    .selectedProductStateFilter = state.selectedProductStateFilter,
                    .selectedAssetKey = state.selectedAssetKey,
                    .selectedSubAssetStableId = state.selectedSubAssetStableId,
                },
                {});
            state.selectedNavigationKey->clear();
            if (state.selectedSubAssetStableId != nullptr) {
                state.selectedSubAssetStableId->clear();
            }
        }

        const float treeHeight =
            std::min(220.0F, std::max(120.0F, ImGui::GetTextLineHeight() * 9.0F));
        if (!ImGui::BeginChild("asset-browser-navigation-tree", ImVec2{0.0F, treeHeight}, 0,
                               ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGui::EndChild();
            return;
        }
        for (const asharia::editor::EditorAssetCatalogNavigationNode& node : nodes) {
            if (node.parentKey.empty()) {
                drawNavigationTreeNode(context, nodes, node, state);
            }
        }
        ImGui::EndChild();
    }

    void drawFolderScopeControls(const asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                                 AssetBrowserFolderScopeState state) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const std::string scopeLabel = textValue(i18n, "assetBrowser.scope", "Scope");
        const std::string allLabel = textValue(i18n, "assetBrowser.scope.all", "All");

        text(scopeLabel);
        ImGui::SameLine();
        std::string rootLabel = allLabel;
        rootLabel += "###asset-browser-folder-root";
        if (ImGui::SmallButton(rootLabel.c_str())) {
            selectFolderScope(state, {});
        }

        for (const std::string& breadcrumbScope :
             folderBreadcrumbScopes(*state.selectedFolderScope)) {
            ImGui::SameLine();
            text("/");
            ImGui::SameLine();
            std::string itemLabel{folderNameForScope(breadcrumbScope)};
            itemLabel += "###asset-browser-folder-breadcrumb-";
            itemLabel += breadcrumbScope;
            const bool current = *state.selectedFolderScope == breadcrumbScope;
            if (current) {
                ImGui::BeginDisabled();
                ImGui::SmallButton(itemLabel.c_str());
                ImGui::EndDisabled();
            } else if (ImGui::SmallButton(itemLabel.c_str())) {
                selectFolderScope(state, breadcrumbScope);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                text(breadcrumbScope);
                ImGui::EndTooltip();
            }
        }

        const std::vector<std::string> folders =
            childFolderScopes(context.catalogView, *state.selectedFolderScope);
        if (folders.empty()) {
            return;
        }

        const std::string childFoldersLabel =
            textValue(i18n, "assetBrowser.scope.childFolders", "Folders");
        asharia::editor::drawEditorUiSectionHeader(childFoldersLabel);
        for (const std::string& folderScope : folders) {
            drawFolderIcon(context.icons, i18n);
            ImGui::SameLine();
            std::string itemLabel{folderNameForScope(folderScope)};
            itemLabel += "###asset-browser-folder-";
            itemLabel += folderScope;
            if (ImGui::Selectable(itemLabel.c_str(), false)) {
                selectFolderScope(state, folderScope);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                text(folderScope);
                ImGui::EndTooltip();
            }
        }
    }

    void drawAssetTypeFilterControl(
        const asharia::editor::EditorAssetBrowserPanelDrawContext& context,
        std::string_view folderScope, std::string& selectedAssetTypeFilter,
        std::string& selectedImportProfileFilter, std::string& selectedProductStateFilter,
        std::string& selectedAssetKey, std::string& selectedSubAssetStableId) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const std::string comboLabel = label(i18n, "assetBrowser.filter.assetType",
                                             "asset-browser-asset-type-filter", "Asset Type");
        const std::string allLabel = textValue(i18n, "assetBrowser.filter.assetType.all", "All");
        const std::string allItemLabel = label(i18n, "assetBrowser.filter.assetType.all",
                                               "asset-browser-asset-type-filter-all", "All");
        const std::string currentLabel =
            selectedAssetTypeFilter.empty() ? allLabel : selectedAssetTypeFilter;
        const std::vector<std::string> filters = assetTypeFilters(context.catalogView, folderScope);

        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo(comboLabel.c_str(), currentLabel.c_str())) {
            const bool allSelected = selectedAssetTypeFilter.empty();
            if (ImGui::Selectable(allItemLabel.c_str(), allSelected)) {
                selectedAssetTypeFilter.clear();
                selectedImportProfileFilter.clear();
                selectedProductStateFilter.clear();
                selectedAssetKey.clear();
                selectedSubAssetStableId.clear();
            }
            if (allSelected) {
                ImGui::SetItemDefaultFocus();
            }

            for (const std::string& filter : filters) {
                const bool selected = selectedAssetTypeFilter == filter;
                std::string itemLabel = filter;
                itemLabel += "###asset-browser-type-filter-";
                itemLabel += filter;
                if (ImGui::Selectable(itemLabel.c_str(), selected)) {
                    selectedAssetTypeFilter = filter;
                    selectedImportProfileFilter.clear();
                    selectedProductStateFilter.clear();
                    selectedAssetKey.clear();
                    selectedSubAssetStableId.clear();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    void drawImportProfileFilterControl(
        const asharia::editor::EditorAssetBrowserPanelDrawContext& context,
        AssetBrowserImportProfileFilterQuery query, std::string& selectedImportProfileFilter,
        std::string& selectedProductStateFilter, std::string& selectedAssetKey,
        std::string& selectedSubAssetStableId) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const std::string comboLabel =
            label(i18n, "assetBrowser.filter.importProfile", "asset-browser-import-profile-filter",
                  "Import Profile");
        const std::string allLabel =
            textValue(i18n, "assetBrowser.filter.importProfile.all", "All profiles");
        const std::string allItemLabel =
            label(i18n, "assetBrowser.filter.importProfile.all",
                  "asset-browser-import-profile-filter-all", "All profiles");
        const std::string currentLabel =
            selectedImportProfileFilter.empty() ? allLabel : selectedImportProfileFilter;
        const std::vector<std::string> filters = importProfileFilters(context.catalogView, query);

        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo(comboLabel.c_str(), currentLabel.c_str())) {
            const bool allSelected = selectedImportProfileFilter.empty();
            if (ImGui::Selectable(allItemLabel.c_str(), allSelected)) {
                selectedImportProfileFilter.clear();
                selectedProductStateFilter.clear();
                selectedAssetKey.clear();
                selectedSubAssetStableId.clear();
            }
            if (allSelected) {
                ImGui::SetItemDefaultFocus();
            }

            for (const std::string& filter : filters) {
                const bool selected = selectedImportProfileFilter == filter;
                std::string itemLabel = filter;
                itemLabel += "###asset-browser-import-profile-filter-";
                itemLabel += filter;
                if (ImGui::Selectable(itemLabel.c_str(), selected)) {
                    selectedImportProfileFilter = filter;
                    selectedProductStateFilter.clear();
                    selectedAssetKey.clear();
                    selectedSubAssetStableId.clear();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    [[nodiscard]] std::string productStateFilterLabel(const asharia::editor::EditorI18n& i18n,
                                                      std::string_view productStateFilter) {
        if (productStateFilter.empty()) {
            return textValue(i18n, "assetBrowser.filter.productState.all", "All states");
        }
        for (const asharia::asset::AssetCatalogProductState state : kProductStateFilterOrder) {
            if (asharia::asset::assetCatalogProductStateName(state) == productStateFilter) {
                return textValue(i18n, stateTextKey(state), stateFallback(state));
            }
        }
        return std::string{productStateFilter};
    }

    void drawProductStateFilterControl(
        const asharia::editor::EditorAssetBrowserPanelDrawContext& context,
        AssetBrowserProductStateFilterQuery query, std::string& selectedProductStateFilter,
        std::string& selectedAssetKey, std::string& selectedSubAssetStableId) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const std::string comboLabel = label(i18n, "assetBrowser.filter.productState",
                                             "asset-browser-product-state-filter", "Product State");
        const std::string allItemLabel =
            label(i18n, "assetBrowser.filter.productState.all",
                  "asset-browser-product-state-filter-all", "All states");
        const std::string currentLabel = productStateFilterLabel(i18n, selectedProductStateFilter);
        const std::vector<asharia::asset::AssetCatalogProductState> filters =
            productStateFilters(context.catalogView, query);

        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo(comboLabel.c_str(), currentLabel.c_str())) {
            const bool allSelected = selectedProductStateFilter.empty();
            if (ImGui::Selectable(allItemLabel.c_str(), allSelected)) {
                selectedProductStateFilter.clear();
                selectedAssetKey.clear();
                selectedSubAssetStableId.clear();
            }
            if (allSelected) {
                ImGui::SetItemDefaultFocus();
            }

            for (const asharia::asset::AssetCatalogProductState state : filters) {
                const std::string_view stateName =
                    asharia::asset::assetCatalogProductStateName(state);
                const bool selected = selectedProductStateFilter == stateName;
                std::string itemLabel = textValue(i18n, stateTextKey(state), stateFallback(state));
                itemLabel += "###asset-browser-product-state-filter-";
                itemLabel.append(stateName.data(), stateName.size());
                if (ImGui::Selectable(itemLabel.c_str(), selected)) {
                    selectedProductStateFilter.assign(stateName.data(), stateName.size());
                    selectedAssetKey.clear();
                    selectedSubAssetStableId.clear();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    void
    drawCatalogDiagnostics(const asharia::editor::EditorAssetBrowserPanelDrawContext& context) {
        if (context.catalogDiagnostics.empty()) {
            return;
        }
        if (!ImGui::BeginTable("asset-browser-diagnostics", 3, kDiagnosticTableFlags)) {
            return;
        }

        ImGui::TableSetupColumn("severity", ImGuiTableColumnFlags_WidthFixed, 76.0F);
        ImGui::TableSetupColumn("code", ImGuiTableColumnFlags_WidthFixed, 156.0F);
        ImGui::TableSetupColumn("message", ImGuiTableColumnFlags_WidthStretch);

        for (const asharia::editor::EditorAssetCatalogDiagnostic& diagnostic :
             context.catalogDiagnostics) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            asharia::editor::drawEditorUiStatusPill(
                asharia::editor::editorAssetCatalogDiagnosticSeverityName(diagnostic.severity),
                toneForDiagnosticSeverity(diagnostic.severity));

            ImGui::TableSetColumnIndex(1);
            text(asharia::editor::editorAssetCatalogDiagnosticCodeName(diagnostic.code));

            ImGui::TableSetColumnIndex(2);
            text(diagnostic.message.empty() ? diagnostic.sourcePath : diagnostic.message);
            if (ImGui::IsItemHovered() &&
                (!diagnostic.sourcePath.empty() || !diagnostic.path.empty())) {
                ImGui::BeginTooltip();
                if (!diagnostic.sourcePath.empty()) {
                    text(diagnostic.sourcePath);
                }
                if (!diagnostic.path.empty()) {
                    text(pathUtf8String(diagnostic.path));
                }
                ImGui::EndTooltip();
            }
        }

        ImGui::EndTable();
        ImGui::Separator();
    }

    void drawAssetBrowserRows(const asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                              AssetBrowserVisibilityQuery query, AssetBrowserRowsState state) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        if (!ImGui::BeginTable("asset-browser-table", 5, kAssetTableFlags)) {
            return;
        }

        const std::string nameColumn = textValue(i18n, "assetBrowser.column.name", "Name");
        const std::string typeColumn = textValue(i18n, "assetBrowser.column.type", "Type");
        const std::string profileColumn =
            textValue(i18n, "assetBrowser.column.importProfile", "Profile");
        const std::string importerColumn =
            textValue(i18n, "assetBrowser.column.importer", "Importer");
        const std::string stateColumn = textValue(i18n, "assetBrowser.column.state", "State");
        ImGui::TableSetupColumn(nameColumn.c_str(),
                                ImGuiTableColumnFlags_WidthStretch |
                                    ImGuiTableColumnFlags_DefaultSort,
                                0.42F, ImGui::GetID("asset-browser-column-name"));
        ImGui::TableSetupColumn(typeColumn.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.16F,
                                ImGui::GetID("asset-browser-column-type"));
        ImGui::TableSetupColumn(profileColumn.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.16F,
                                ImGui::GetID("asset-browser-column-import-profile"));
        ImGui::TableSetupColumn(importerColumn.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.16F,
                                ImGui::GetID("asset-browser-column-importer"));
        ImGui::TableSetupColumn(stateColumn.c_str(), ImGuiTableColumnFlags_WidthFixed, 176.0F,
                                ImGui::GetID("asset-browser-column-state"));
        ImGui::TableHeadersRow();

        ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
        const std::vector<const asharia::asset::AssetCatalogViewEntry*> rows =
            visibleSortedRows(context.catalogView, query, sortSpecs);
        if (sortSpecs != nullptr) {
            sortSpecs->SpecsDirty = false;
        }
        for (const asharia::asset::AssetCatalogViewEntry* rowPtr : rows) {
            const asharia::asset::AssetCatalogViewEntry& row = *rowPtr;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            drawAssetRowIcon(context.icons, i18n, row);
            ImGui::SameLine();
            const std::string_view selectionKey = selectionKeyForRow(row);
            const bool selected = !state.selectedAssetKey->empty() &&
                                  std::string_view{*state.selectedAssetKey} == selectionKey;
            std::string rowLabel = row.displayName.empty() ? row.sourcePath : row.displayName;
            rowLabel += "###asset-browser-row-";
            rowLabel.append(selectionKey.data(), selectionKey.size());
            if (ImGui::Selectable(rowLabel.c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                state.selectedAssetKey->assign(selectionKey.data(), selectionKey.size());
                state.selectedNavigationKey->clear();
                state.selectedSubAssetStableId->clear();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                text(row.sourcePath);
                ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(1);
            text(row.assetTypeName);

            ImGui::TableSetColumnIndex(2);
            text(row.importProfileName.empty() ? "-" : row.importProfileName);

            ImGui::TableSetColumnIndex(3);
            text(row.importerName.empty() ? "-" : row.importerName);

            ImGui::TableSetColumnIndex(4);
            drawAssetRowStatePills(context, row);
        }

        if (rows.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            text(i18n.text(asharia::editor::EditorI18nTextQuery{
                .key = "assetBrowser.noRows",
                .fallback = "No assets",
            }));
        }

        ImGui::EndTable();
    }

    void drawSelectedAssetDiagnostics(const asharia::asset::AssetCatalogViewEntry& row) {
        if (row.diagnostics.empty()) {
            return;
        }
        if (!ImGui::BeginTable("asset-browser-selected-diagnostics", 3, kDiagnosticTableFlags)) {
            return;
        }

        ImGui::TableSetupColumn("severity", ImGuiTableColumnFlags_WidthFixed, 76.0F);
        ImGui::TableSetupColumn("code", ImGuiTableColumnFlags_WidthFixed, 156.0F);
        ImGui::TableSetupColumn("message", ImGuiTableColumnFlags_WidthStretch);

        for (const asharia::asset::AssetCatalogDiagnostic& diagnostic : row.diagnostics) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            asharia::editor::drawEditorUiStatusPill(
                assetDiagnosticSeverityName(diagnostic.severity),
                toneForAssetDiagnosticSeverity(diagnostic.severity));

            ImGui::TableSetColumnIndex(1);
            text(asharia::asset::assetCatalogDiagnosticCodeName(diagnostic.code));

            ImGui::TableSetColumnIndex(2);
            text(diagnostic.message.empty() ? diagnostic.sourcePath : diagnostic.message);
        }

        ImGui::EndTable();
    }

    void drawSelectedAssetCopyButton(const asharia::editor::EditorI18n& i18n,
                                     AssetBrowserCopyButtonQuery query) {
        const std::string tooltip = textValue(i18n, query.tooltipKey, query.tooltipFallback);
        const asharia::editor::EditorIconDescriptor icon =
            asharia::editor::makeLucideEditorIconDescriptor(
                "copy", asharia::editor::editorIconTint(0.72F, 0.76F, 0.82F), query.tooltipKey,
                tooltip);
        if (query.value.empty()) {
            ImGui::BeginDisabled();
        }
        if (asharia::editor::drawEditorIconButton(icon, query.stableId, 14.0F) &&
            !query.value.empty()) {
            const std::string clipboardValue{query.value};
            ImGui::SetClipboardText(clipboardValue.c_str());
        }
        if (query.value.empty()) {
            ImGui::EndDisabled();
        }
    }

    void
    drawSelectedAssetPendingReimport(asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                                     const asharia::asset::AssetCatalogViewEntry& row) {
        const asharia::editor::EditorAssetPendingReimport* pending =
            findPendingReimportForRow(context, row);
        if (pending == nullptr) {
            return;
        }

        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const asharia::editor::EditorAssetReimportRequest request = pending->request;
        const std::size_t requestCount = pending->requestCount;
        const asharia::asset::AssetGuid guid = parseRowGuid(row);
        const std::string targetProfile = targetProfileForSnapshot(context);
        const bool canClearPending = guid || !row.sourcePath.empty();

        const std::string pendingLabel =
            textValue(i18n, "assetBrowser.importSettings.pending", "Pending reimport") + ": " +
            std::to_string(requestCount);
        asharia::editor::drawEditorUiStatusPill(pendingLabel,
                                                asharia::editor::EditorUiTone::Warning);
        ImGui::SameLine();

        const std::string clearTooltip =
            textValue(i18n, "assetBrowser.importSettings.clearPending", "Clear pending reimport");
        const asharia::editor::EditorIconDescriptor clearIcon =
            asharia::editor::makeLucideEditorIconDescriptor(
                "x", asharia::editor::editorIconTint(0.78F, 0.80F, 0.84F),
                "assetBrowser.importSettings.clearPending", clearTooltip);
        if (!canClearPending) {
            ImGui::BeginDisabled();
        }
        if (asharia::editor::drawEditorIconButton(
                clearIcon, "asset-browser-import-settings-clear-pending", 14.0F) &&
            canClearPending) {
            static_cast<void>(context.pendingReimports.clearForSource(
                asharia::editor::EditorAssetReimportSourceKey{
                    .guid = guid,
                    .sourcePath = row.sourcePath,
                    .targetProfile = targetProfile,
                }));
        }
        if (!canClearPending) {
            ImGui::EndDisabled();
        }

        const std::string targetProfileLabel =
            textValue(i18n, "assetBrowser.catalogSource.targetProfile", "Target Profile");
        const std::string changedSettingsLabel =
            textValue(i18n, "assetBrowser.importSettings.changedSettings", "Changed Settings");
        const std::string metadataFileLabel =
            textValue(i18n, "assetBrowser.detail.metadataFile", "Metadata File");
        const std::string changedSettingsValue = joinChangedSettingKeys(request.changedSettingKeys);
        const std::string metadataFileValue = pathUtf8String(request.metadataFile);
        if (asharia::editor::beginEditorUiPropertyTable("asset-browser-selected-pending-reimport",
                                                        144.0F)) {
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{targetProfileLabel},
                                                  .value = textOrDash(request.targetProfile)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{changedSettingsLabel},
                                                  .value = textOrDash(changedSettingsValue)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{metadataFileLabel},
                                                  .value = textOrDash(metadataFileValue)});
            asharia::editor::endEditorUiPropertyTable();
        }
    }

    [[nodiscard]] bool validImportSettingsUiState(AssetBrowserImportSettingsUiState state) {
        return state.selectionKey != nullptr && state.textureProfileDraft != nullptr &&
               state.textureProfileBaseline != nullptr && state.message != nullptr &&
               state.messageIsError != nullptr;
    }

    [[nodiscard]] std::string
    currentTextureProfileForImportSettings(const asharia::asset::AssetCatalogViewEntry& row,
                                           const std::filesystem::path& metadataFile) {
        if (!metadataFile.empty()) {
            auto profile = asharia::editor::readEditorTextureProfileSetting(metadataFile);
            if (profile) {
                return std::move(*profile);
            }
        }
        return row.importProfileName;
    }

    void syncTextureProfileDraftState(const asharia::asset::AssetCatalogViewEntry& row,
                                      const std::filesystem::path& metadataFile,
                                      AssetBrowserImportSettingsUiState state) {
        const std::string_view selectionKey = selectionKeyForRow(row);
        const std::string currentProfile =
            currentTextureProfileForImportSettings(row, metadataFile);
        if (*state.selectionKey != selectionKey) {
            state.selectionKey->assign(selectionKey.data(), selectionKey.size());
            state.textureProfileDraft->assign(currentProfile);
            state.textureProfileBaseline->assign(currentProfile);
            state.message->clear();
            *state.messageIsError = false;
            return;
        }

        const bool userIsNotEditing = *state.textureProfileDraft == *state.textureProfileBaseline;
        if (userIsNotEditing && *state.textureProfileBaseline != currentProfile) {
            state.textureProfileDraft->assign(currentProfile);
            state.textureProfileBaseline->assign(currentProfile);
        }
    }

    void drawTextureProfileCombo(const asharia::editor::EditorI18n& i18n,
                                 AssetBrowserImportSettingsUiState state) {
        const std::string profileLabel = label(i18n, "assetBrowser.importSettings.profile",
                                               "asset-browser-import-settings-profile", "Profile");
        const std::string currentProfileLabel =
            textureProfileLabel(i18n, *state.textureProfileDraft);
        constexpr float kApplyButtonWidth = 32.0F;
        const float comboWidth =
            std::max(140.0F, ImGui::GetContentRegionAvail().x - kApplyButtonWidth -
                                 ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetNextItemWidth(comboWidth);
        if (!ImGui::BeginCombo(profileLabel.c_str(), currentProfileLabel.c_str())) {
            return;
        }

        for (const AssetBrowserTextureProfileOption& option : kTextureProfileOptions) {
            const bool selected = *state.textureProfileDraft == option.value;
            const std::string optionLabel = textureProfileLabel(i18n, option.value);
            if (ImGui::Selectable(optionLabel.c_str(), selected)) {
                state.textureProfileDraft->assign(option.value.data(), option.value.size());
                state.message->clear();
                *state.messageIsError = false;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    void applyTextureProfileDraft(asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                                  const std::filesystem::path& metadataFile,
                                  AssetBrowserImportSettingsUiState state) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const std::size_t oldRequestCount = context.reimportRequests.size();
        auto command = asharia::editor::makeEditorTextureProfileEditCommand(
            metadataFile, targetProfileForSnapshot(context), *state.textureProfileDraft,
            context.reimportRequests);
        if (!command) {
            *state.message = command.error().message;
            *state.messageIsError = true;
            return;
        }

        asharia::editor::EditorTransaction transaction;
        transaction.addCommand(std::move(*command));
        if (auto executed = transaction.executeAll(); !executed) {
            *state.message = executed.error().message;
            *state.messageIsError = true;
            return;
        }

        const std::size_t recordedRequests = recordNewPendingRequests(context, oldRequestCount);
        if (auto currentProfile = asharia::editor::readEditorTextureProfileSetting(metadataFile)) {
            state.textureProfileBaseline->assign(*currentProfile);
            state.textureProfileDraft->assign(*currentProfile);
        } else {
            state.textureProfileBaseline->assign(*state.textureProfileDraft);
        }
        if (recordedRequests != 0U) {
            context.commandHistory.push(std::move(transaction));
            *state.message =
                textValue(i18n, "assetBrowser.importSettings.applied", "Applied; pending reimport");
        } else {
            *state.message = textValue(i18n, "assetBrowser.importSettings.noChange", "No change");
        }
        *state.messageIsError = false;
    }

    void drawTextureProfileApplyButton(asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                                       const std::filesystem::path& metadataFile,
                                       AssetBrowserImportSettingsUiState state) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const bool canApply = !metadataFile.empty() && !state.textureProfileDraft->empty() &&
                              *state.textureProfileDraft != *state.textureProfileBaseline;
        const std::string applyTooltip =
            textValue(i18n, "assetBrowser.importSettings.apply", "Apply import settings");
        const asharia::editor::EditorIconDescriptor applyIcon =
            asharia::editor::makeLucideEditorIconDescriptor(
                "check", asharia::editor::editorIconTint(0.70F, 0.86F, 0.68F),
                "assetBrowser.importSettings.apply", applyTooltip);
        if (!canApply) {
            ImGui::BeginDisabled();
        }
        if (asharia::editor::drawEditorIconButton(applyIcon, "asset-browser-import-settings-apply",
                                                  14.0F) &&
            canApply) {
            applyTextureProfileDraft(context, metadataFile, state);
        }
        if (!canApply) {
            ImGui::EndDisabled();
        }
    }

    void drawImportSettingsMessage(AssetBrowserImportSettingsUiState state) {
        if (state.message->empty()) {
            return;
        }

        asharia::editor::drawEditorUiStatusPill(
            *state.message, *state.messageIsError ? asharia::editor::EditorUiTone::Danger
                                                  : asharia::editor::EditorUiTone::Info);
    }

    void
    drawSelectedAssetImportSettings(asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                                    const asharia::asset::AssetCatalogViewEntry& row,
                                    const std::filesystem::path& metadataFile,
                                    AssetBrowserImportSettingsUiState state) {
        if (!isTextureImportSettingsRow(row) || context.catalogSnapshot == nullptr ||
            !validImportSettingsUiState(state)) {
            return;
        }

        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        syncTextureProfileDraftState(row, metadataFile, state);

        const std::string header =
            textValue(i18n, "assetBrowser.importSettings", "Import Settings");
        asharia::editor::drawEditorUiSectionHeader(header);
        drawSelectedAssetPendingReimport(context, row);

        drawTextureProfileCombo(i18n, state);
        ImGui::SameLine();
        drawTextureProfileApplyButton(context, metadataFile, state);
        drawImportSettingsMessage(state);
    }

    void
    drawSelectedSubAssetDetails(const asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                                const asharia::asset::AssetCatalogViewEntry& row,
                                const asharia::asset::AssetCatalogSubAssetViewEntry* subAsset) {
        if (subAsset == nullptr) {
            return;
        }

        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const std::string header =
            textValue(i18n, "assetBrowser.detail.selectedSubAsset", "Selected Sub-asset");
        asharia::editor::drawEditorUiSectionHeader(header);
        drawSubAssetIcon(context.icons, i18n, row, *subAsset);
        ImGui::SameLine();
        text(subAsset->displayName.empty() ? subAsset->stableId : subAsset->displayName);
        ImGui::Spacing();
        drawSelectedAssetCopyButton(i18n, AssetBrowserCopyButtonQuery{
                                              .value = subAsset->stableId,
                                              .stableId = "asset-browser-copy-selected-subasset-id",
                                              .tooltipKey = "assetBrowser.copy.subAssetStableId",
                                              .tooltipFallback = "Copy sub-asset ID",
                                          });

        const std::string stableIdLabel = textValue(i18n, "assetBrowser.detail.subAssetId", "ID");
        const std::string nameLabel = textValue(i18n, "assetBrowser.detail.subAssetName", "Name");
        const std::string roleLabel = textValue(i18n, "assetBrowser.detail.subAssetRole", "Role");
        const std::string parentSourcePathLabel =
            textValue(i18n, "assetBrowser.detail.subAssetParentSourcePath", "Parent Source Path");
        const std::string parentGuidLabel =
            textValue(i18n, "assetBrowser.detail.subAssetParentGuid", "Parent GUID");
        if (asharia::editor::beginEditorUiPropertyTable("asset-browser-selected-subasset-details",
                                                        144.0F)) {
            asharia::editor::drawEditorUiProperty(asharia::editor::EditorUiProperty{
                .label = std::string_view{stableIdLabel}, .value = textOrDash(subAsset->stableId)});
            asharia::editor::drawEditorUiProperty(asharia::editor::EditorUiProperty{
                .label = std::string_view{nameLabel}, .value = textOrDash(subAsset->displayName)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{roleLabel},
                                                  .value = textOrDash(subAsset->assetRoleName)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{parentSourcePathLabel},
                                                  .value = textOrDash(row.sourcePath)});
            asharia::editor::drawEditorUiProperty(asharia::editor::EditorUiProperty{
                .label = std::string_view{parentGuidLabel}, .value = textOrDash(row.guidText)});
            asharia::editor::endEditorUiPropertyTable();
        }
    }

    void drawSelectedAssetSubAssets(const asharia::editor::EditorI18n& i18n,
                                    const asharia::asset::AssetCatalogViewEntry& row,
                                    std::string& selectedSubAssetStableId) {
        if (row.subAssets.empty()) {
            return;
        }

        const std::string header = textValue(i18n, "assetBrowser.detail.subAssets", "Sub-assets");
        asharia::editor::drawEditorUiSectionHeader(header);
        if (!ImGui::BeginTable("asset-browser-sub-assets", 4,
                               ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_SizingStretchProp)) {
            return;
        }

        const std::string idColumn = textValue(i18n, "assetBrowser.detail.subAssetId", "ID");
        const std::string nameColumn = textValue(i18n, "assetBrowser.detail.subAssetName", "Name");
        const std::string roleColumn = textValue(i18n, "assetBrowser.detail.subAssetRole", "Role");
        const std::string actionColumn = textValue(i18n, "assetBrowser.detail.subAssetActions", "");
        ImGui::TableSetupColumn(idColumn.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.32F,
                                ImGui::GetID("asset-browser-subasset-column-id"));
        ImGui::TableSetupColumn(nameColumn.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.32F,
                                ImGui::GetID("asset-browser-subasset-column-name"));
        ImGui::TableSetupColumn(roleColumn.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.28F,
                                ImGui::GetID("asset-browser-subasset-column-role"));
        ImGui::TableSetupColumn(actionColumn.c_str(), ImGuiTableColumnFlags_WidthFixed, 32.0F,
                                ImGui::GetID("asset-browser-subasset-column-actions"));
        ImGui::TableHeadersRow();

        std::size_t subAssetIndex = 0U;
        for (const asharia::asset::AssetCatalogSubAssetViewEntry& subAsset : row.subAssets) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool selected =
                !selectedSubAssetStableId.empty() && selectedSubAssetStableId == subAsset.stableId;
            std::string stableIdLabel = subAsset.stableId.empty() ? "-" : subAsset.stableId;
            stableIdLabel += "###asset-browser-subasset-row-";
            stableIdLabel += row.guidText;
            stableIdLabel += "-";
            stableIdLabel += std::to_string(subAssetIndex);
            if (ImGui::Selectable(stableIdLabel.c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                selectedSubAssetStableId = subAsset.stableId;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                text(subAsset.stableId);
                ImGui::EndTooltip();
            }
            ImGui::TableSetColumnIndex(1);
            text(subAsset.displayName.empty() ? "-" : subAsset.displayName);
            ImGui::TableSetColumnIndex(2);
            text(subAsset.assetRoleName.empty() ? "-" : subAsset.assetRoleName);
            ImGui::TableSetColumnIndex(3);
            std::string copyStableId{"asset-browser-copy-subasset-stable-id-"};
            copyStableId += row.guidText;
            copyStableId += "-";
            copyStableId += std::to_string(subAssetIndex);
            drawSelectedAssetCopyButton(i18n,
                                        AssetBrowserCopyButtonQuery{
                                            .value = subAsset.stableId,
                                            .stableId = copyStableId,
                                            .tooltipKey = "assetBrowser.copy.subAssetStableId",
                                            .tooltipFallback = "Copy sub-asset ID",
                                        });
            ++subAssetIndex;
        }

        ImGui::EndTable();
    }

    void drawSelectedAssetDetails(asharia::editor::EditorAssetBrowserPanelDrawContext& context,
                                  const asharia::asset::AssetCatalogViewEntry* selectedRow,
                                  std::string& selectedSubAssetStableId,
                                  AssetBrowserImportSettingsUiState importSettingsState) {
        const asharia::editor::EditorI18n& i18n = context.ui.i18n;
        const std::string detailsHeader = textValue(i18n, "assetBrowser.details", "Selected Asset");
        asharia::editor::drawEditorUiSectionHeader(detailsHeader);

        if (selectedRow == nullptr) {
            text(i18n.text(asharia::editor::EditorI18nTextQuery{
                .key = "assetBrowser.noSelection",
                .fallback = "No asset selected",
            }));
            return;
        }

        const asharia::asset::AssetCatalogSubAssetViewEntry* selectedSubAsset =
            findSubAssetByStableId(*selectedRow, selectedSubAssetStableId);

        drawAssetRowIcon(context.icons, i18n, *selectedRow);
        ImGui::SameLine();
        text(selectedRow->displayName.empty() ? selectedRow->sourcePath : selectedRow->displayName);
        ImGui::SameLine();
        asharia::editor::drawEditorUiStatusPill(
            i18n.text(asharia::editor::EditorI18nTextQuery{
                .key = stateTextKey(selectedRow->productState),
                .fallback = stateFallback(selectedRow->productState),
            }),
            toneForState(selectedRow->productState));
        ImGui::Spacing();
        drawSelectedAssetCopyButton(i18n, AssetBrowserCopyButtonQuery{
                                              .value = selectedRow->guidText,
                                              .stableId = "asset-browser-copy-guid",
                                              .tooltipKey = "assetBrowser.copy.guid",
                                              .tooltipFallback = "Copy GUID",
                                          });
        ImGui::SameLine();
        drawSelectedAssetCopyButton(i18n, AssetBrowserCopyButtonQuery{
                                              .value = selectedRow->sourcePath,
                                              .stableId = "asset-browser-copy-source-path",
                                              .tooltipKey = "assetBrowser.copy.sourcePath",
                                              .tooltipFallback = "Copy source path",
                                          });
        const bool hasSnapshot = context.catalogSnapshot != nullptr;
        const std::filesystem::path sourceFile =
            hasSnapshot ? asharia::editor::resolveEditorAssetCatalogSourceFilePath(
                              *context.catalogSnapshot, selectedRow->sourcePath)
                        : std::filesystem::path{};
        const std::filesystem::path metadataFile =
            hasSnapshot ? asharia::editor::resolveEditorAssetCatalogMetadataFilePath(
                              *context.catalogSnapshot, selectedRow->sourcePath)
                        : std::filesystem::path{};
        const std::string sourceFileValue =
            hasSnapshot ? pathUtf8String(sourceFile) : std::string{};
        const std::string metadataFileValue =
            hasSnapshot ? pathUtf8String(metadataFile) : std::string{};
        const asharia::editor::EditorAssetCatalogResolvedSourceRoot sourceRoot =
            hasSnapshot ? asharia::editor::resolveEditorAssetCatalogSourceRootForSourcePath(
                              *context.catalogSnapshot, selectedRow->sourcePath)
                        : asharia::editor::EditorAssetCatalogResolvedSourceRoot{};
        const std::string sourceRootValue =
            sourceRoot.rootName.empty() ? sourceRoot.sourcePathPrefix : sourceRoot.rootName;
        const std::string sourceRootDirectoryValue = pathUtf8String(sourceRoot.resolvedDirectory);
        if (hasSnapshot) {
            ImGui::SameLine();
            drawSelectedAssetCopyButton(i18n, AssetBrowserCopyButtonQuery{
                                                  .value = sourceFileValue,
                                                  .stableId = "asset-browser-copy-source-file",
                                                  .tooltipKey = "assetBrowser.copy.sourceFile",
                                                  .tooltipFallback = "Copy source file path",
                                              });
            ImGui::SameLine();
            drawSelectedAssetCopyButton(i18n, AssetBrowserCopyButtonQuery{
                                                  .value = metadataFileValue,
                                                  .stableId = "asset-browser-copy-metadata-file",
                                                  .tooltipKey = "assetBrowser.copy.metadataFile",
                                                  .tooltipFallback = "Copy metadata file path",
                                              });
            ImGui::SameLine();
            drawSelectedAssetCopyButton(i18n, AssetBrowserCopyButtonQuery{
                                                  .value = sourceRootDirectoryValue,
                                                  .stableId = "asset-browser-copy-source-root",
                                                  .tooltipKey = "assetBrowser.copy.sourceRoot",
                                                  .tooltipFallback = "Copy source root path",
                                              });
        }

        const std::string guidLabel = textValue(i18n, "assetBrowser.detail.guid", "GUID");
        const std::string sourcePathLabel =
            textValue(i18n, "assetBrowser.detail.sourcePath", "Source Path");
        const std::string sourceFileLabel =
            textValue(i18n, "assetBrowser.detail.sourceFile", "Source File");
        const std::string metadataFileLabel =
            textValue(i18n, "assetBrowser.detail.metadataFile", "Metadata File");
        const std::string sourceRootLabel =
            textValue(i18n, "assetBrowser.detail.sourceRoot", "Source Root");
        const std::string sourceRootDirectoryLabel =
            textValue(i18n, "assetBrowser.detail.sourceRootDirectory", "Source Root Directory");
        const std::string typeLabel = textValue(i18n, "assetBrowser.detail.type", "Type");
        const std::string importerLabel =
            textValue(i18n, "assetBrowser.detail.importer", "Importer");
        const std::string importerVersionLabel =
            textValue(i18n, "assetBrowser.detail.importerVersion", "Importer Version");
        const std::string importProfileLabel =
            textValue(i18n, "assetBrowser.detail.importProfile", "Import Profile");
        const std::string assetRoleLabel =
            textValue(i18n, "assetBrowser.detail.assetRole", "Asset Role");
        const std::string extensionLabel =
            textValue(i18n, "assetBrowser.detail.extension", "Extension");
        const std::string subAssetCountLabel =
            textValue(i18n, "assetBrowser.detail.subAssetCount", "Sub-assets");
        const std::string productStateLabel =
            textValue(i18n, "assetBrowser.detail.productState", "Product State");
        const std::string currentProductsLabel =
            textValue(i18n, "assetBrowser.detail.currentProducts", "Current Products");
        const std::string staleProductsLabel =
            textValue(i18n, "assetBrowser.detail.staleProducts", "Stale Products");

        const std::string importerVersionValue = std::to_string(selectedRow->importerVersion.value);
        const std::string subAssetCountValue = std::to_string(selectedRow->subAssets.size());
        const std::string productStateValue{i18n.text(asharia::editor::EditorI18nTextQuery{
            .key = stateTextKey(selectedRow->productState),
            .fallback = stateFallback(selectedRow->productState),
        })};
        const std::string currentProductsValue = std::to_string(selectedRow->currentProductCount);
        const std::string staleProductsValue = std::to_string(selectedRow->staleProductCount);

        if (asharia::editor::beginEditorUiPropertyTable("asset-browser-selected-details", 128.0F)) {
            asharia::editor::drawEditorUiProperty(asharia::editor::EditorUiProperty{
                .label = std::string_view{guidLabel}, .value = textOrDash(selectedRow->guidText)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{sourcePathLabel},
                                                  .value = textOrDash(selectedRow->sourcePath)});
            if (hasSnapshot) {
                asharia::editor::drawEditorUiProperty(
                    asharia::editor::EditorUiProperty{.label = std::string_view{sourceFileLabel},
                                                      .value = textOrDash(sourceFileValue)});
                asharia::editor::drawEditorUiProperty(
                    asharia::editor::EditorUiProperty{.label = std::string_view{metadataFileLabel},
                                                      .value = textOrDash(metadataFileValue)});
                asharia::editor::drawEditorUiProperty(
                    asharia::editor::EditorUiProperty{.label = std::string_view{sourceRootLabel},
                                                      .value = textOrDash(sourceRootValue)});
                asharia::editor::drawEditorUiProperty(asharia::editor::EditorUiProperty{
                    .label = std::string_view{sourceRootDirectoryLabel},
                    .value = textOrDash(sourceRootDirectoryValue)});
            }
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{typeLabel},
                                                  .value = textOrDash(selectedRow->assetTypeName)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{importerLabel},
                                                  .value = textOrDash(selectedRow->importerName)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{importerVersionLabel},
                                                  .value = std::string_view{importerVersionValue}});
            asharia::editor::drawEditorUiProperty(asharia::editor::EditorUiProperty{
                .label = std::string_view{importProfileLabel},
                .value = textOrDash(selectedRow->importProfileName)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{assetRoleLabel},
                                                  .value = textOrDash(selectedRow->assetRoleName)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{extensionLabel},
                                                  .value = textOrDash(selectedRow->extension)});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{subAssetCountLabel},
                                                  .value = std::string_view{subAssetCountValue}});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{productStateLabel},
                                                  .value = std::string_view{productStateValue}});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{currentProductsLabel},
                                                  .value = std::string_view{currentProductsValue}});
            asharia::editor::drawEditorUiProperty(
                asharia::editor::EditorUiProperty{.label = std::string_view{staleProductsLabel},
                                                  .value = std::string_view{staleProductsValue}});
            asharia::editor::endEditorUiPropertyTable();
        }

        drawSelectedAssetImportSettings(context, *selectedRow, metadataFile, importSettingsState);
        drawSelectedSubAssetDetails(context, *selectedRow, selectedSubAsset);
        drawSelectedAssetSubAssets(i18n, *selectedRow, selectedSubAssetStableId);
        drawSelectedAssetDiagnostics(*selectedRow);
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
        const ImGuiStyle& style = ImGui::GetStyle();
        const float clearIconSize = 16.0F;
        const float clearButtonWidth = clearIconSize + (style.FramePadding.x * 2.0F);
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const bool clearButtonInline =
            availableWidth > (clearButtonWidth + style.ItemSpacing.x + 80.0F);
        if (clearButtonInline) {
            ImGui::SetNextItemWidth(availableWidth - clearButtonWidth - style.ItemSpacing.x);
        } else {
            ImGui::SetNextItemWidth(-1.0F);
        }
        const std::string filterLabel =
            label(i18n, "assetBrowser.filter", "asset-browser-filter", "Filter");
        ImGui::InputText(filterLabel.c_str(), filter_.data(), filter_.size());
        if (clearButtonInline) {
            ImGui::SameLine();
        }
        const bool clearEnabled =
            filter_[0] != '\0' || !selectedFolderScope_.empty() ||
            !selectedAssetTypeFilter_.empty() || !selectedImportProfileFilter_.empty() ||
            !selectedProductStateFilter_.empty() || !selectedAssetKey_.empty() ||
            !selectedNavigationKey_.empty() || !selectedSubAssetStableId_.empty();
        const std::string clearTooltip =
            textValue(i18n, "assetBrowser.filter.clear", "Clear filters");
        const EditorIconDescriptor clearIcon = makeLucideEditorIconDescriptor(
            "x", editorIconTint(0.78F, 0.80F, 0.84F), "assetBrowser.filter.clear", clearTooltip);
        if (!clearEnabled) {
            ImGui::BeginDisabled();
        }
        if (drawEditorIconButton(clearIcon, "asset-browser-clear-filters", clearIconSize)) {
            filter_.fill('\0');
            selectedFolderScope_.clear();
            selectedAssetTypeFilter_.clear();
            selectedImportProfileFilter_.clear();
            selectedProductStateFilter_.clear();
            selectedAssetKey_.clear();
            selectedNavigationKey_.clear();
            selectedSubAssetStableId_.clear();
        }
        if (!clearEnabled) {
            ImGui::EndDisabled();
        }
        ImGui::Separator();

        const std::string_view filter{filter_.data()};
        if (!assetTypeFilterExists(context.catalogView, selectedFolderScope_,
                                   selectedAssetTypeFilter_)) {
            selectedAssetTypeFilter_.clear();
            selectedImportProfileFilter_.clear();
            selectedProductStateFilter_.clear();
            selectedAssetKey_.clear();
            selectedSubAssetStableId_.clear();
        }
        if (!importProfileFilterExists(context.catalogView,
                                       AssetBrowserImportProfileFilterQuery{
                                           .folderScope = selectedFolderScope_,
                                           .assetTypeFilter = selectedAssetTypeFilter_,
                                       },
                                       selectedImportProfileFilter_)) {
            selectedImportProfileFilter_.clear();
            selectedProductStateFilter_.clear();
            selectedAssetKey_.clear();
            selectedSubAssetStableId_.clear();
        }
        if (!productStateFilterExists(context.catalogView,
                                      AssetBrowserProductStateFilterQuery{
                                          .folderScope = selectedFolderScope_,
                                          .assetTypeFilter = selectedAssetTypeFilter_,
                                          .importProfileFilter = selectedImportProfileFilter_,
                                      },
                                      selectedProductStateFilter_)) {
            selectedProductStateFilter_.clear();
            selectedAssetKey_.clear();
            selectedSubAssetStableId_.clear();
        }
        const asharia::asset::AssetCatalogViewEntry* selectedRow = selectedVisibleRow(
            context.catalogView, AssetBrowserSelectionQuery{
                                     .filter = filter,
                                     .folderScope = selectedFolderScope_,
                                     .assetTypeFilter = selectedAssetTypeFilter_,
                                     .importProfileFilter = selectedImportProfileFilter_,
                                     .productStateFilter = selectedProductStateFilter_,
                                     .selectedAssetKey = selectedAssetKey_,
                                 });
        if (selectedRow != nullptr &&
            std::string_view{selectedAssetKey_} != selectionKeyForRow(*selectedRow)) {
            const std::string_view selectionKey = selectionKeyForRow(*selectedRow);
            selectedAssetKey_.assign(selectionKey.data(), selectionKey.size());
            if (findSubAssetByStableId(*selectedRow, selectedSubAssetStableId_) == nullptr) {
                selectedSubAssetStableId_.clear();
            }
        } else if (selectedRow == nullptr) {
            selectedAssetKey_.clear();
            selectedSubAssetStableId_.clear();
        }

        if (context.catalogSnapshot == nullptr) {
            selectedNavigationKey_.clear();
            selectedSubAssetStableId_.clear();
            drawFolderScopeControls(
                context, AssetBrowserFolderScopeState{
                             .selectedFolderScope = &selectedFolderScope_,
                             .selectedAssetTypeFilter = &selectedAssetTypeFilter_,
                             .selectedImportProfileFilter = &selectedImportProfileFilter_,
                             .selectedProductStateFilter = &selectedProductStateFilter_,
                             .selectedAssetKey = &selectedAssetKey_,
                             .selectedSubAssetStableId = &selectedSubAssetStableId_,
                         });
        } else {
            drawNavigationTreeControls(
                context, AssetBrowserNavigationTreeState{
                             .selectedFolderScope = &selectedFolderScope_,
                             .selectedAssetTypeFilter = &selectedAssetTypeFilter_,
                             .selectedImportProfileFilter = &selectedImportProfileFilter_,
                             .selectedProductStateFilter = &selectedProductStateFilter_,
                             .selectedAssetKey = &selectedAssetKey_,
                             .selectedNavigationKey = &selectedNavigationKey_,
                             .selectedSubAssetStableId = &selectedSubAssetStableId_,
                         });
        }
        drawAssetTypeFilterControl(context, selectedFolderScope_, selectedAssetTypeFilter_,
                                   selectedImportProfileFilter_, selectedProductStateFilter_,
                                   selectedAssetKey_, selectedSubAssetStableId_);
        drawImportProfileFilterControl(context,
                                       AssetBrowserImportProfileFilterQuery{
                                           .folderScope = selectedFolderScope_,
                                           .assetTypeFilter = selectedAssetTypeFilter_,
                                       },
                                       selectedImportProfileFilter_, selectedProductStateFilter_,
                                       selectedAssetKey_, selectedSubAssetStableId_);
        drawProductStateFilterControl(context,
                                      AssetBrowserProductStateFilterQuery{
                                          .folderScope = selectedFolderScope_,
                                          .assetTypeFilter = selectedAssetTypeFilter_,
                                          .importProfileFilter = selectedImportProfileFilter_,
                                      },
                                      selectedProductStateFilter_, selectedAssetKey_,
                                      selectedSubAssetStableId_);
        ImGui::Separator();
        const AssetBrowserVisibilityQuery visibility{
            .filter = filter,
            .folderScope = selectedFolderScope_,
            .assetTypeFilter = selectedAssetTypeFilter_,
            .importProfileFilter = selectedImportProfileFilter_,
            .productStateFilter = selectedProductStateFilter_,
        };
        drawAssetBrowserSummary(context, visibility);
        ImGui::Separator();
        drawAssetBrowserCatalogSource(context);
        ImGui::Separator();
        drawCatalogDiagnostics(context);
        drawAssetBrowserRows(context, visibility,
                             AssetBrowserRowsState{
                                 .selectedAssetKey = &selectedAssetKey_,
                                 .selectedNavigationKey = &selectedNavigationKey_,
                                 .selectedSubAssetStableId = &selectedSubAssetStableId_,
                             });
        selectedRow = selectedVisibleRow(context.catalogView,
                                         AssetBrowserSelectionQuery{
                                             .filter = filter,
                                             .folderScope = selectedFolderScope_,
                                             .assetTypeFilter = selectedAssetTypeFilter_,
                                             .importProfileFilter = selectedImportProfileFilter_,
                                             .productStateFilter = selectedProductStateFilter_,
                                             .selectedAssetKey = selectedAssetKey_,
                                         });
        ImGui::Separator();
        if (selectedRow != nullptr &&
            findSubAssetByStableId(*selectedRow, selectedSubAssetStableId_) == nullptr) {
            selectedSubAssetStableId_.clear();
        }
        drawSelectedAssetDetails(context, selectedRow, selectedSubAssetStableId_,
                                 AssetBrowserImportSettingsUiState{
                                     .selectionKey = &importSettingsSelectionKey_,
                                     .textureProfileDraft = &textureProfileDraft_,
                                     .textureProfileBaseline = &textureProfileBaseline_,
                                     .message = &importSettingsMessage_,
                                     .messageIsError = &importSettingsMessageIsError_,
                                 });
    }

} // namespace asharia::editor
