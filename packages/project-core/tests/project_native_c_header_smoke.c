#include "asharia/project/project_native_api.h"

int main(void) {
    AshariaProjectNativeAbiHeader header = {
        ASHARIA_PROJECT_NATIVE_ABI_VERSION,
        (uint32_t)sizeof(AshariaProjectNativeAbiHeader),
    };
    AshariaProjectNativeResult result = {0};
    result.header = header;
    return result.header.abiVersion == ASHARIA_PROJECT_NATIVE_ABI_VERSION ? 0 : 1;
}
