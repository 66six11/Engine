#include "asharia/scene/scene_document_io.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/archive/json_archive.hpp"
#include "asharia/core/error.hpp"
#include "asharia/core/file_io.hpp"

namespace asharia::scene {
    namespace {

        using archive::ArchiveMember;
        using archive::ArchiveValue;
        using archive::ArchiveValueKind;
        using namespace std::string_view_literals;

        inline constexpr std::uint64_t kMaxSceneDocumentBytes = 64ULL * 1024ULL * 1024ULL;

        [[nodiscard]] Error sceneIoError(std::string message) {
            return Error{ErrorDomain::Scene, static_cast<int>(SceneDocumentErrorCode::SceneIo),
                         std::move(message)};
        }

        [[nodiscard]] bool containsName(std::span<const std::string_view> names,
                                        std::string_view name) noexcept {
            return std::ranges::any_of(
                names, [name](std::string_view allowed) { return allowed == name; });
        }

        [[nodiscard]] VoidResult
        validateObjectMembers(const ArchiveValue& value, std::string_view context,
                              std::span<const std::string_view> allowedMembers) {
            if (value.kind != ArchiveValueKind::Object) {
                return std::unexpected{
                    sceneIoError(std::string{context} + " must be a JSON object.")};
            }
            for (const ArchiveMember& member : value.objectValue) {
                if (!containsName(allowedMembers, member.key)) {
                    return std::unexpected{sceneIoError(
                        std::string{context} + " contains unknown member '" + member.key + "'.")};
                }
            }
            return {};
        }

        [[nodiscard]] Result<const ArchiveValue*> requiredMember(const ArchiveValue& object,
                                                                 std::string_view memberName,
                                                                 ArchiveValueKind expectedKind,
                                                                 std::string_view context) {
            const ArchiveValue* value = object.findMemberValue(memberName);
            if (value == nullptr) {
                return std::unexpected{sceneIoError(std::string{context} +
                                                    " is missing required member '" +
                                                    std::string{memberName} + "'.")};
            }
            if (value->kind != expectedKind) {
                return std::unexpected{sceneIoError(std::string{context} + " member '" +
                                                    std::string{memberName} +
                                                    "' has an unexpected type.")};
            }
            return value;
        }

