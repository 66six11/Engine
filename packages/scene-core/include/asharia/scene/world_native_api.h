#pragma once

#include <stdint.h>

#define ASHARIA_SCENE_NATIVE_ABI_VERSION 1U

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
};

typedef struct AshariaSceneNativeAbiHeader {
    uint32_t abiVersion;
    uint32_t structSize;
} AshariaSceneNativeAbiHeader;

typedef struct AshariaSceneNativeWorldCreateRequest {
    AshariaSceneNativeAbiHeader header;
} AshariaSceneNativeWorldCreateRequest;

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

#if defined(__cplusplus)
} // extern "C"
#endif
