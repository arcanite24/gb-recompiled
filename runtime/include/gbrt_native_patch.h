#ifndef GBRT_NATIVE_PATCH_H
#define GBRT_NATIVE_PATCH_H

#include "gbrt.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GB_NATIVE_PATCH_ABI_VERSION 1u
#define GB_NATIVE_FUNCTION_ID(bank_, address_) \
    ((((uint32_t)(bank_)) << 16u) | ((uint32_t)(address_) & 0xffffu))

typedef uint32_t GBNativeFunctionId;
typedef struct GBNativeCall GBNativeCall;

typedef enum GBNativeStatus {
    GB_NATIVE_STATUS_OK = 0,
    GB_NATIVE_STATUS_ERROR = 1
} GBNativeStatus;

typedef enum GBNativeReplaceResult {
    /* The replacement completed the guest call, including timing and RET. */
    GB_NATIVE_REPLACE_HANDLED = 0,
    /* Schedule the one generated original body. This is not a nested C call. */
    GB_NATIVE_REPLACE_USE_ORIGINAL = 1,
    /* Stop execution with a deterministic patch error. */
    GB_NATIVE_REPLACE_ERROR = 2
} GBNativeReplaceResult;

typedef GBNativeStatus (*GBNativeHookFn)(GBNativeCall* call);
typedef GBNativeReplaceResult (*GBNativeReplacementFn)(GBNativeCall* call);

GBContext* gb_native_context(GBNativeCall* call);
GBNativeFunctionId gb_native_function_id(const GBNativeCall* call);
const char* gb_native_patch_id(const GBNativeCall* call);
int gb_native_patch_failed(const GBContext* ctx);
bool gb_native_use_host_presentation(const GBNativeCall* call);

/*
 * Request original execution from a replacement callback. Return this result
 * directly. The generated body may yield, so post work belongs in a post hook.
 */
GBNativeReplaceResult gb_native_call_original(GBNativeCall* call);

/* Record a diagnostic, stop execution, and return GB_NATIVE_REPLACE_ERROR. */
GBNativeReplaceResult gb_native_fail(GBNativeCall* call, const char* message);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#define GB_NATIVE_EXTERN extern "C"
#else
#define GB_NATIVE_EXTERN extern
#endif

#define GB_NATIVE_HOOK(name_) \
    GB_NATIVE_EXTERN GBNativeStatus name_(GBNativeCall* call)
#define GB_NATIVE_REPLACEMENT(name_) \
    GB_NATIVE_EXTERN GBNativeReplaceResult name_(GBNativeCall* call)

#endif
