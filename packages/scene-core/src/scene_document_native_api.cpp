#include "asharia/scene/scene_document_native_api.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "asharia/core/error.hpp"
#include "asharia/scene/scene_document.hpp"

namespace {

    struct DocumentSlot {
        std::unique_ptr<asharia::scene::SceneDocument> document;
        std::thread::id ownerThread;
        std::uint32_t generation{1U};
    };

    struct DocumentRegistry {
        std::mutex mutex;
        std::vector<DocumentSlot> slots;
    };

    struct DocumentRevisionState {
        std::uint64_t revision;
        std::uint64_t savedRevision;
    };

    constexpr DocumentRevisionState kEmptyRevisionState{};

    [[nodiscard]] DocumentRegistry& documentRegistry() {
        static DocumentRegistry registry;
        return registry;
    }

    [[nodiscard]] constexpr bool hasSupportedHeader(const AshariaSceneNativeAbiHeader& header,
                                                    std::size_t requiredSize) noexcept {
        return header.abiVersion == ASHARIA_SCENE_DOCUMENT_NATIVE_ABI_VERSION &&
               header.structSize >= requiredSize;
    }

    [[nodiscard]] bool isValidUtf8(std::string_view text) noexcept {
        std::size_t index = 0U;
        while (index < text.size()) {
            const auto leading = static_cast<unsigned char>(text[index]);
            std::size_t continuationCount = 0U;
            std::uint32_t codePoint = 0U;
            if (leading <= 0x7FU) {
                ++index;
                continue;
            }
            if (leading >= 0xC2U && leading <= 0xDFU) {
                continuationCount = 1U;
                codePoint = leading & 0x1FU;
            } else if (leading >= 0xE0U && leading <= 0xEFU) {
                continuationCount = 2U;
                codePoint = leading & 0x0FU;
            } else if (leading >= 0xF0U && leading <= 0xF4U) {
                continuationCount = 3U;
                codePoint = leading & 0x07U;
            } else {
                return false;
            }
            if (continuationCount > text.size() - index - 1U) {
                return false;
            }
            for (std::size_t offset = 1U; offset <= continuationCount; ++offset) {
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

    [[nodiscard]] AshariaSceneNativeStatus makeUtf8View(AshariaSceneNativeStringView value,
                                                        std::uint64_t maximumLength,
                                                        std::string_view& output) noexcept {
        output = {};
        if (value.byteLength > maximumLength ||
            value.byteLength > std::numeric_limits<std::size_t>::max() ||
            (value.data == nullptr && value.byteLength != 0U)) {
            return AshariaSceneNativeStatus_InvalidArgument;
        }
        if (value.byteLength == 0U) {
            return AshariaSceneNativeStatus_Success;
        }
        output = std::string_view{value.data, static_cast<std::size_t>(value.byteLength)};
        return isValidUtf8(output) ? AshariaSceneNativeStatus_Success
                                   : AshariaSceneNativeStatus_InvalidUtf8;
    }

    [[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text) {
        std::u8string utf8;
        utf8.reserve(text.size());
        for (char byte : text) {
            utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
        }
        return std::filesystem::path{utf8};
    }

    [[nodiscard]] constexpr AshariaSceneNativeTransform
    fromTransform(const asharia::TransformComponent& transform) noexcept {
        return AshariaSceneNativeTransform{
            .position = {transform.position.x, transform.position.y, transform.position.z},
            .rotation = {transform.rotation.x, transform.rotation.y, transform.rotation.z,
                         transform.rotation.w},
            .scale = {transform.scale.x, transform.scale.y, transform.scale.z},
        };
    }

    [[nodiscard]] constexpr asharia::TransformComponent
    toTransform(const AshariaSceneNativeTransform& transform) noexcept {
        return asharia::TransformComponent{
            .position = {.x = transform.position.x,
                         .y = transform.position.y,
                         .z = transform.position.z},
            .rotation = {.x = transform.rotation.x,
                         .y = transform.rotation.y,
                         .z = transform.rotation.z,
                         .w = transform.rotation.w},
            .scale = {.x = transform.scale.x, .y = transform.scale.y, .z = transform.scale.z},
        };
    }

    [[nodiscard]] AshariaSceneNativeStatus statusFromError(const asharia::Error& error) noexcept {
        if (error.domain != asharia::ErrorDomain::Scene) {
            return AshariaSceneNativeStatus_InternalError;
        }
        using asharia::scene::SceneDocumentErrorCode;
        switch (static_cast<SceneDocumentErrorCode>(error.code)) {
        case SceneDocumentErrorCode::InvalidScene:
            return AshariaSceneNativeStatus_InvalidScene;
        case SceneDocumentErrorCode::SceneIo:
            return AshariaSceneNativeStatus_IoFailure;
        case SceneDocumentErrorCode::RevisionConflict:
            return AshariaSceneNativeStatus_RevisionConflict;
        case SceneDocumentErrorCode::DuplicateObjectId:
            return AshariaSceneNativeStatus_DuplicateObject;
        case SceneDocumentErrorCode::InvalidObjectId:
            return AshariaSceneNativeStatus_InvalidObject;
        case SceneDocumentErrorCode::InvalidTransform:
            return AshariaSceneNativeStatus_InvalidTransform;
        case SceneDocumentErrorCode::InvalidAssetReference:
            return AshariaSceneNativeStatus_InvalidAssetReference;
        case SceneDocumentErrorCode::RevisionExhausted:
            return AshariaSceneNativeStatus_RevisionExhausted;
        }
        return AshariaSceneNativeStatus_InternalError;
    }

    [[nodiscard]] bool invalidResponseBuffer(void* responseBuffer,
                                             std::uint64_t responseCapacity) noexcept {
        return (responseBuffer == nullptr && responseCapacity != 0U) ||
               responseCapacity > std::numeric_limits<std::size_t>::max();
    }

    [[nodiscard]] AshariaSceneNativeStatus
    finishOperation(AshariaSceneNativeStatus operationStatus, std::string_view message,
                    DocumentRevisionState revisionState, void* responseBuffer,
                    std::uint64_t responseCapacity,
                    AshariaSceneNativeDocumentOperationResult& result) {
        result.operationStatus = operationStatus;
        result.requiredBufferSize = static_cast<std::uint64_t>(message.size());
        result.revision = revisionState.revision;
        result.savedRevision = revisionState.savedRevision;
        result.messageUtf8 = {.offset = 0U,
                              .byteLength = static_cast<std::uint64_t>(message.size())};
        if (responseCapacity < result.requiredBufferSize ||
            (result.requiredBufferSize != 0U && responseBuffer == nullptr)) {
            return AshariaSceneNativeStatus_BufferTooSmall;
        }
        if (!message.empty()) {
            std::memcpy(responseBuffer, message.data(), message.size());
        }
        return operationStatus;
    }

    [[nodiscard]] AshariaSceneNativeStatus finishTransformOperation(
        AshariaSceneNativeStatus operationStatus, std::string_view message,
        DocumentRevisionState revisionState,
        const asharia::scene::SceneEntityTransformReceipt* receipt, void* responseBuffer,
        std::uint64_t responseCapacity,
        AshariaSceneNativeDocumentTransformOperationResult& result) {
        result.operationStatus = operationStatus;
        result.requiredBufferSize = static_cast<std::uint64_t>(message.size());
        result.revision = revisionState.revision;
        result.savedRevision = revisionState.savedRevision;
        result.messageUtf8 = {.offset = 0U,
                              .byteLength = static_cast<std::uint64_t>(message.size())};
        if (receipt != nullptr) {
            result.changed = receipt->changed ? 1U : 0U;
            std::memcpy(result.objectId.bytes, receipt->objectId.bytes.data(),
                        receipt->objectId.bytes.size());
            result.beforeTransform = fromTransform(receipt->before);
            result.afterTransform = fromTransform(receipt->after);
            result.beforeRevision = receipt->beforeRevision;
            result.afterRevision = receipt->afterRevision;
        }
        if (responseCapacity < result.requiredBufferSize ||
            (result.requiredBufferSize != 0U && responseBuffer == nullptr)) {
            return AshariaSceneNativeStatus_BufferTooSmall;
        }
        if (!message.empty()) {
            std::memcpy(responseBuffer, message.data(), message.size());
        }
        return operationStatus;
    }

    [[nodiscard]] DocumentSlot* findDocumentSlot(AshariaSceneNativeDocumentHandle handle,
                                                 AshariaSceneNativeStatus& status) noexcept {
        if (handle.index == 0U || handle.generation == 0U ||
            handle.index > documentRegistry().slots.size()) {
            status = AshariaSceneNativeStatus_StaleHandle;
            return nullptr;
        }
        DocumentSlot& slot = documentRegistry().slots[handle.index - 1U];
        if (slot.document == nullptr || slot.generation != handle.generation) {
            status = AshariaSceneNativeStatus_StaleHandle;
            return nullptr;
        }
        if (slot.ownerThread != std::this_thread::get_id()) {
            status = AshariaSceneNativeStatus_WrongThread;
            return nullptr;
        }
        status = AshariaSceneNativeStatus_Success;
        return &slot;
    }

    [[nodiscard]] AshariaSceneNativeStatus
    insertDocument(asharia::scene::SceneDocument document,
                   AshariaSceneNativeDocumentHandle& handle) {
        DocumentRegistry& registry = documentRegistry();
        std::scoped_lock lock{registry.mutex};
        for (std::size_t index = 0U; index < registry.slots.size(); ++index) {
            DocumentSlot& slot = registry.slots[index];
            if (slot.document == nullptr) {
                slot.document =
                    std::make_unique<asharia::scene::SceneDocument>(std::move(document));
                slot.ownerThread = std::this_thread::get_id();
                handle = {.index = static_cast<std::uint32_t>(index + 1U),
                          .generation = slot.generation};
                return AshariaSceneNativeStatus_Success;
            }
        }
        if (registry.slots.size() >= std::numeric_limits<std::uint32_t>::max()) {
            return AshariaSceneNativeStatus_InternalError;
        }
        DocumentSlot slot;
        slot.document = std::make_unique<asharia::scene::SceneDocument>(std::move(document));
        slot.ownerThread = std::this_thread::get_id();
        registry.slots.push_back(std::move(slot));
        handle = {.index = static_cast<std::uint32_t>(registry.slots.size()),
                  .generation = registry.slots.back().generation};
        return AshariaSceneNativeStatus_Success;
    }

    [[nodiscard]] bool checkedAdd(std::uint64_t value, std::uint64_t amount,
                                  std::uint64_t& result) noexcept {
        if (amount > std::numeric_limits<std::uint64_t>::max() - value) {
            return false;
        }
        result = value + amount;
        return true;
    }

    [[nodiscard]] AshariaSceneNativeStatus
    finishSnapshot(const asharia::scene::SceneDocumentSnapshot& snapshot, void* responseBuffer,
                   std::uint64_t responseCapacity,
                   AshariaSceneNativeDocumentSnapshotResult& result) {
        const std::string sceneId = asharia::scene::formatSceneId(snapshot.data.sceneId);
        const auto entityCount = static_cast<std::uint64_t>(snapshot.data.entities.size());
        if (snapshot.runtimeEntities.size() != snapshot.data.entities.size() ||
            entityCount > std::numeric_limits<std::uint64_t>::max() /
                              sizeof(AshariaSceneNativeDocumentEntitySnapshot)) {
            result.operationStatus = AshariaSceneNativeStatus_InternalError;
            return AshariaSceneNativeStatus_InternalError;
        }

        std::uint64_t required = entityCount * sizeof(AshariaSceneNativeDocumentEntitySnapshot);
        if (!checkedAdd(required, sceneId.size(), required)) {
            result.operationStatus = AshariaSceneNativeStatus_InternalError;
            return AshariaSceneNativeStatus_InternalError;
        }
        for (std::size_t index = 0U; index < snapshot.data.entities.size(); ++index) {
            const asharia::scene::SceneEntityData& entity = snapshot.data.entities[index];
            const asharia::scene::SceneDocumentSnapshot::RuntimeEntityBinding& runtime =
                snapshot.runtimeEntities[index];
            if (runtime.objectId != entity.objectId || !asharia::isValid(runtime.entity)) {
                result.operationStatus = AshariaSceneNativeStatus_InternalError;
                return AshariaSceneNativeStatus_InternalError;
            }
            if (!checkedAdd(required, 36U, required) ||
                !checkedAdd(required, entity.name.size(), required) ||
                (entity.mesh.has_value() && !checkedAdd(required, 36U, required))) {
                result.operationStatus = AshariaSceneNativeStatus_InternalError;
                return AshariaSceneNativeStatus_InternalError;
            }
        }

        result.operationStatus = AshariaSceneNativeStatus_Success;
        result.requiredBufferSize = required;
        result.revision = snapshot.revision;
        result.savedRevision = snapshot.savedRevision;
        result.entityCount = entityCount;
        result.entitiesOffset = 0U;
        result.messageUtf8 = {};
        if (responseCapacity < required || (required != 0U && responseBuffer == nullptr)) {
            return AshariaSceneNativeStatus_BufferTooSmall;
        }

        const auto responseSize = static_cast<std::size_t>(responseCapacity);
        std::span<std::byte> bytes{static_cast<std::byte*>(responseBuffer), responseSize};
        std::uint64_t cursor = entityCount * sizeof(AshariaSceneNativeDocumentEntitySnapshot);
        result.sceneIdUtf8 = {.offset = cursor,
                              .byteLength = static_cast<std::uint64_t>(sceneId.size())};
        std::memcpy(bytes.subspan(static_cast<std::size_t>(cursor), sceneId.size()).data(),
                    sceneId.data(), sceneId.size());
        cursor += sceneId.size();

        for (std::size_t index = 0U; index < snapshot.data.entities.size(); ++index) {
            const asharia::scene::SceneEntityData& entity = snapshot.data.entities[index];
            const asharia::scene::SceneDocumentSnapshot::RuntimeEntityBinding& runtime =
                snapshot.runtimeEntities[index];
            const std::string objectId = asharia::scene::formatSceneObjectId(entity.objectId);
            AshariaSceneNativeDocumentEntitySnapshot nativeEntity{
                .objectIdUtf8 = {.offset = cursor,
                                 .byteLength = static_cast<std::uint64_t>(objectId.size())},
                .nameUtf8 = {},
                .transform = fromTransform(entity.transform),
                .runtimeEntity = {.index = runtime.entity.index,
                                  .generation = runtime.entity.generation},
                .meshAssetGuidUtf8 = {},
            };
            std::memcpy(bytes.subspan(static_cast<std::size_t>(cursor), objectId.size()).data(),
                        objectId.data(), objectId.size());
            cursor += objectId.size();
            nativeEntity.nameUtf8 = {
                .offset = cursor,
                .byteLength = static_cast<std::uint64_t>(entity.name.size()),
            };
            if (!entity.name.empty()) {
                std::memcpy(
                    bytes.subspan(static_cast<std::size_t>(cursor), entity.name.size()).data(),
                    entity.name.data(), entity.name.size());
            }
            cursor += entity.name.size();
            if (entity.mesh.has_value()) {
                const std::string meshAssetGuid =
                    asharia::asset::formatAssetGuid(entity.mesh->guid);
                nativeEntity.meshAssetGuidUtf8 = {
                    .offset = cursor,
                    .byteLength = static_cast<std::uint64_t>(meshAssetGuid.size()),
                };
                std::memcpy(
                    bytes.subspan(static_cast<std::size_t>(cursor), meshAssetGuid.size()).data(),
                    meshAssetGuid.data(), meshAssetGuid.size());
                cursor += meshAssetGuid.size();
            }
            const std::uint64_t entryOffset = static_cast<std::uint64_t>(index) *
                                              sizeof(AshariaSceneNativeDocumentEntitySnapshot);
            std::memcpy(
                bytes.subspan(static_cast<std::size_t>(entryOffset), sizeof(nativeEntity)).data(),
                &nativeEntity, sizeof(nativeEntity));
        }
        return AshariaSceneNativeStatus_Success;
    }

    [[nodiscard]] AshariaSceneNativeStatus
    finishSnapshotError(AshariaSceneNativeStatus operationStatus, std::string_view message,
                        void* responseBuffer, std::uint64_t responseCapacity,
                        AshariaSceneNativeDocumentSnapshotResult& result) {
        result.operationStatus = operationStatus;
        result.requiredBufferSize = static_cast<std::uint64_t>(message.size());
        result.messageUtf8 = {.offset = 0U,
                              .byteLength = static_cast<std::uint64_t>(message.size())};
        if (responseCapacity < result.requiredBufferSize ||
            (result.requiredBufferSize != 0U && responseBuffer == nullptr)) {
            return AshariaSceneNativeStatus_BufferTooSmall;
        }
        if (!message.empty()) {
            std::memcpy(responseBuffer, message.data(), message.size());
        }
        return operationStatus;
    }

    static_assert(std::is_standard_layout_v<AshariaSceneNativeDocumentHandle>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeDocumentHandle>);
    static_assert(sizeof(AshariaSceneNativeDocumentHandle) == 8U);
    static_assert(sizeof(AshariaSceneNativeTextSpan) == 16U);
    static_assert(sizeof(AshariaSceneNativeDocumentOpenDefaultRequest) == 40U);
    static_assert(sizeof(AshariaSceneNativeDocumentRequest) == 16U);
    static_assert(sizeof(AshariaSceneNativeDocumentCreateEntityRequest) == 56U);
    static_assert(sizeof(AshariaSceneNativeDocumentCreateMeshEntityRequest) == 72U);
    static_assert(sizeof(AshariaSceneNativeDocumentSetEntityNameRequest) == 56U);
    static_assert(sizeof(AshariaSceneNativeDocumentSetEntityTransformRequest) == 80U);
    static_assert(sizeof(AshariaSceneNativeDocumentSaveRequest) == 24U);
    static_assert(sizeof(AshariaSceneNativeDocumentOperationResult) == 48U);
    static_assert(sizeof(AshariaSceneNativeObjectId) == 16U);
    static_assert(sizeof(AshariaSceneNativeDocumentTransformOperationResult) == 160U);
    static_assert(offsetof(AshariaSceneNativeDocumentTransformOperationResult, objectId) == 32U);
    static_assert(offsetof(AshariaSceneNativeDocumentTransformOperationResult, beforeTransform) ==
                  48U);
    static_assert(offsetof(AshariaSceneNativeDocumentTransformOperationResult, afterTransform) ==
                  88U);
    static_assert(offsetof(AshariaSceneNativeDocumentTransformOperationResult, beforeRevision) ==
                  128U);
    static_assert(offsetof(AshariaSceneNativeDocumentTransformOperationResult, messageUtf8) ==
                  144U);
    static_assert(sizeof(AshariaSceneNativeDocumentEntitySnapshot) == 96U);
    static_assert(sizeof(AshariaSceneNativeDocumentSnapshotResult) == 80U);

} // namespace

extern "C" {

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_document_open_default(
    const AshariaSceneNativeDocumentOpenDefaultRequest* request,
    AshariaSceneNativeDocumentHandle* document, void* responseBuffer,
    std::uint64_t responseCapacity, AshariaSceneNativeDocumentOperationResult* result) noexcept {
    if (document == nullptr || result == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    *document = {};
    *result = {};
    if (invalidResponseBuffer(responseBuffer, responseCapacity) || request == nullptr) {
        result->operationStatus = AshariaSceneNativeStatus_InvalidArgument;
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header,
                            sizeof(AshariaSceneNativeDocumentOpenDefaultRequest))) {
        result->operationStatus = AshariaSceneNativeStatus_UnsupportedAbi;
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }

    std::string_view projectRoot;
    AshariaSceneNativeStatus inputStatus = makeUtf8View(
        request->projectRootUtf8, ASHARIA_SCENE_NATIVE_MAX_PROJECT_PATH_UTF8_BYTES, projectRoot);
    if (inputStatus != AshariaSceneNativeStatus_Success || projectRoot.empty()) {
        result->operationStatus = inputStatus == AshariaSceneNativeStatus_Success
                                      ? AshariaSceneNativeStatus_InvalidArgument
                                      : inputStatus;
        return result->operationStatus;
    }
    std::string_view newSceneIdText;
    inputStatus = makeUtf8View(request->newSceneIdUtf8, 36U, newSceneIdText);
    if (inputStatus != AshariaSceneNativeStatus_Success) {
        result->operationStatus = inputStatus;
        return inputStatus;
    }

    try {
        asharia::scene::SceneId newSceneId{};
        if (!newSceneIdText.empty()) {
            auto parsed = asharia::scene::parseSceneId(newSceneIdText);
            if (!parsed) {
                return finishOperation(AshariaSceneNativeStatus_InvalidScene,
                                       parsed.error().message, kEmptyRevisionState, responseBuffer,
                                       responseCapacity, *result);
            }
            newSceneId = *parsed;
        }
        auto opened = asharia::scene::SceneDocument::openOrCreateDefault(pathFromUtf8(projectRoot),
                                                                         newSceneId);
        if (!opened) {
            return finishOperation(statusFromError(opened.error()), opened.error().message,
                                   kEmptyRevisionState, responseBuffer, responseCapacity, *result);
        }
        const asharia::scene::SceneDocumentSnapshot snapshot = opened->snapshot();
        const AshariaSceneNativeStatus inserted = insertDocument(std::move(*opened), *document);
        if (inserted != AshariaSceneNativeStatus_Success) {
            return finishOperation(inserted, "Could not allocate a native scene document handle.",
                                   kEmptyRevisionState, responseBuffer, responseCapacity, *result);
        }
        return finishOperation(
            AshariaSceneNativeStatus_Success, {},
            {.revision = snapshot.revision, .savedRevision = snapshot.savedRevision},
            responseBuffer, responseCapacity, *result);
    } catch (...) {
        return finishOperation(AshariaSceneNativeStatus_InternalError,
                               "Native scene document open failed unexpectedly.",
                               kEmptyRevisionState, responseBuffer, responseCapacity, *result);
    }
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_document_close(AshariaSceneNativeDocumentHandle* document) noexcept {
    if (document == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    try {
        std::scoped_lock lock{documentRegistry().mutex};
        AshariaSceneNativeStatus status = AshariaSceneNativeStatus_Success;
        DocumentSlot* slot = findDocumentSlot(*document, status);
        if (slot == nullptr) {
            return status;
        }
        slot->document.reset();
        slot->ownerThread = {};
        ++slot->generation;
        if (slot->generation == 0U) {
            slot->generation = 1U;
        }
        *document = {};
        return AshariaSceneNativeStatus_Success;
    } catch (...) {
        return AshariaSceneNativeStatus_InternalError;
    }
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_document_snapshot(
    const AshariaSceneNativeDocumentRequest* request, void* responseBuffer,
    std::uint64_t responseCapacity, AshariaSceneNativeDocumentSnapshotResult* result) noexcept {
    if (result == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    *result = {};
    if (invalidResponseBuffer(responseBuffer, responseCapacity) || request == nullptr) {
        result->operationStatus = AshariaSceneNativeStatus_InvalidArgument;
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header, sizeof(AshariaSceneNativeDocumentRequest))) {
        result->operationStatus = AshariaSceneNativeStatus_UnsupportedAbi;
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }
    try {
        asharia::scene::SceneDocumentSnapshot snapshot;
        {
            std::scoped_lock lock{documentRegistry().mutex};
            AshariaSceneNativeStatus status = AshariaSceneNativeStatus_Success;
            DocumentSlot* slot = findDocumentSlot(request->document, status);
            if (slot == nullptr) {
                return finishSnapshotError(status,
                                           "Scene document handle is invalid for this call.",
                                           responseBuffer, responseCapacity, *result);
            }
            snapshot = slot->document->snapshot();
        }
        return finishSnapshot(snapshot, responseBuffer, responseCapacity, *result);
    } catch (...) {
        return finishSnapshotError(AshariaSceneNativeStatus_InternalError,
                                   "Native scene document snapshot failed unexpectedly.",
                                   responseBuffer, responseCapacity, *result);
    }
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_document_create_entity(
    const AshariaSceneNativeDocumentCreateEntityRequest* request, void* responseBuffer,
    std::uint64_t responseCapacity, AshariaSceneNativeDocumentOperationResult* result) noexcept {
    if (result == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    *result = {};
    if (invalidResponseBuffer(responseBuffer, responseCapacity) || request == nullptr) {
        result->operationStatus = AshariaSceneNativeStatus_InvalidArgument;
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header,
                            sizeof(AshariaSceneNativeDocumentCreateEntityRequest))) {
        result->operationStatus = AshariaSceneNativeStatus_UnsupportedAbi;
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }
    std::string_view objectIdText;
    std::string_view name;
    AshariaSceneNativeStatus inputStatus = makeUtf8View(request->objectIdUtf8, 36U, objectIdText);
    if (inputStatus == AshariaSceneNativeStatus_Success) {
        inputStatus =
            makeUtf8View(request->nameUtf8, ASHARIA_SCENE_NATIVE_MAX_ENTITY_NAME_UTF8_BYTES, name);
    }
    if (inputStatus != AshariaSceneNativeStatus_Success) {
        result->operationStatus = inputStatus;
        return inputStatus;
    }
    try {
        auto objectId = asharia::scene::parseSceneObjectId(objectIdText);
        if (!objectId) {
            return finishOperation(AshariaSceneNativeStatus_InvalidObject, objectId.error().message,
                                   kEmptyRevisionState, responseBuffer, responseCapacity, *result);
        }
        std::scoped_lock lock{documentRegistry().mutex};
        AshariaSceneNativeStatus status = AshariaSceneNativeStatus_Success;
        DocumentSlot* slot = findDocumentSlot(request->document, status);
        if (slot == nullptr) {
            return finishOperation(status, "Scene document handle is invalid for this call.",
                                   kEmptyRevisionState, responseBuffer, responseCapacity, *result);
        }
        auto changed = slot->document->createEntity(*objectId, name, request->expectedRevision);
        const auto snapshot = slot->document->snapshot();
        const DocumentRevisionState revisionState{.revision = snapshot.revision,
                                                  .savedRevision = snapshot.savedRevision};
        return changed ? finishOperation(AshariaSceneNativeStatus_Success, {}, revisionState,
                                         responseBuffer, responseCapacity, *result)
                       : finishOperation(statusFromError(changed.error()), changed.error().message,
                                         revisionState, responseBuffer, responseCapacity, *result);
    } catch (...) {
        return finishOperation(AshariaSceneNativeStatus_InternalError,
                               "Native scene entity creation failed unexpectedly.",
                               kEmptyRevisionState, responseBuffer, responseCapacity, *result);
    }
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_document_create_mesh_entity(
    const AshariaSceneNativeDocumentCreateMeshEntityRequest* request, void* responseBuffer,
    std::uint64_t responseCapacity, AshariaSceneNativeDocumentOperationResult* result) noexcept {
    if (result == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    *result = {};
    if (invalidResponseBuffer(responseBuffer, responseCapacity) || request == nullptr) {
        result->operationStatus = AshariaSceneNativeStatus_InvalidArgument;
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header,
                            sizeof(AshariaSceneNativeDocumentCreateMeshEntityRequest))) {
        result->operationStatus = AshariaSceneNativeStatus_UnsupportedAbi;
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }
    std::string_view objectIdText;
    std::string_view name;
    std::string_view meshAssetGuidText;
    AshariaSceneNativeStatus inputStatus = makeUtf8View(request->objectIdUtf8, 36U, objectIdText);
    if (inputStatus == AshariaSceneNativeStatus_Success) {
        inputStatus =
            makeUtf8View(request->nameUtf8, ASHARIA_SCENE_NATIVE_MAX_ENTITY_NAME_UTF8_BYTES, name);
    }
    if (inputStatus == AshariaSceneNativeStatus_Success) {
        inputStatus = makeUtf8View(request->meshAssetGuidUtf8, 36U, meshAssetGuidText);
    }
    if (inputStatus != AshariaSceneNativeStatus_Success) {
        result->operationStatus = inputStatus;
        return inputStatus;
    }
    try {
        auto objectId = asharia::scene::parseSceneObjectId(objectIdText);
        if (!objectId) {
            return finishOperation(AshariaSceneNativeStatus_InvalidObject, objectId.error().message,
                                   kEmptyRevisionState, responseBuffer, responseCapacity, *result);
        }
        auto meshAssetGuid = asharia::asset::parseAssetGuid(meshAssetGuidText);
        if (!meshAssetGuid) {
            return finishOperation(AshariaSceneNativeStatus_InvalidAssetReference,
                                   meshAssetGuid.error().message, kEmptyRevisionState,
                                   responseBuffer, responseCapacity, *result);
        }
        std::scoped_lock lock{documentRegistry().mutex};
        AshariaSceneNativeStatus status = AshariaSceneNativeStatus_Success;
        DocumentSlot* slot = findDocumentSlot(request->document, status);
        if (slot == nullptr) {
            return finishOperation(status, "Scene document handle is invalid for this call.",
                                   kEmptyRevisionState, responseBuffer, responseCapacity, *result);
        }
        auto changed = slot->document->createMeshEntity(*objectId, name, *meshAssetGuid,
                                                        request->expectedRevision);
        const auto snapshot = slot->document->snapshot();
        const DocumentRevisionState revisionState{.revision = snapshot.revision,
                                                  .savedRevision = snapshot.savedRevision};
        return changed ? finishOperation(AshariaSceneNativeStatus_Success, {}, revisionState,
                                         responseBuffer, responseCapacity, *result)
                       : finishOperation(statusFromError(changed.error()), changed.error().message,
                                         revisionState, responseBuffer, responseCapacity, *result);
    } catch (...) {
        return finishOperation(AshariaSceneNativeStatus_InternalError,
                               "Native scene mesh entity creation failed unexpectedly.",
                               kEmptyRevisionState, responseBuffer, responseCapacity, *result);
    }
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_document_set_entity_name(
    const AshariaSceneNativeDocumentSetEntityNameRequest* request, void* responseBuffer,
    std::uint64_t responseCapacity, AshariaSceneNativeDocumentOperationResult* result) noexcept {
    if (result == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    *result = {};
    if (invalidResponseBuffer(responseBuffer, responseCapacity) || request == nullptr) {
        result->operationStatus = AshariaSceneNativeStatus_InvalidArgument;
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header,
                            sizeof(AshariaSceneNativeDocumentSetEntityNameRequest))) {
        result->operationStatus = AshariaSceneNativeStatus_UnsupportedAbi;
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }
    std::string_view objectIdText;
    std::string_view name;
    AshariaSceneNativeStatus inputStatus = makeUtf8View(request->objectIdUtf8, 36U, objectIdText);
    if (inputStatus == AshariaSceneNativeStatus_Success) {
        inputStatus =
            makeUtf8View(request->nameUtf8, ASHARIA_SCENE_NATIVE_MAX_ENTITY_NAME_UTF8_BYTES, name);
    }
    if (inputStatus != AshariaSceneNativeStatus_Success) {
        result->operationStatus = inputStatus;
        return inputStatus;
    }
    try {
        auto objectId = asharia::scene::parseSceneObjectId(objectIdText);
        if (!objectId) {
            return finishOperation(AshariaSceneNativeStatus_InvalidObject, objectId.error().message,
                                   kEmptyRevisionState, responseBuffer, responseCapacity, *result);
        }
        std::scoped_lock lock{documentRegistry().mutex};
        AshariaSceneNativeStatus status = AshariaSceneNativeStatus_Success;
        DocumentSlot* slot = findDocumentSlot(request->document, status);
        if (slot == nullptr) {
            return finishOperation(status, "Scene document handle is invalid for this call.",
                                   kEmptyRevisionState, responseBuffer, responseCapacity, *result);
        }
        auto changed = slot->document->setEntityName(*objectId, name, request->expectedRevision);
        const auto snapshot = slot->document->snapshot();
        const DocumentRevisionState revisionState{.revision = snapshot.revision,
                                                  .savedRevision = snapshot.savedRevision};
        return changed ? finishOperation(AshariaSceneNativeStatus_Success, {}, revisionState,
                                         responseBuffer, responseCapacity, *result)
                       : finishOperation(statusFromError(changed.error()), changed.error().message,
                                         revisionState, responseBuffer, responseCapacity, *result);
    } catch (...) {
        return finishOperation(AshariaSceneNativeStatus_InternalError,
                               "Native scene entity rename failed unexpectedly.",
                               kEmptyRevisionState, responseBuffer, responseCapacity, *result);
    }
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_document_set_entity_transform(
    const AshariaSceneNativeDocumentSetEntityTransformRequest* request, void* responseBuffer,
    std::uint64_t responseCapacity,
    AshariaSceneNativeDocumentTransformOperationResult* result) noexcept {
    if (result == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    *result = {};
    if (invalidResponseBuffer(responseBuffer, responseCapacity) || request == nullptr) {
        result->operationStatus = AshariaSceneNativeStatus_InvalidArgument;
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header,
                            sizeof(AshariaSceneNativeDocumentSetEntityTransformRequest))) {
        result->operationStatus = AshariaSceneNativeStatus_UnsupportedAbi;
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }
    std::string_view objectIdText;
    const AshariaSceneNativeStatus inputStatus =
        makeUtf8View(request->objectIdUtf8, 36U, objectIdText);
    if (inputStatus != AshariaSceneNativeStatus_Success) {
        result->operationStatus = inputStatus;
        return inputStatus;
    }
    try {
        auto objectId = asharia::scene::parseSceneObjectId(objectIdText);
        if (!objectId) {
            return finishTransformOperation(AshariaSceneNativeStatus_InvalidObject,
                                            objectId.error().message, kEmptyRevisionState, nullptr,
                                            responseBuffer, responseCapacity, *result);
        }
        std::scoped_lock lock{documentRegistry().mutex};
        AshariaSceneNativeStatus status = AshariaSceneNativeStatus_Success;
        DocumentSlot* slot = findDocumentSlot(request->document, status);
        if (slot == nullptr) {
            return finishTransformOperation(
                status, "Scene document handle is invalid for this call.", kEmptyRevisionState,
                nullptr, responseBuffer, responseCapacity, *result);
        }
        auto changed = slot->document->setEntityTransform(
            *objectId, toTransform(request->transform), request->expectedRevision);
        const auto snapshot = slot->document->snapshot();
        const DocumentRevisionState revisionState{.revision = snapshot.revision,
                                                  .savedRevision = snapshot.savedRevision};
        return changed ? finishTransformOperation(AshariaSceneNativeStatus_Success, {},
                                                  revisionState, &*changed, responseBuffer,
                                                  responseCapacity, *result)
                       : finishTransformOperation(statusFromError(changed.error()),
                                                  changed.error().message, revisionState, nullptr,
                                                  responseBuffer, responseCapacity, *result);
    } catch (...) {
        return finishTransformOperation(
            AshariaSceneNativeStatus_InternalError,
            "Native scene Transform edit failed unexpectedly.", kEmptyRevisionState, nullptr,
            responseBuffer, responseCapacity, *result);
    }
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_document_save(
    const AshariaSceneNativeDocumentSaveRequest* request, void* responseBuffer,
    std::uint64_t responseCapacity, AshariaSceneNativeDocumentOperationResult* result) noexcept {
    if (result == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    *result = {};
    if (invalidResponseBuffer(responseBuffer, responseCapacity) || request == nullptr) {
        result->operationStatus = AshariaSceneNativeStatus_InvalidArgument;
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header, sizeof(AshariaSceneNativeDocumentSaveRequest))) {
        result->operationStatus = AshariaSceneNativeStatus_UnsupportedAbi;
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }
    try {
        std::scoped_lock lock{documentRegistry().mutex};
        AshariaSceneNativeStatus status = AshariaSceneNativeStatus_Success;
        DocumentSlot* slot = findDocumentSlot(request->document, status);
        if (slot == nullptr) {
            return finishOperation(status, "Scene document handle is invalid for this call.",
                                   kEmptyRevisionState, responseBuffer, responseCapacity, *result);
        }
        auto saved = slot->document->save(request->expectedRevision);
        const auto snapshot = slot->document->snapshot();
        const DocumentRevisionState revisionState{.revision = snapshot.revision,
                                                  .savedRevision = snapshot.savedRevision};
        return saved ? finishOperation(AshariaSceneNativeStatus_Success, {}, revisionState,
                                       responseBuffer, responseCapacity, *result)
                     : finishOperation(statusFromError(saved.error()), saved.error().message,
                                       revisionState, responseBuffer, responseCapacity, *result);
    } catch (...) {
        return finishOperation(AshariaSceneNativeStatus_InternalError,
                               "Native scene document save failed unexpectedly.",
                               kEmptyRevisionState, responseBuffer, responseCapacity, *result);
    }
}

} // extern "C"
