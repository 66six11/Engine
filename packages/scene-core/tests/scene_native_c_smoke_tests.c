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

    if (asharia_scene_world_destroy(world) != AshariaSceneNativeStatus_Success) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
