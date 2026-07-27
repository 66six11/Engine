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
        isAlive != 1U ||
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
