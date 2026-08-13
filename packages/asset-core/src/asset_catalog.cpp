#include "asharia/asset_core/asset_catalog.hpp"

#include <algorithm>
#include <expected>
#include <iterator>
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
        const std::size_t sourceIndex = sources_.size() - 1U;
        try {
            sourceIndicesByGuid_.emplace(sources_.back().guid.bytes, sourceIndex);
            try {
                sourceIndicesByPath_.emplace(sources_.back().sourcePath, sourceIndex);
            } catch (...) {
                sourceIndicesByGuid_.erase(sources_.back().guid.bytes);
                throw;
            }
        } catch (...) {
            sources_.pop_back();
            throw;
        }
        return {};
    }

    VoidResult AssetCatalog::updateSource(SourceAssetRecord record,
                                          AssetCatalogRelocationPolicy relocationPolicy) {
        auto validRecord = validateSourceAssetRecord(record);
        if (!validRecord) {
            return std::unexpected{std::move(validRecord.error())};
        }

        const auto existingIndex = sourceIndicesByGuid_.find(record.guid.bytes);
        if (existingIndex == sourceIndicesByGuid_.end()) {
            return std::unexpected{assetCatalogError("Asset catalog cannot update missing source " +
                                                     sourceRecordLabel(record) + ".")};
        }
        SourceAssetRecord& existing = sources_[existingIndex->second];

        if (const SourceAssetRecord* pathOwner = findBySourcePath(record.sourcePath);
            pathOwner != nullptr && pathOwner->guid != record.guid) {
            return std::unexpected{assetCatalogError(
                "Asset catalog duplicate source path existing " + sourceRecordLabel(*pathOwner) +
                " new " + sourceRecordLabel(record) + ".")};
        }

        if (existing.sourcePath != record.sourcePath &&
            relocationPolicy == AssetCatalogRelocationPolicy::RejectPathChange) {
            return std::unexpected{assetCatalogError("Asset catalog relocation rejected existing " +
                                                     sourceRecordLabel(existing) + " new " +
                                                     sourceRecordLabel(record) + ".")};
        }

        if (existing.sourcePath != record.sourcePath) {
            const auto inserted =
                sourceIndicesByPath_.emplace(record.sourcePath, existingIndex->second);
            if (!inserted.second) {
                return std::unexpected{
                    assetCatalogError("Asset catalog could not index relocated source " +
                                      sourceRecordLabel(record) + ".")};
            }
            sourceIndicesByPath_.erase(existing.sourcePath);
        }
        existing = std::move(record);
        return {};
    }

    VoidResult AssetCatalog::removeSource(AssetGuid guid) {
        if (!guid) {
            return std::unexpected{
                assetCatalogError("Asset catalog cannot remove invalid asset GUID.")};
        }

        const auto existing = sourceIndicesByGuid_.find(guid.bytes);
        if (existing == sourceIndicesByGuid_.end()) {
            return std::unexpected{
                assetCatalogError("Asset catalog cannot remove missing source guid=\"" +
                                  formatAssetGuid(guid) + "\".")};
        }

        const std::size_t removedIndex = existing->second;
        sourceIndicesByPath_.erase(sources_[removedIndex].sourcePath);
        sourceIndicesByGuid_.erase(existing);
        sources_.erase(std::next(sources_.begin(), static_cast<std::ptrdiff_t>(removedIndex)));
        for (auto& [guidBytes, sourceIndex] : sourceIndicesByGuid_) {
            (void)guidBytes;
            if (sourceIndex > removedIndex) {
                --sourceIndex;
            }
        }
        for (auto& [sourcePath, sourceIndex] : sourceIndicesByPath_) {
            (void)sourcePath;
            if (sourceIndex > removedIndex) {
                --sourceIndex;
            }
        }
        return {};
    }

    const SourceAssetRecord* AssetCatalog::findByGuid(AssetGuid guid) const noexcept {
        const auto found = sourceIndicesByGuid_.find(guid.bytes);
        return found == sourceIndicesByGuid_.end() ? nullptr : &sources_[found->second];
    }

    const SourceAssetRecord*
    AssetCatalog::findBySourcePath(std::string_view sourcePath) const noexcept {
        const auto found = sourceIndicesByPath_.find(sourcePath);
        return found == sourceIndicesByPath_.end() ? nullptr : &sources_[found->second];
    }

    std::span<const SourceAssetRecord> AssetCatalog::sources() const noexcept {
        return std::span<const SourceAssetRecord>{sources_.data(), sources_.size()};
    }

} // namespace asharia::asset
