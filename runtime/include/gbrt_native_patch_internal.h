#ifndef GBRT_NATIVE_PATCH_INTERNAL_H
#define GBRT_NATIVE_PATCH_INTERNAL_H

#include "gbrt_native_patch.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GBNativeBinding {
    uint32_t abi_version;
    const char* patch_id;
    GBNativeFunctionId function_id;
    size_t rom_size;
    uint8_t rom_sha256[32];
    uint8_t allow_return_stack_entry;
    GBNativeHookFn pre;
    GBNativeReplacementFn replace;
    GBNativeHookFn post;
} GBNativeBinding;

typedef enum GBNativePhase {
    GB_NATIVE_PHASE_PRE = 1,
    GB_NATIVE_PHASE_REPLACEMENT = 2,
    GB_NATIVE_PHASE_ORIGINAL = 3,
    GB_NATIVE_PHASE_POST = 4
} GBNativePhase;

struct GBNativeCall {
    GBContext* ctx;
    const GBNativeBinding* binding;
    GBNativePhase phase;
    uint8_t original_requested;
    uint8_t failed;
};

typedef enum GBNativeEnterResult {
    GB_NATIVE_ENTER_SKIP = 0,
    GB_NATIVE_ENTER_RUN_ORIGINAL = 1,
    GB_NATIVE_ENTER_ERROR = 2
} GBNativeEnterResult;

/* Generated CALL/RST sites mark the exact call frame before any safepoint. */
void gbrt_native_patch_mark_call(GBContext* ctx,
                                 GBNativeFunctionId function_id,
                                 uint16_t return_pc);

/* Called only by a manifest-targeted generated wrapper. */
GBNativeEnterResult gbrt_native_patch_enter(GBContext* ctx,
                                             const GBNativeBinding* binding,
                                             const void* original_body_key);

int gbrt_native_patch_validate(GBContext* ctx, const GBNativeBinding* binding);

/* Called by RET helpers; completes only the matching active guest invocation. */
void gbrt_native_patch_on_return(GBContext* ctx);

void gbrt_native_patch_reset(GBContext* ctx);
void gbrt_native_patch_destroy(GBContext* ctx);

#ifdef __cplusplus
}
#endif

#endif
