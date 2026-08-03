#include "gbrt_native_patch.h"

GB_NATIVE_HOOK(nl5_pre) {
    GBContext* ctx = gb_native_context(call);
    ctx->hram[0]++;
    return GB_NATIVE_STATUS_OK;
}

GB_NATIVE_REPLACEMENT(nl5_replace) {
    GBContext* ctx = gb_native_context(call);
    ctx->hram[1]++;

    /* This is a scheduling disposition, not a synchronous nested C call. */
    return gb_native_call_original(call);
}

GB_NATIVE_HOOK(nl5_post) {
    GBContext* ctx = gb_native_context(call);
    ctx->hram[2]++;
    return GB_NATIVE_STATUS_OK;
}
