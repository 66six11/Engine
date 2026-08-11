#pragma once

#include <stdint.h>

#include "asharia/scene/world_native_api.h"

#define ASHARIA_SCENE_DOCUMENT_NATIVE_ABI_VERSION 2U
#define ASHARIA_SCENE_NATIVE_MAX_PROJECT_PATH_UTF8_BYTES 32768U

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Document handles are stable registry tokens, not pointers. They are owned by
 * the thread that opens them. Close invalidates the generation and clears a
 * successfully closed handle, so copied or reused stale tokens are rejected.
 */
typedef struct AshariaSceneNativeDocumentHandle {
    uint32_t index;
    uint32_t generation;
} AshariaSceneNativeDocumentHandle;

/* Offsets and byte lengths are relative to the start of the response buffer. */
typedef struct AshariaSceneNativeTextSpan {
    uint64_t offset;
    uint64_t byteLength;
} AshariaSceneNativeTextSpan;

typedef struct AshariaSceneNativeDocumentOpenDefaultRequest {
    AshariaSceneNativeAbiHeader header;
    AshariaSceneNativeStringView projectRootUtf8;
    /* Required only when the default scene does not yet exist. */
    AshariaSceneNativeStringView newSceneIdUtf8;
} AshariaSceneNativeDocumentOpenDefaultRequest;

typedef struct AshariaSceneNativeDocumentRequest {
    AshariaSceneNativeAbiHeader header;
    AshariaSceneNativeDocumentHandle document;
} AshariaSceneNativeDocumentRequest;

typedef struct AshariaSceneNativeDocumentCreateEntityRequest {
    AshariaSceneNativeAbiHeader header;
    AshariaSceneNativeDocumentHandle document;
    uint64_t expectedRevision;
    AshariaSceneNativeStringView objectIdUtf8;
    AshariaSceneNativeStringView nameUtf8;
} AshariaSceneNativeDocumentCreateEntityRequest;

typedef struct AshariaSceneNativeDocumentCreateMeshEntityRequest {
    AshariaSceneNativeAbiHeader header;
    AshariaSceneNativeDocumentHandle document;
    uint64_t expectedRevision;
    AshariaSceneNativeStringView objectIdUtf8;
    AshariaSceneNativeStringView nameUtf8;
    AshariaSceneNativeStringView meshAssetGuidUtf8;
} AshariaSceneNativeDocumentCreateMeshEntityRequest;

typedef struct AshariaSceneNativeDocumentSetEntityNameRequest {
    AshariaSceneNativeAbiHeader header;
    AshariaSceneNativeDocumentHandle document;
    uint64_t expectedRevision;
    AshariaSceneNativeStringView objectIdUtf8;
    AshariaSceneNativeStringView nameUtf8;
} AshariaSceneNativeDocumentSetEntityNameRequest;

typedef struct AshariaSceneNativeDocumentSetEntityTransformRequest {
    AshariaSceneNativeAbiHeader header;
    AshariaSceneNativeDocumentHandle document;
    uint64_t expectedRevision;
    AshariaSceneNativeStringView objectIdUtf8;
    AshariaSceneNativeTransform transform;
} AshariaSceneNativeDocumentSetEntityTransformRequest;

typedef struct AshariaSceneNativeDocumentSaveRequest {
    AshariaSceneNativeAbiHeader header;
    AshariaSceneNativeDocumentHandle document;
    uint64_t expectedRevision;
} AshariaSceneNativeDocumentSaveRequest;

/*
 * operationStatus preserves the underlying result when the function returns
 * BufferTooSmall. requiredBufferSize is the exact response size to retry with.
 */
typedef struct AshariaSceneNativeDocumentOperationResult {
    AshariaSceneNativeStatus operationStatus;
    uint32_t reserved;
    uint64_t requiredBufferSize;
    uint64_t revision;
    uint64_t savedRevision;
    AshariaSceneNativeTextSpan messageUtf8;
} AshariaSceneNativeDocumentOperationResult;

typedef struct AshariaSceneNativeDocumentEntitySnapshot {
    AshariaSceneNativeTextSpan objectIdUtf8;
    AshariaSceneNativeTextSpan nameUtf8;
    AshariaSceneNativeTransform transform;
    AshariaSceneNativeEntityId runtimeEntity;
    /* Empty when the entity has no Mesh component; otherwise a canonical asset GUID. */
    AshariaSceneNativeTextSpan meshAssetGuidUtf8;
} AshariaSceneNativeDocumentEntitySnapshot;

typedef struct AshariaSceneNativeDocumentSnapshotResult {
    AshariaSceneNativeStatus operationStatus;
    uint32_t reserved;
    uint64_t requiredBufferSize;
    uint64_t revision;
    uint64_t savedRevision;
    uint64_t entityCount;
    uint64_t entitiesOffset;
    AshariaSceneNativeTextSpan sceneIdUtf8;
    AshariaSceneNativeTextSpan messageUtf8;
} AshariaSceneNativeDocumentSnapshotResult;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_document_open_default(
    const AshariaSceneNativeDocumentOpenDefaultRequest* request,
    AshariaSceneNativeDocumentHandle* document, void* responseBuffer, uint64_t responseCapacity,
    AshariaSceneNativeDocumentOperationResult* result) ASHARIA_SCENE_NATIVE_NOEXCEPT;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_document_close(AshariaSceneNativeDocumentHandle* document)
    ASHARIA_SCENE_NATIVE_NOEXCEPT;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_document_snapshot(const AshariaSceneNativeDocumentRequest* request,
                                void* responseBuffer, uint64_t responseCapacity,
                                AshariaSceneNativeDocumentSnapshotResult* result)
    ASHARIA_SCENE_NATIVE_NOEXCEPT;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_document_create_entity(const AshariaSceneNativeDocumentCreateEntityRequest* request,
                                     void* responseBuffer, uint64_t responseCapacity,
                                     AshariaSceneNativeDocumentOperationResult* result)
    ASHARIA_SCENE_NATIVE_NOEXCEPT;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_document_create_mesh_entity(
    const AshariaSceneNativeDocumentCreateMeshEntityRequest* request, void* responseBuffer,
    uint64_t responseCapacity,
    AshariaSceneNativeDocumentOperationResult* result) ASHARIA_SCENE_NATIVE_NOEXCEPT;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_document_set_entity_name(
    const AshariaSceneNativeDocumentSetEntityNameRequest* request, void* responseBuffer,
    uint64_t responseCapacity,
    AshariaSceneNativeDocumentOperationResult* result) ASHARIA_SCENE_NATIVE_NOEXCEPT;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_document_set_entity_transform(
    const AshariaSceneNativeDocumentSetEntityTransformRequest* request, void* responseBuffer,
    uint64_t responseCapacity,
    AshariaSceneNativeDocumentOperationResult* result) ASHARIA_SCENE_NATIVE_NOEXCEPT;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_document_save(const AshariaSceneNativeDocumentSaveRequest* request,
                            void* responseBuffer, uint64_t responseCapacity,
                            AshariaSceneNativeDocumentOperationResult* result)
    ASHARIA_SCENE_NATIVE_NOEXCEPT;

#if defined(__cplusplus)
} // extern "C"
#endif
