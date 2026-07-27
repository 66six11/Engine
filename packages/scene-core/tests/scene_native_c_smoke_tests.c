#include <stdint.h>
#include <stdlib.h>

#include "asharia/scene/world_native_api.h"

int main(void) {
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
        transform.scale.z != 2.0F ||
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
