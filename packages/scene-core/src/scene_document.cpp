#include "asharia/scene/scene_document.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "asharia/core/error.hpp"
#include "asharia/scene/scene_document_io.hpp"

namespace asharia::scene {
    namespace {

        constexpr std::size_t kUuidTextLength = 36;
        constexpr float kUnitQuaternionTolerance = 1.0e-3F;

        [[nodiscard]] Error sceneDocumentError(SceneDocumentErrorCode code, std::string message) {
            return Error{ErrorDomain::Scene, static_cast<int>(code), std::move(message)};
        }

        [[nodiscard]] std::string pathText(const std::filesystem::path& path) {
            const std::u8string utf8 = path.u8string();
            return std::string{utf8.begin(), utf8.end()};
        }

        [[nodiscard]] constexpr int hexadecimalValue(char character) noexcept {
            if (character >= '0' && character <= '9') {
                return character - '0';
            }
            if (character >= 'a' && character <= 'f') {
                return character - 'a' + 10;
            }
            if (character >= 'A' && character <= 'F') {
                return character - 'A' + 10;
            }
            return -1;
        }

        template <typename Identifier>
        [[nodiscard]] Result<Identifier> parseUuid(std::string_view text, const char* context) {
            if (text.size() != kUuidTextLength) {
                return std::unexpected{sceneDocumentError(
                    SceneDocumentErrorCode::InvalidScene,
                    std::string{context} + " must contain a 36-character UUID.")};
            }

            Identifier identifier{};
            std::span<std::uint8_t> identifierBytes{identifier.bytes};
            std::size_t byteIndex = 0;
            for (std::size_t index = 0; index < text.size();) {
                if (index == 8U || index == 13U || index == 18U || index == 23U) {
                    if (text[index] != '-') {
                        return std::unexpected{sceneDocumentError(
                            SceneDocumentErrorCode::InvalidScene,
                            std::string{context} + " has invalid UUID separators.")};
                    }
                    ++index;
                    continue;
                }

                const int high = hexadecimalValue(text[index]);
                const int low = hexadecimalValue(text[index + 1U]);
                if (high < 0 || low < 0) {
                    return std::unexpected{sceneDocumentError(
                        SceneDocumentErrorCode::InvalidScene,
                        std::string{context} + " contains non-hexadecimal UUID digits.")};
                }
                identifierBytes[byteIndex] = static_cast<std::uint8_t>((high << 4) | low);
                ++byteIndex;
                index += 2U;
            }

            if (!identifier) {
                return std::unexpected{
                    sceneDocumentError(SceneDocumentErrorCode::InvalidScene,
                                       std::string{context} + " must not use the zero UUID.")};
            }
            return identifier;
        }