        [[nodiscard]] Result<std::string> requiredString(const ArchiveValue& object,
                                                         std::string_view memberName,
                                                         std::string_view context) {
            auto value = requiredMember(object, memberName, ArchiveValueKind::String, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            return (*value)->stringValue;
        }

        [[nodiscard]] Result<std::uint32_t> requiredUint32(const ArchiveValue& object,
                                                           std::string_view memberName,
                                                           std::string_view context) {
            auto value = requiredMember(object, memberName, ArchiveValueKind::Integer, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            if ((*value)->integerValue <= 0 ||
                (*value)->integerValue >
                    static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
                return std::unexpected{sceneIoError(std::string{context} + " member '" +
                                                    std::string{memberName} +
                                                    "' must be a positive uint32 value.")};
            }
            return static_cast<std::uint32_t>((*value)->integerValue);
        }

        [[nodiscard]] Result<float> numberValue(const ArchiveValue& value,
                                                std::string_view context) {
            double number = 0.0;
            if (value.kind == ArchiveValueKind::Float) {
                number = value.floatValue;
            } else if (value.kind == ArchiveValueKind::Integer) {
                number = static_cast<double>(value.integerValue);
            } else {
                return std::unexpected{
                    sceneIoError(std::string{context} + " must contain numeric values.")};
            }
            if (number < -static_cast<double>(std::numeric_limits<float>::max()) ||
                number > static_cast<double>(std::numeric_limits<float>::max())) {
                return std::unexpected{
                    sceneIoError(std::string{context} + " contains an out-of-range float value.")};
            }
            return static_cast<float>(number);
        }

        [[nodiscard]] Result<Vec3> readVec3(const ArchiveValue& object, std::string_view memberName,
                                            std::string_view context) {
            auto value = requiredMember(object, memberName, ArchiveValueKind::Array, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            if ((*value)->arrayValue.size() != 3U) {
                return std::unexpected{sceneIoError(std::string{context} + " member '" +
                                                    std::string{memberName} +
                                                    "' must contain exactly three numbers.")};
            }
            auto xComponent = numberValue((*value)->arrayValue[0], context);
            auto yComponent = numberValue((*value)->arrayValue[1], context);
            auto zComponent = numberValue((*value)->arrayValue[2], context);
            if (!xComponent) {
                return std::unexpected{std::move(xComponent.error())};
            }
            if (!yComponent) {
                return std::unexpected{std::move(yComponent.error())};
            }
            if (!zComponent) {
                return std::unexpected{std::move(zComponent.error())};
            }
            return Vec3{.x = *xComponent, .y = *yComponent, .z = *zComponent};
        }

        [[nodiscard]] Result<Quat> readQuat(const ArchiveValue& object, std::string_view memberName,
                                            std::string_view context) {
            auto value = requiredMember(object, memberName, ArchiveValueKind::Array, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            if ((*value)->arrayValue.size() != 4U) {
                return std::unexpected{sceneIoError(std::string{context} + " member '" +
                                                    std::string{memberName} +
                                                    "' must contain exactly four numbers.")};
            }
            auto xComponent = numberValue((*value)->arrayValue[0], context);
            auto yComponent = numberValue((*value)->arrayValue[1], context);
            auto zComponent = numberValue((*value)->arrayValue[2], context);
            auto wComponent = numberValue((*value)->arrayValue[3], context);
            if (!xComponent) {
                return std::unexpected{std::move(xComponent.error())};
            }
            if (!yComponent) {
                return std::unexpected{std::move(yComponent.error())};
            }
            if (!zComponent) {
                return std::unexpected{std::move(zComponent.error())};
            }
            if (!wComponent) {
                return std::unexpected{std::move(wComponent.error())};
            }
            return Quat{.x = *xComponent, .y = *yComponent, .z = *zComponent, .w = *wComponent};
        }

        [[nodiscard]] ArchiveValue vec3Value(Vec3 value) {
            return ArchiveValue::array({
                ArchiveValue::floating(value.x),
                ArchiveValue::floating(value.y),
                ArchiveValue::floating(value.z),
            });
        }

        [[nodiscard]] ArchiveValue quatValue(Quat value) {
            return ArchiveValue::array({
                ArchiveValue::floating(value.x),
                ArchiveValue::floating(value.y),
                ArchiveValue::floating(value.z),
                ArchiveValue::floating(value.w),
            });
        }

        [[nodiscard]] ArchiveValue transformValue(const TransformComponent& transform) {
            return ArchiveValue::object({
                ArchiveMember{.key = "position", .value = vec3Value(transform.position)},
                ArchiveMember{.key = "rotation", .value = quatValue(transform.rotation)},
                ArchiveMember{.key = "scale", .value = vec3Value(transform.scale)},
            });
        }

        [[nodiscard]] ArchiveValue documentValue(const SceneDocumentData& data) {
            std::vector<ArchiveValue> entities;
            entities.reserve(data.entities.size());
            for (const SceneEntityData& entity : data.entities) {
                entities.push_back(ArchiveValue::object({
                    ArchiveMember{
                        .key = "id",
                        .value = ArchiveValue::string(formatSceneObjectId(entity.objectId)),
                    },
                    ArchiveMember{
                        .key = "name",
                        .value = ArchiveValue::string(entity.name),
                    },
                    ArchiveMember{
                        .key = "transform",
                        .value = transformValue(entity.transform),
                    },
                }));
            }
            return ArchiveValue::object({
                ArchiveMember{
                    .key = "schema",
                    .value = ArchiveValue::string(std::string{kAshariaSceneSchema}),
                },
                ArchiveMember{
                    .key = "schemaVersion",
                    .value = ArchiveValue::integer(kAshariaSceneSchemaVersion),
                },
                ArchiveMember{
                    .key = "sceneId",
                    .value = ArchiveValue::string(formatSceneId(data.sceneId)),
                },
                ArchiveMember{
                    .key = "entities",
                    .value = ArchiveValue::array(std::move(entities)),
                },
            });
        }

        [[nodiscard]] Result<TransformComponent> readTransform(const ArchiveValue& value,
                                                               std::string_view context) {
            constexpr std::array members{"position"sv, "rotation"sv, "scale"sv};
            if (auto valid = validateObjectMembers(value, context, members); !valid) {
                return std::unexpected{std::move(valid.error())};
            }
            auto position = readVec3(value, "position", context);
            auto rotation = readQuat(value, "rotation", context);
            auto scale = readVec3(value, "scale", context);
            if (!position) {
                return std::unexpected{std::move(position.error())};
            }
            if (!rotation) {
                return std::unexpected{std::move(rotation.error())};
            }
            if (!scale) {
                return std::unexpected{std::move(scale.error())};
            }
            return TransformComponent{
                .position = *position, .rotation = *rotation, .scale = *scale};
        }

        [[nodiscard]] Result<SceneEntityData> readEntity(const ArchiveValue& value,
                                                         std::size_t index) {
            const std::string context = "Scene entity[" + std::to_string(index) + "]";
            constexpr std::array members{"id"sv, "name"sv, "transform"sv};
            if (auto valid = validateObjectMembers(value, context, members); !valid) {
                return std::unexpected{std::move(valid.error())};
            }
            auto idText = requiredString(value, "id", context);
            auto name = requiredString(value, "name", context);
            auto transformArchive =
                requiredMember(value, "transform", ArchiveValueKind::Object, context);
            if (!idText) {
                return std::unexpected{std::move(idText.error())};
            }
            if (!name) {
                return std::unexpected{std::move(name.error())};
            }
            if (!transformArchive) {
                return std::unexpected{std::move(transformArchive.error())};
            }
            auto objectId = parseSceneObjectId(*idText);
            auto transform = readTransform(**transformArchive, context + " Transform");
            if (!objectId) {
                return std::unexpected{std::move(objectId.error())};
            }
            if (!transform) {
                return std::unexpected{std::move(transform.error())};
            }
            return SceneEntityData{
                .objectId = *objectId,
                .name = std::move(*name),
                .transform = *transform,
            };
        }

    } // namespace

    Result<std::string> writeSceneDocumentText(const SceneDocumentData& data) {
        if (auto valid = validateSceneDocumentData(data); !valid) {
            return std::unexpected{std::move(valid.error())};
        }
        auto text = archive::writeJsonArchive(documentValue(data));
        if (!text) {
            return std::unexpected{
                sceneIoError("Failed to serialize scene document: " + text.error().message)};
        }
        if (text->size() > kMaxSceneDocumentBytes) {
            return std::unexpected{
                sceneIoError("Serialized scene document exceeds the 64 MiB persistence limit.")};
        }
        return text;
    }

    VoidResult writeSceneDocumentFile(const std::filesystem::path& path,
                                      const SceneDocumentData& data) {
        auto text = writeSceneDocumentText(data);
        if (!text) {
            return std::unexpected{std::move(text.error())};
        }
        auto written = core::writeFileTextAtomically(path, *text);
        if (!written) {
            return std::unexpected{
                sceneIoError("Failed to write scene document: " + written.error().message)};
        }
        return {};
    }

    Result<SceneDocumentData> readSceneDocumentText(std::string_view text) {
        auto archiveValue = archive::readJsonArchive(text);
        if (!archiveValue) {
            return std::unexpected{
                sceneIoError("Failed to parse scene document: " + archiveValue.error().message)};
        }
        constexpr std::array rootMembers{"schema"sv, "schemaVersion"sv, "sceneId"sv, "entities"sv};
        if (auto valid = validateObjectMembers(*archiveValue, "Scene document", rootMembers);
            !valid) {
            return std::unexpected{std::move(valid.error())};
        }

        auto schema = requiredString(*archiveValue, "schema", "Scene document");
        auto schemaVersion = requiredUint32(*archiveValue, "schemaVersion", "Scene document");
        auto sceneIdText = requiredString(*archiveValue, "sceneId", "Scene document");
        auto entities =
            requiredMember(*archiveValue, "entities", ArchiveValueKind::Array, "Scene document");
        if (!schema) {
            return std::unexpected{std::move(schema.error())};
        }
        if (!schemaVersion) {
            return std::unexpected{std::move(schemaVersion.error())};
        }
        if (!sceneIdText) {
            return std::unexpected{std::move(sceneIdText.error())};
        }
        if (!entities) {
            return std::unexpected{std::move(entities.error())};
        }
        if (*schema != kAshariaSceneSchema || *schemaVersion != kAshariaSceneSchemaVersion) {
            return std::unexpected{
                sceneIoError("Scene document schema or version is unsupported.")};
        }
        if ((*entities)->arrayValue.size() > kMaxSceneEntities) {
            return std::unexpected{
                sceneIoError("Scene document entity count exceeds the configured limit.")};
        }

        auto sceneId = parseSceneId(*sceneIdText);
        if (!sceneId) {
            return std::unexpected{std::move(sceneId.error())};
        }
        SceneDocumentData data{.sceneId = *sceneId, .entities = {}};
        data.entities.reserve((*entities)->arrayValue.size());
        for (std::size_t index = 0; index < (*entities)->arrayValue.size(); ++index) {
            auto entity = readEntity((*entities)->arrayValue[index], index);
            if (!entity) {
                return std::unexpected{std::move(entity.error())};
            }
            data.entities.push_back(std::move(*entity));
        }
        if (auto valid = validateSceneDocumentData(data); !valid) {
            return std::unexpected{std::move(valid.error())};
        }
        return data;
    }

    Result<SceneDocumentData> readSceneDocumentFile(const std::filesystem::path& path) {
        auto archiveValue =
            archive::readJsonArchiveFile(path, {.maxBytes = kMaxSceneDocumentBytes});
        if (!archiveValue) {
            return std::unexpected{
                sceneIoError("Failed to read scene document: " + archiveValue.error().message)};
        }
        auto text = archive::writeJsonArchive(*archiveValue);
        if (!text) {
            return std::unexpected{
                sceneIoError("Failed to normalize scene document: " + text.error().message)};
        }
        return readSceneDocumentText(*text);
    }

} // namespace asharia::scene
