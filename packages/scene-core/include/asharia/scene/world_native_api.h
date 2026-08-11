#pragma once

#include <stdint.h>

#define ASHARIA_SCENE_NATIVE_ABI_VERSION 1U
#define ASHARIA_SCENE_NATIVE_MAX_ENTITY_NAME_UTF8_BYTES 4096U

#if defined(_WIN32)
#define ASHARIA_SCENE_NATIVE_CALL __cdecl
#if defined(ASHARIA_SCENE_NATIVE_BUILD)
#define ASHARIA_SCENE_NATIVE_API __declspec(dllexport)
#else
#define ASHARIA_SCENE_NATIVE_API __declspec(dllimport)
#endif
#else
#define ASHARIA_SCENE_NATIVE_CALL
#define ASHARIA_SCENE_NATIVE_API __attribute__((visibility("default")))
#endif

#if defined(__cplusplus)
#define ASHARIA_SCENE_NATIVE_NOEXCEPT noexcept
extern "C" {
#else
#define ASHARIA_SCENE_NATIVE_NOEXCEPT
#endif

typedef uint32_t AshariaSceneNativeStatus;
enum {
    AshariaSceneNativeStatus_Success = 0U,
    AshariaSceneNativeStatus_InvalidArgument = 1U,
    AshariaSceneNativeStatus_UnsupportedAbi = 2U,
    AshariaSceneNativeStatus_WrongThread = 3U,
    AshariaSceneNativeStatus_InternalError = 4U,
    AshariaSceneNativeStatus_InvalidEntity = 5U,
    AshariaSceneNativeStatus_EntityCapacityExceeded = 6U,
    AshariaSceneNativeStatus_InvalidTransform = 7U,
    AshariaSceneNativeStatus_InvalidUtf8 = 8U,
    AshariaSceneNativeStatus_BufferTooSmall = 9U,
    AshariaSceneNativeStatus_InvalidScene = 10U,
    AshariaSceneNativeStatus_RevisionConflict = 11U,
    AshariaSceneNativeStatus_IoFailure = 12U,
    AshariaSceneNativeStatus_StaleHandle = 13U,
    AshariaSceneNativeStatus_InvalidObject = 14U,
    AshariaSceneNativeStatus_DuplicateObject = 15U,
    AshariaSceneNativeStatus_InvalidAssetReference = 16U,
};

typedef struct AshariaSceneNativeAbiHeader {
    uint32_t abiVersion;
    uint32_t structSize;
} AshariaSceneNativeAbiHeader;

typedef struct AshariaSceneNativeEntityId {
    uint32_t index;
    uint32_t generation;
} AshariaSceneNativeEntityId;

/*
 * Length-delimited UTF-8 input. data may be null only when byteLength is zero.
 * The callee borrows the bytes for the duration of the call and copies retained
 * values before returning.
 */
typedef struct AshariaSceneNativeStringView {
    const char* data;
    uint64_t byteLength;
} AshariaSceneNativeStringView;

typedef struct AshariaSceneNativeVec3 {
    float x;
    float y;
    float z;
} AshariaSceneNativeVec3;

typedef struct AshariaSceneNativeQuat {
    float x;
    float y;
    float z;
    float w;
} AshariaSceneNativeQuat;

typedef struct AshariaSceneNativeTransform {
    AshariaSceneNativeVec3 position;
    AshariaSceneNativeQuat rotation;
    AshariaSceneNativeVec3 scale;
} AshariaSceneNativeTransform;

typedef struct AshariaSceneNativeWorldCreateRequest {
    AshariaSceneNativeAbiHeader header;
} AshariaSceneNativeWorldCreateRequest;

typedef struct AshariaSceneNativeCreateEntityRequest {
    AshariaSceneNativeAbiHeader header;
} AshariaSceneNativeCreateEntityRequest;

typedef struct AshariaSceneNativeEntityRequest {
    AshariaSceneNativeAbiHeader header;
    AshariaSceneNativeEntityId entity;
} AshariaSceneNativeEntityRequest;

typedef struct AshariaSceneNativeSetLocalTransformRequest {
    AshariaSceneNativeAbiHeader header;
    AshariaSceneNativeEntityId entity;
    AshariaSceneNativeTransform transform;
} AshariaSceneNativeSetLocalTransformRequest;

typedef struct AshariaSceneNativeSetEntityNameRequest {
    AshariaSceneNativeAbiHeader header;
    AshariaSceneNativeEntityId entity;
    AshariaSceneNativeStringView nameUtf8;
} AshariaSceneNativeSetEntityNameRequest;

typedef struct AshariaSceneNativeWorld AshariaSceneNativeWorld;

/*
 * A World handle is owned by the thread that creates it. The caller must
 * serialize access, stop dependent work before destroy, and destroy the handle
 * on its owner thread. A handle is invalid after a successful destroy.
 */
ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_world_create(const AshariaSceneNativeWorldCreateRequest* request,
                           AshariaSceneNativeWorld** world) ASHARIA_SCENE_NATIVE_NOEXCEPT;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_world_destroy(AshariaSceneNativeWorld* world) ASHARIA_SCENE_NATIVE_NOEXCEPT;

/*
 * Entity IDs are scoped to their World. Destroy invalidates the ID; a reused
 * index receives a different generation, so stale IDs do not name the new
 * entity. Zero is reserved for invalid or failed outputs.
 */
ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_world_create_entity(AshariaSceneNativeWorld* world,
                                  const AshariaSceneNativeCreateEntityRequest* request,
                                  AshariaSceneNativeEntityId* entity) ASHARIA_SCENE_NATIVE_NOEXCEPT;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_world_destroy_entity(AshariaSceneNativeWorld* world,
                                   const AshariaSceneNativeEntityRequest* request)
    ASHARIA_SCENE_NATIVE_NOEXCEPT;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_world_is_alive(AshariaSceneNativeWorld* world,
                             const AshariaSceneNativeEntityRequest* request,
                             uint32_t* isAlive) ASHARIA_SCENE_NATIVE_NOEXCEPT;

/*
 * Local Transform is parent-relative. Version 1 has no hierarchy or world
 * Transform operation. Set rejects non-finite values and non-unit rotations;
 * it never silently normalizes or clamps caller data.
 */
ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_world_get_local_transform(
    AshariaSceneNativeWorld* world, const AshariaSceneNativeEntityRequest* request,
    AshariaSceneNativeTransform* transform) ASHARIA_SCENE_NATIVE_NOEXCEPT;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_world_set_local_transform(AshariaSceneNativeWorld* world,
                                        const AshariaSceneNativeSetLocalTransformRequest* request)
    ASHARIA_SCENE_NATIVE_NOEXCEPT;

/*
 * Entity names are mutable, non-unique display/debug text. They are not entity
 * identity, paths, or lookup keys. Set accepts at most
 * ASHARIA_SCENE_NATIVE_MAX_ENTITY_NAME_UTF8_BYTES bytes.
 *
 * Get copies exact UTF-8 bytes without a trailing NUL. A null buffer with zero
 * capacity queries the required byte length. An undersized non-null buffer is
 * left untouched and returns BufferTooSmall with the required length.
 */
ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_world_get_entity_name(AshariaSceneNativeWorld* world,
                                    const AshariaSceneNativeEntityRequest* request, char* nameUtf8,
                                    uint64_t nameCapacity,
                                    uint64_t* nameByteLength) ASHARIA_SCENE_NATIVE_NOEXCEPT;

ASHARIA_SCENE_NATIVE_API AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_world_set_entity_name(AshariaSceneNativeWorld* world,
                                    const AshariaSceneNativeSetEntityNameRequest* request)
    ASHARIA_SCENE_NATIVE_NOEXCEPT;

#if defined(__cplusplus)
} // extern "C"
#endif