        template <typename Identifier> [[nodiscard]] std::string formatUuid(Identifier identifier) {
            constexpr std::array<char, 16> kHexadecimalDigits{
                '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
            std::string text;
            text.reserve(kUuidTextLength);
            std::size_t index = 0U;
            for (std::uint8_t byte : identifier.bytes) {
                if (index == 4U || index == 6U || index == 8U || index == 10U) {
                    text.push_back('-');
                }
                text.push_back(kHexadecimalDigits.at(byte >> 4U));
                text.push_back(kHexadecimalDigits.at(byte & 0x0FU));
                ++index;
            }
            return text;
        }

        [[nodiscard]] bool isValidUtf8(std::string_view text) noexcept {
            std::size_t index = 0;
            while (index < text.size()) {
                const auto leading = static_cast<unsigned char>(text[index]);
                std::size_t continuationCount = 0;
                std::uint32_t codePoint = 0;
                if (leading <= 0x7FU) {
                    ++index;
                    continue;
                }
                if (leading >= 0xC2U && leading <= 0xDFU) {
                    continuationCount = 1;
                    codePoint = leading & 0x1FU;
                } else if (leading >= 0xE0U && leading <= 0xEFU) {
                    continuationCount = 2;
                    codePoint = leading & 0x0FU;
                } else if (leading >= 0xF0U && leading <= 0xF4U) {
                    continuationCount = 3;
                    codePoint = leading & 0x07U;
                } else {
                    return false;
                }
                if (continuationCount > text.size() - index - 1U) {
                    return false;
                }
                for (std::size_t offset = 1; offset <= continuationCount; ++offset) {
                    const auto continuation = static_cast<unsigned char>(text[index + offset]);
                    if ((continuation & 0xC0U) != 0x80U) {
                        return false;
                    }
                    codePoint = (codePoint << 6U) | (continuation & 0x3FU);
                }
                if ((continuationCount == 2U && codePoint < 0x800U) ||
                    (continuationCount == 3U && codePoint < 0x10000U) ||
                    (codePoint >= 0xD800U && codePoint <= 0xDFFFU) || codePoint > 0x10FFFFU) {
                    return false;
                }
                index += continuationCount + 1U;
            }
            return true;
        }

        [[nodiscard]] bool finite(Vec3 value) noexcept {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool validTransform(const TransformComponent& transform) noexcept {
            if (!finite(transform.position) || !finite(transform.scale) ||
                !std::isfinite(transform.rotation.x) || !std::isfinite(transform.rotation.y) ||
                !std::isfinite(transform.rotation.z) || !std::isfinite(transform.rotation.w)) {
                return false;
            }
            const float lengthSquared = (transform.rotation.x * transform.rotation.x) +
                                        (transform.rotation.y * transform.rotation.y) +
                                        (transform.rotation.z * transform.rotation.z) +
                                        (transform.rotation.w * transform.rotation.w);
            return std::fabs(lengthSquared - 1.0F) <= kUnitQuaternionTolerance;
        }

        [[nodiscard]] VoidResult validateEntity(const SceneEntityData& entity, std::size_t index) {
            const std::string context = "Scene entity[" + std::to_string(index) + "]";
            if (!entity.objectId) {
                return std::unexpected{
                    sceneDocumentError(SceneDocumentErrorCode::InvalidObjectId,
                                       context + " must use a non-zero object ID.")};
            }
            if (entity.name.size() > kMaxSceneEntityNameUtf8Bytes || !isValidUtf8(entity.name)) {
                return std::unexpected{sceneDocumentError(
                    SceneDocumentErrorCode::InvalidScene,
                    context + " name must be valid UTF-8 within the configured byte limit.")};
            }
            if (!validTransform(entity.transform)) {
                return std::unexpected{
                    sceneDocumentError(SceneDocumentErrorCode::InvalidTransform,
                                       context + " contains a non-finite or non-unit Transform.")};
            }
            if (entity.mesh.has_value() && (!static_cast<bool>(*entity.mesh) ||
                                            entity.mesh->expectedType != kSceneMeshAssetType)) {
                return std::unexpected{sceneDocumentError(
                    SceneDocumentErrorCode::InvalidAssetReference,
                    context + " mesh must reference a non-zero asset GUID with expected type '" +
                        std::string{kSceneMeshAssetTypeName} + "'.")};
            }
            return {};
        }

        [[nodiscard]] Error invalidObjectError(SceneObjectId objectId, std::string_view operation) {
            return sceneDocumentError(
                SceneDocumentErrorCode::InvalidObjectId,
                "Scene document operation rejected an unknown object [operation=" +
                    std::string{operation} + "; objectId=" + formatSceneObjectId(objectId) + "].");
        }

    } // namespace

    SceneId::operator bool() const noexcept {
        return std::ranges::any_of(bytes, [](std::uint8_t byte) { return byte != 0U; });
    }

    SceneObjectId::operator bool() const noexcept {
        return std::ranges::any_of(bytes, [](std::uint8_t byte) { return byte != 0U; });
    }

    Result<SceneId> parseSceneId(std::string_view text) {
        return parseUuid<SceneId>(text, "Scene ID");
    }

    std::string formatSceneId(SceneId sceneId) {
        return formatUuid(sceneId);
    }

    Result<SceneObjectId> parseSceneObjectId(std::string_view text) {
        return parseUuid<SceneObjectId>(text, "Scene object ID");
    }

    std::string formatSceneObjectId(SceneObjectId objectId) {
        return formatUuid(objectId);
    }

    VoidResult validateSceneDocumentData(const SceneDocumentData& data) {
        if (!data.sceneId) {
            return std::unexpected{
                sceneDocumentError(SceneDocumentErrorCode::InvalidScene,
                                   "Scene document must use a non-zero scene ID.")};
        }
        if (data.entities.size() > kMaxSceneEntities) {
            return std::unexpected{
                sceneDocumentError(SceneDocumentErrorCode::InvalidScene,
                                   "Scene document entity count exceeds the configured limit.")};
        }
        std::set<std::array<std::uint8_t, 16>> objectIds;
        for (std::size_t index = 0; index < data.entities.size(); ++index) {
            if (auto valid = validateEntity(data.entities[index], index); !valid) {
                return valid;
            }
            if (!objectIds.insert(data.entities[index].objectId.bytes).second) {
                return std::unexpected{sceneDocumentError(
                    SceneDocumentErrorCode::DuplicateObjectId,
                    "Scene document contains duplicate object ID '" +
                        formatSceneObjectId(data.entities[index].objectId) + "'.")};
            }
        }
        return {};
    }

    SceneDocument::SceneDocument(std::filesystem::path path, SceneDocumentData data, World world,
                                 std::vector<RuntimeEntity> runtimeEntities)
        : path_(std::move(path)), data_(std::move(data)), world_(std::move(world)),
          runtimeEntities_(std::move(runtimeEntities)) {}

    Result<SceneDocument>
    SceneDocument::openOrCreateDefault(const std::filesystem::path& projectRoot,
                                       SceneId newSceneId) {
        std::error_code filesystemError;
        if (!std::filesystem::is_directory(projectRoot, filesystemError) || filesystemError) {
            return std::unexpected{
                sceneDocumentError(SceneDocumentErrorCode::SceneIo,
                                   "Scene document project root is not an accessible directory '" +
                                       pathText(projectRoot) + "'.")};
        }

        const std::filesystem::path scenePath =
            projectRoot / std::filesystem::path{kDefaultSceneRelativePath};
        SceneDocumentData data;
        const bool exists = std::filesystem::exists(scenePath, filesystemError);
        if (filesystemError) {
            return std::unexpected{sceneDocumentError(SceneDocumentErrorCode::SceneIo,
                                                      "Could not inspect default scene path '" +
                                                          pathText(scenePath) + "'.")};
        }
        if (exists) {
            auto read = readSceneDocumentFile(scenePath);
            if (!read) {
                return std::unexpected{std::move(read.error())};
            }
            data = std::move(*read);
        } else {
            if (!newSceneId) {
                return std::unexpected{sceneDocumentError(
                    SceneDocumentErrorCode::InvalidScene,
                    "A non-zero scene ID is required when creating the default scene.")};
            }
            std::filesystem::create_directories(scenePath.parent_path(), filesystemError);
            if (filesystemError) {
                return std::unexpected{
                    sceneDocumentError(SceneDocumentErrorCode::SceneIo,
                                       "Could not create the default scene directory '" +
                                           pathText(scenePath.parent_path()) + "'.")};
            }
            data.sceneId = newSceneId;
            if (auto written = writeSceneDocumentFile(scenePath, data); !written) {
                return std::unexpected{std::move(written.error())};
            }
            auto verified = readSceneDocumentFile(scenePath);
            if (!verified) {
                return std::unexpected{std::move(verified.error())};
            }
            data = std::move(*verified);
        }

        if (auto valid = validateSceneDocumentData(data); !valid) {
            return std::unexpected{std::move(valid.error())};
        }

        World world;
        std::vector<RuntimeEntity> runtimeEntities;
        runtimeEntities.reserve(data.entities.size());
        for (const SceneEntityData& persisted : data.entities) {
            auto created = world.createEntity(persisted.name);
            if (!created) {
                return std::unexpected{
                    sceneDocumentError(SceneDocumentErrorCode::InvalidScene,
                                       "Could not materialize persisted scene entity '" +
                                           formatSceneObjectId(persisted.objectId) + "'.")};
            }
            if (auto transformed = world.setTransform(*created, persisted.transform);
                !transformed) {
                return std::unexpected{std::move(transformed.error())};
            }
            runtimeEntities.push_back(RuntimeEntity{
                .objectId = persisted.objectId,
                .entity = *created,
            });
        }
        return SceneDocument{scenePath, std::move(data), std::move(world),
                             std::move(runtimeEntities)};
    }

    const std::filesystem::path& SceneDocument::path() const noexcept {
        return path_;
    }

    SceneDocumentSnapshot SceneDocument::snapshot() const {
        std::vector<SceneDocumentSnapshot::RuntimeEntityBinding> runtimeEntities;
        runtimeEntities.reserve(runtimeEntities_.size());
        for (const RuntimeEntity& runtime : runtimeEntities_) {
            runtimeEntities.push_back(SceneDocumentSnapshot::RuntimeEntityBinding{
                .objectId = runtime.objectId,
                .entity = runtime.entity,
            });
        }
        return SceneDocumentSnapshot{
            .data = data_,
            .runtimeEntities = std::move(runtimeEntities),
            .revision = revision_,
            .savedRevision = savedRevision_,
        };
    }

    VoidResult SceneDocument::createEntity(SceneObjectId objectId, std::string_view name,
                                           std::uint64_t expectedRevision) {
        return createPreparedEntity(
            SceneEntityData{
                .objectId = objectId,
                .name = std::string{name},
                .transform = {},
                .mesh = std::nullopt,
            },
            expectedRevision);
    }

    VoidResult SceneDocument::createMeshEntity(SceneObjectId objectId, std::string_view name,
                                               asset::AssetGuid meshAsset,
                                               std::uint64_t expectedRevision) {
        return createPreparedEntity(
            SceneEntityData{
                .objectId = objectId,
                .name = std::string{name},
                .transform = {},
                .mesh = asset::makeAssetReference(meshAsset, kSceneMeshAssetType),
            },
            expectedRevision);
    }

    VoidResult SceneDocument::createPreparedEntity(SceneEntityData prepared,
                                                   std::uint64_t expectedRevision) {
        if (auto revision = requireRevision(expectedRevision); !revision) {
            return revision;
        }
        if (data_.entities.size() >= kMaxSceneEntities) {
            return std::unexpected{
                sceneDocumentError(SceneDocumentErrorCode::InvalidScene,
                                   "Scene document entity count reached the configured limit.")};
        }
        if (auto valid = validateEntity(prepared, data_.entities.size()); !valid) {
            return valid;
        }
        if (findRuntimeEntity(prepared.objectId) != nullptr) {
            return std::unexpected{
                sceneDocumentError(SceneDocumentErrorCode::DuplicateObjectId,
                                   "Scene document already contains object ID '" +
                                       formatSceneObjectId(prepared.objectId) + "'.")};
        }

        const SceneObjectId objectId = prepared.objectId;
        data_.entities.reserve(data_.entities.size() + 1U);
        runtimeEntities_.reserve(runtimeEntities_.size() + 1U);
        auto created = world_.createEntity(prepared.name);
        if (!created) {
            return std::unexpected{std::move(created.error())};
        }
        data_.entities.push_back(std::move(prepared));
        runtimeEntities_.push_back(RuntimeEntity{.objectId = objectId, .entity = *created});
        advanceRevision();
        return {};
    }

    VoidResult SceneDocument::setEntityName(SceneObjectId objectId, std::string_view name,
                                            std::uint64_t expectedRevision) {
        if (auto revision = requireRevision(expectedRevision); !revision) {
            return revision;
        }
        RuntimeEntity* runtime = findRuntimeEntity(objectId);
        if (runtime == nullptr) {
            return std::unexpected{invalidObjectError(objectId, "setEntityName")};
        }
        auto persisted = std::ranges::find(data_.entities, objectId, &SceneEntityData::objectId);
        if (persisted == data_.entities.end()) {
            return std::unexpected{invalidObjectError(objectId, "setEntityName")};
        }
        std::string prepared{name};
        if (prepared.size() > kMaxSceneEntityNameUtf8Bytes || !isValidUtf8(prepared)) {
            return std::unexpected{sceneDocumentError(
                SceneDocumentErrorCode::InvalidScene,
                "Scene entity name must be valid UTF-8 within the configured byte limit.")};
        }
        if (persisted->name == prepared) {
            return {};
        }
        if (auto changed = world_.setEntityName(runtime->entity, prepared); !changed) {
            return changed;
        }
        persisted->name = std::move(prepared);
        advanceRevision();
        return {};
    }

    VoidResult SceneDocument::setEntityTransform(SceneObjectId objectId,
                                                 const TransformComponent& transform,
                                                 std::uint64_t expectedRevision) {
        if (auto revision = requireRevision(expectedRevision); !revision) {
            return revision;
        }
        if (!validTransform(transform)) {
            return std::unexpected{sceneDocumentError(
                SceneDocumentErrorCode::InvalidTransform,
                "Scene entity Transform must be finite and use a unit quaternion.")};
        }
        RuntimeEntity* runtime = findRuntimeEntity(objectId);
        if (runtime == nullptr) {
            return std::unexpected{invalidObjectError(objectId, "setEntityTransform")};
        }
        auto persisted = std::ranges::find(data_.entities, objectId, &SceneEntityData::objectId);
        if (persisted == data_.entities.end()) {
            return std::unexpected{invalidObjectError(objectId, "setEntityTransform")};
        }
        if (persisted->transform == transform) {
            return {};
        }
        if (auto changed = world_.setTransform(runtime->entity, transform); !changed) {
            return changed;
        }
        persisted->transform = transform;
        advanceRevision();
        return {};
    }

    VoidResult SceneDocument::save(std::uint64_t expectedRevision) {
        if (auto revision = requireRevision(expectedRevision); !revision) {
            return revision;
        }
        if (savedRevision_ == revision_) {
            return {};
        }
        if (auto written = writeSceneDocumentFile(path_, data_); !written) {
            return written;
        }
        auto verified = readSceneDocumentFile(path_);
        if (!verified) {
            return std::unexpected{std::move(verified.error())};
        }
        if (*verified != data_) {
            return std::unexpected{sceneDocumentError(
                SceneDocumentErrorCode::SceneIo,
                "Scene document save verification did not reproduce the authoritative data.")};
        }
        savedRevision_ = revision_;
        return {};
    }

    SceneDocument::RuntimeEntity*
    SceneDocument::findRuntimeEntity(SceneObjectId objectId) noexcept {
        const auto found = std::ranges::find(runtimeEntities_, objectId, &RuntimeEntity::objectId);
        return found == runtimeEntities_.end() ? nullptr : &*found;
    }

    const SceneDocument::RuntimeEntity*
    SceneDocument::findRuntimeEntity(SceneObjectId objectId) const noexcept {
        const auto found = std::ranges::find(runtimeEntities_, objectId, &RuntimeEntity::objectId);
        return found == runtimeEntities_.end() ? nullptr : &*found;
    }

    VoidResult SceneDocument::requireRevision(std::uint64_t expectedRevision) const {
        if (expectedRevision == revision_) {
            return {};
        }
        return std::unexpected{sceneDocumentError(
            SceneDocumentErrorCode::RevisionConflict,
            "Scene document revision conflict [expected=" + std::to_string(expectedRevision) +
                "; actual=" + std::to_string(revision_) + "].")};
    }

    void SceneDocument::advanceRevision() noexcept {
        if (revision_ != std::numeric_limits<std::uint64_t>::max()) {
            ++revision_;
        }
    }

} // namespace asharia::scene
