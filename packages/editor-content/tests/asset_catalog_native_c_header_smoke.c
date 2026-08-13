#include <stddef.h>

#include "asharia/editor_content/asset_catalog_native_api.h"

int main(void) {
    AshariaEditorContentNativeQueryRequest request = {0};
    AshariaEditorContentNativeResult result = {0};
    request.header.abiVersion = ASHARIA_EDITOR_CONTENT_NATIVE_ABI_VERSION;
    request.header.structSize = (uint32_t)sizeof(request);
    return sizeof(AshariaEditorContentNativeAbiHeader) == 8U &&
                   sizeof(AshariaEditorContentNativeStringView) == 16U &&
                   sizeof(AshariaEditorContentNativeLimits) == 32U &&
                   sizeof(AshariaEditorContentNativeQueryRequest) == 88U &&
                   sizeof(AshariaEditorContentNativeResult) == 56U &&
                   offsetof(AshariaEditorContentNativeQueryRequest, limits) == 56U &&
                   offsetof(AshariaEditorContentNativeResult, payloadJsonUtf8) == 24U &&
                   result.operationStatus == 0U
               ? 0
               : 1;
}
