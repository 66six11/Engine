#include "asharia/asset_core/asset_product.hpp"

#include <cstdint>

namespace asharia::asset {
    namespace {

        constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

        [[nodiscard]] constexpr std::uint64_t hashByte(std::uint64_t hash,
                                                       std::uint8_t byte) noexcept {
            hash ^= byte;
            hash *= kFnv1a64Prime;
            return hash;
        }

        [[nodiscard]] constexpr std::uint64_t hashUint64(std::uint64_t hash,
                                                         std::uint64_t value) noexcept {
            for (std::uint32_t shift = 0; shift < 64; shift += 8) {
                hash = hashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
            }
            return hash;
        }

        [[nodiscard]] constexpr std::uint64_t hashText(std::uint64_t hash,
                                                       std::string_view text) noexcept {
            hash = hashUint64(hash, text.size());
            for (const char character : text) {
                hash = hashByte(hash, static_cast<unsigned char>(character));
            }
            return hash;
        }

        [[nodiscard]] constexpr std::uint64_t hashGuid(std::uint64_t hash,
                                                       AssetGuid guid) noexcept {
            for (const std::uint8_t byte : guid.bytes) {
                hash = hashByte(hash, byte);
            }
            return hash;
        }

    } // namespace

    std::uint64_t makeAssetTargetProfileHash(std::string_view targetProfile) noexcept {
        if (targetProfile.empty()) {
            return 0;
        }

        return hashText(kFnv1a64Offset, targetProfile);
    }

    std::uint64_t hashAssetDependencies(std::span<const AssetDependency> dependencies) noexcept {
        std::uint64_t hash = hashUint64(kFnv1a64Offset, dependencies.size());
        for (const AssetDependency& dependency : dependencies) {
            hash = hashGuid(hash, dependency.owner);
            hash = hashUint64(hash, static_cast<std::uint64_t>(dependency.kind));
            hash = hashGuid(hash, dependency.asset);
            hash = hashText(hash, dependency.path);
            hash = hashUint64(hash, dependency.hash);
        }
        return hash;
    }

    AssetProductKey makeAssetProductKey(const SourceAssetRecord& source,
                                        std::uint64_t dependencyHash,
                                        std::uint64_t targetProfileHash) noexcept {
        return AssetProductKey{
            .guid = source.guid,
            .assetType = source.assetType,
            .importerId = source.importerId,
            .importerVersion = source.importerVersion,
            .sourceHash = source.sourceHash,
            .settingsHash = source.settingsHash,
            .dependencyHash = dependencyHash,
            .targetProfileHash = targetProfileHash,
        };
    }

    std::uint64_t hashAssetProductKey(const AssetProductKey& key) noexcept {
        std::uint64_t hash = hashGuid(kFnv1a64Offset, key.guid);
        hash = hashUint64(hash, key.assetType.value);
        hash = hashUint64(hash, key.importerId.value);
        hash = hashUint64(hash, key.importerVersion.value);
        hash = hashUint64(hash, key.sourceHash);
        hash = hashUint64(hash, key.settingsHash);
        hash = hashUint64(hash, key.dependencyHash);
        hash = hashUint64(hash, key.targetProfileHash);
        return hash;
    }

} // namespace asharia::asset
