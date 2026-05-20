#include "asharia/asset_core/asset_catalog.hpp"

#include <algorithm>
#include <expected>
#include <string>
#include <utility>

namespace asharia::asset {
    namespace {

        [[nodiscard]] Error assetCatalogError(std::string message) {
            return Error{ErrorDomain::Asset, 4, std::move(message)};
        }

        [[nodiscard]] std::string sourceRecordLabel(const SourceAssetRecord& record) {
            return "guid=\"" + formatAssetGuid(record.guid) + "\" source=\"" + record.sourcePath +
                   "\"";
        }

    } // namespace

    VoidResult AssetCatalog::addSource(SourceAssetRecord record) {
        auto validRecord = validateSourceAssetRecord(record);
        if (!validRecord) {
            return std::unexpected{std::move(validRecord.error())};
        }

        if (const SourceAssetRecord* existing = findByGuid(record.guid)) {
            return std::unexpected{assetCatalogError("Asset catalog duplicate GUID existing " +
                                                     sourceRecordLabel(*existing) + " new " +
                                                     sourceRecordLabel(record) + ".")};
        }

        if (const SourceAssetRecord* existing = findBySourcePath(record.sourcePath)) {
            return std::unexpected{assetCatalogError(
                "Asset catalog duplicate source path existing " + sourceRecordLabel(*existing) +
                " new " + sourceRecordLabel(record) + ".")};
        }

        sources_.push_back(std::move(record));
        return {};
    }

    VoidResult AssetCatalog::updateSource(SourceAssetRecord record,
                                          AssetCatalogRelocationPolicy relocationPolicy) {
        auto validRecord = validateSourceAssetRecord(record);
        if (!validRecord) {
            return std::unexpected{std::move(validRecord.error())};
        }

        auto existing =
            std::ranges::find_if(sources_, [guid = record.guid](const SourceAssetRecord& source) {
                return source.guid == guid;
            });
        if (existing == sources_.end()) {
            return std::unexpected{assetCatalogError("Asset catalog cannot update missing source " +
                                                     sourceRecordLabel(record) + ".")};
        }

        if (const SourceAssetRecord* pathOwner = findBySourcePath(record.sourcePath);
            pathOwner != nullptr && pathOwner->guid != record.guid) {
            return std::unexpected{assetCatalogError(
                "Asset catalog duplicate source path existing " + sourceRecordLabel(*pathOwner) +
                " new " + sourceRecordLabel(record) + ".")};
        }

        if (existing->sourcePath != record.sourcePath &&
            relocationPolicy == AssetCatalogRelocationPolicy::RejectPathChange) {
            return std::unexpected{assetCatalogError("Asset catalog relocation rejected existing " +
                                                     sourceRecordLabel(*existing) + " new " +
                                                     sourceRecordLabel(record) + ".")};
        }

        *existing = std::move(record);
        return {};
    }

    VoidResult AssetCatalog::removeSource(AssetGuid guid) {
        if (!guid) {
            return std::unexpected{
                assetCatalogError("Asset catalog cannot remove invalid asset GUID.")};
        }

        const auto existing = std::ranges::find_if(
            sources_, [guid](const SourceAssetRecord& source) { return source.guid == guid; });
        if (existing == sources_.end()) {
            return std::unexpected{
                assetCatalogError("Asset catalog cannot remove missing source guid=\"" +
                                  formatAssetGuid(guid) + "\".")};
        }

        sources_.erase(existing);
        return {};
    }

    const SourceAssetRecord* AssetCatalog::findByGuid(AssetGuid guid) const noexcept {
        const auto found = std::ranges::find_if(
            sources_, [guid](const SourceAssetRecord& source) { return source.guid == guid; });
        return found == sources_.end() ? nullptr : &*found;
    }

    const SourceAssetRecord*
    AssetCatalog::findBySourcePath(std::string_view sourcePath) const noexcept {
        const auto found =
            std::ranges::find_if(sources_, [sourcePath](const SourceAssetRecord& source) {
                return source.sourcePath == sourcePath;
            });
        return found == sources_.end() ? nullptr : &*found;
    }

    std::span<const SourceAssetRecord> AssetCatalog::sources() const noexcept {
        return std::span<const SourceAssetRecord>{sources_.data(), sources_.size()};
    }

} // namespace asharia::asset
