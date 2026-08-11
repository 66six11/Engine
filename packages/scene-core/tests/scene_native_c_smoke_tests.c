#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "asharia/scene/scene_document_native_api.h"
#include "asharia/scene/world_native_api.h"

int main(void) {
    _Static_assert(ASHARIA_SCENE_DOCUMENT_NATIVE_ABI_VERSION == 2U,
                   "SceneDocument ABI must hard-cut independently from World ABI.");
    _Static_assert(sizeof(AshariaSceneNativeDocumentCreateMeshEntityRequest) == 72U,
                   "Unexpected C mesh entity request layout.");
    _Static_assert(sizeof(AshariaSceneNativeDocumentEntitySnapshot) == 96U,
                   "Unexpected C SceneDocument entity snapshot layout.");
    (void)&asharia_scene_document_create_mesh_entity;

    AshariaSceneNativeDocumentHandle document = {0U, 0U};
    if (document.index != 0U || document.generation != 0U) {
        return EXIT_FAILURE;
    }

    AshariaSceneNativeWorldCreateRequest request;
    request.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION;
    request.header.structSize = (uint32_t)sizeof(request);

    AshariaSceneNativeWorld* world = NULL;
    if (asharia_scene_world_create(&request, &world) != AshariaSceneNativeStatus_Success ||
        world == NULL) {
        return EXIT_FAILURE;
    }

    AshariaSceneNativeCreateEntityRequest createEntityRequest;
    createEntityRequest.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION;
    createEntityRequest.header.structSize = (uint32_t)sizeof(createEntityRequest);

    AshariaSceneNativeEntityId entity = {0U, 0U};
    if (asharia_scene_world_create_entity(world, &createEntityRequest, &entity) !=
            AshariaSceneNativeStatus_Success ||
        entity.index == 0U || entity.generation == 0U) {
        return EXIT_FAILURE;
    }

    AshariaSceneNativeEntityRequest entityRequest;
    entityRequest.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION;
    entityRequest.header.structSize = (uint32_t)sizeof(entityRequest);
    entityRequest.entity = entity;

    uint32_t isAlive = 0U;
    if (asharia_scene_world_is_alive(world, &entityRequest, &isAlive) !=
            AshariaSceneNativeStatus_Success ||
        isAlive != 1U) {
        return EXIT_FAILURE;
    }

    AshariaSceneNativeSetLocalTransformRequest setTransformRequest;
    setTransformRequest.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION;
    setTransformRequest.header.structSize = (uint32_t)sizeof(setTransformRequest);
    setTransformRequest.entity = entity;
    setTransformRequest.transform.position.x = 1.0F;
    setTransformRequest.transform.position.y = -2.0F;
    setTransformRequest.transform.position.z = 3.0F;
    setTransformRequest.transform.rotation.x = 0.0F;
    setTransformRequest.transform.rotation.y = 0.0F;
    setTransformRequest.transform.rotation.z = 0.0F;
    setTransformRequest.transform.rotation.w = 1.0F;
    setTransformRequest.transform.scale.x = 0.0F;
    setTransformRequest.transform.scale.y = -1.0F;
    setTransformRequest.transform.scale.z = 2.0F;

    if (asharia_scene_world_set_local_transform(world, &setTransformRequest) !=
        AshariaSceneNativeStatus_Success) {
        return EXIT_FAILURE;
    }

    AshariaSceneNativeTransform transform = {0};
    if (asharia_scene_world_get_local_transform(world, &entityRequest, &transform) !=
            AshariaSceneNativeStatus_Success ||
        transform.position.x != 1.0F || transform.position.y != -2.0F ||
        transform.position.z != 3.0F || transform.rotation.x != 0.0F ||
        transform.rotation.y != 0.0F || transform.rotation.z != 0.0F ||
        transform.rotation.w != 1.0F || transform.scale.x != 0.0F || transform.scale.y != -1.0F ||
        transform.scale.z != 2.0F) {
        return EXIT_FAILURE;
    }

    static const char nameUtf8[] = "Native \xE7\xAB\x8B\xE6\x96\xB9\xE4\xBD\x93";
    AshariaSceneNativeSetEntityNameRequest setNameRequest;
    setNameRequest.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION;
    setNameRequest.header.structSize = (uint32_t)sizeof(setNameRequest);
    setNameRequest.entity = entity;
    setNameRequest.nameUtf8.data = nameUtf8;
    setNameRequest.nameUtf8.byteLength = (uint64_t)(sizeof(nameUtf8) - 1U);

    uint64_t nameByteLength = 0U;
    if (asharia_scene_world_set_entity_name(world, &setNameRequest) !=
            AshariaSceneNativeStatus_Success ||
        asharia_scene_world_get_entity_name(world, &entityRequest, NULL, 0U, &nameByteLength) !=
            AshariaSceneNativeStatus_Success ||
        nameByteLength != (uint64_t)(sizeof(nameUtf8) - 1U)) {
        return EXIT_FAILURE;
    }

    char copiedName[sizeof(nameUtf8) - 1U];
    if (asharia_scene_world_get_entity_name(world, &entityRequest, copiedName,
                                            (uint64_t)sizeof(copiedName),
                                            &nameByteLength) != AshariaSceneNativeStatus_Success ||
        nameByteLength != (uint64_t)sizeof(copiedName) ||
        memcmp(copiedName, nameUtf8, sizeof(copiedName)) != 0 ||
        asharia_scene_world_destroy_entity(world, &entityRequest) !=
            AshariaSceneNativeStatus_Success) {
        return EXIT_FAILURE;
    }

    isAlive = 1U;
    if (asharia_scene_world_is_alive(world, &entityRequest, &isAlive) !=
            AshariaSceneNativeStatus_Success ||
        isAlive != 0U) {
        return EXIT_FAILURE;
    }

    if (asharia_scene_world_destroy(world) != AshariaSceneNativeStatus_Success) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
