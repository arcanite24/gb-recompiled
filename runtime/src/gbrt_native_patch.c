#include "gbrt_native_patch_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GBRT_ENABLE_NATIVE_PATCHES
#error "gbrt_native_patch.c requires GBRT_ENABLE_NATIVE_PATCHES"
#endif

#define GBRT_NATIVE_PATCH_MAX_DEPTH 32u

typedef struct GBNativePendingCall {
    GBNativeFunctionId function_id;
    uint16_t entry_sp;
    uint16_t return_pc;
} GBNativePendingCall;

typedef struct GBNativeInvocation {
    const GBNativeBinding* binding;
    const void* original_body_key;
    uint16_t entry_sp;
    uint16_t return_pc;
    GBNativePhase phase;
    GBNativeCall call;
} GBNativeInvocation;

typedef struct GBNativePatchState {
    GBNativePendingCall pending[GBRT_NATIVE_PATCH_MAX_DEPTH];
    GBNativeInvocation invocations[GBRT_NATIVE_PATCH_MAX_DEPTH];
    size_t pending_count;
    size_t invocation_count;
    size_t validated_rom_size;
    uint8_t validated_rom_sha256[32];
    uint8_t identity_validated;
    uint8_t failed;
} GBNativePatchState;

static uint32_t rotate_right(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
}

static void sha256(const uint8_t* data, size_t size, uint8_t digest[32]) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    const uint64_t bit_size = (uint64_t)size * 8u;
    const size_t padded_size = ((size + 9u + 63u) / 64u) * 64u;
    uint8_t* padded = (uint8_t*)calloc(1, padded_size);
    size_t offset;
    if (padded == NULL) {
        memset(digest, 0, 32u);
        return;
    }
    if (size > 0u) memcpy(padded, data, size);
    padded[size] = 0x80u;
    for (offset = 0; offset < 8u; ++offset) {
        padded[padded_size - 1u - offset] = (uint8_t)(bit_size >> (offset * 8u));
    }

    for (offset = 0; offset < padded_size; offset += 64u) {
        uint32_t w[64] = {0};
        size_t i;
        for (i = 0; i < 16u; ++i) {
            const size_t p = offset + i * 4u;
            w[i] = ((uint32_t)padded[p] << 24u) |
                   ((uint32_t)padded[p + 1u] << 16u) |
                   ((uint32_t)padded[p + 2u] << 8u) |
                   (uint32_t)padded[p + 3u];
        }
        for (i = 16u; i < 64u; ++i) {
            const uint32_t s0 = rotate_right(w[i - 15u], 7u) ^
                                rotate_right(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
            const uint32_t s1 = rotate_right(w[i - 2u], 17u) ^
                                rotate_right(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
            w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
        }
        {
            uint32_t a = h[0];
            uint32_t b = h[1];
            uint32_t c = h[2];
            uint32_t d = h[3];
            uint32_t e = h[4];
            uint32_t f = h[5];
            uint32_t g = h[6];
            uint32_t hh = h[7];
            for (i = 0; i < 64u; ++i) {
                const uint32_t s1 = rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
                const uint32_t ch = (e & f) ^ ((~e) & g);
                const uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
                const uint32_t s0 = rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
                const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                const uint32_t temp2 = s0 + maj;
                hh = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
            }
            h[0] += a;
            h[1] += b;
            h[2] += c;
            h[3] += d;
            h[4] += e;
            h[5] += f;
            h[6] += g;
            h[7] += hh;
        }
    }
    free(padded);
    for (offset = 0; offset < 8u; ++offset) {
        digest[offset * 4u] = (uint8_t)(h[offset] >> 24u);
        digest[offset * 4u + 1u] = (uint8_t)(h[offset] >> 16u);
        digest[offset * 4u + 2u] = (uint8_t)(h[offset] >> 8u);
        digest[offset * 4u + 3u] = (uint8_t)h[offset];
    }
}

static GBNativePatchState* get_state(GBContext* ctx, int create) {
    GBNativePatchState* state;
    if (ctx == NULL) return NULL;
    state = (GBNativePatchState*)ctx->native_patch_state;
    if (state == NULL && create) {
        state = (GBNativePatchState*)calloc(1, sizeof(*state));
        ctx->native_patch_state = state;
    }
    return state;
}

static void fail_context(GBContext* ctx, const char* patch_id, const char* message) {
    GBNativePatchState* state = get_state(ctx, 1);
    if (state != NULL) state->failed = 1u;
    if (ctx != NULL) ctx->stopped = 1u;
    fprintf(stderr, "[GBRT][native-patch:%s] %s\n",
            patch_id != NULL ? patch_id : "unknown",
            message != NULL ? message : "unspecified native patch failure");
}

static int validate_identity(GBContext* ctx,
                             GBNativePatchState* state,
                             const GBNativeBinding* binding) {
    uint8_t digest[32];
    if (binding->abi_version != GB_NATIVE_PATCH_ABI_VERSION) {
        fail_context(ctx, binding->patch_id, "unsupported native patch ABI");
        return 0;
    }
    if (state->identity_validated) {
        if (state->validated_rom_size != binding->rom_size ||
            memcmp(state->validated_rom_sha256, binding->rom_sha256, 32u) != 0) {
            fail_context(ctx, binding->patch_id, "bindings disagree on exact ROM identity");
            return 0;
        }
        return 1;
    }
    if (ctx->rom == NULL || ctx->rom_size != binding->rom_size) {
        fail_context(ctx, binding->patch_id, "loaded ROM size does not match patch identity");
        return 0;
    }
    sha256(ctx->rom, ctx->rom_size, digest);
    if (memcmp(digest, binding->rom_sha256, 32u) != 0) {
        fail_context(ctx, binding->patch_id, "loaded ROM SHA-256 does not match patch identity");
        return 0;
    }
    state->validated_rom_size = binding->rom_size;
    memcpy(state->validated_rom_sha256, binding->rom_sha256, 32u);
    state->identity_validated = 1u;
    return 1;
}

static int run_hook(GBNativeInvocation* invocation,
                    GBNativePhase phase,
                    GBNativeHookFn hook,
                    const char* role) {
    GBNativeStatus result;
    if (hook == NULL) return 1;
    invocation->phase = phase;
    invocation->call.phase = phase;
    invocation->call.failed = 0u;
    result = hook(&invocation->call);
    if (result != GB_NATIVE_STATUS_OK || invocation->call.failed) {
        if (!invocation->call.failed) {
            char message[128];
            snprintf(message, sizeof(message), "%s callback returned an error", role);
            fail_context(invocation->call.ctx, invocation->binding->patch_id, message);
        }
        return 0;
    }
    return 1;
}

GBContext* gb_native_context(GBNativeCall* call) {
    return call != NULL ? call->ctx : NULL;
}

GBNativeFunctionId gb_native_function_id(const GBNativeCall* call) {
    return call != NULL && call->binding != NULL ? call->binding->function_id : 0u;
}

const char* gb_native_patch_id(const GBNativeCall* call) {
    return call != NULL && call->binding != NULL ? call->binding->patch_id : NULL;
}

int gb_native_patch_failed(const GBContext* ctx) {
    const GBNativePatchState* state;
    if (ctx == NULL) return 1;
    state = (const GBNativePatchState*)ctx->native_patch_state;
    return state != NULL && state->failed;
}

bool gb_native_use_host_presentation(const GBNativeCall* call) {
    return call != NULL && call->ctx != NULL &&
           call->ctx->config.native_presentation_enabled;
}

int gbrt_native_patch_validate(GBContext* ctx, const GBNativeBinding* binding) {
    GBNativePatchState* state = get_state(ctx, 1);
    if (state == NULL || binding == NULL) {
        fail_context(ctx, binding != NULL ? binding->patch_id : NULL,
                     "invalid generated native patch binding");
        return 0;
    }
    if (state->failed) return 0;
    return validate_identity(ctx, state, binding);
}

GBNativeReplaceResult gb_native_call_original(GBNativeCall* call) {
    if (call == NULL || call->ctx == NULL || call->binding == NULL) {
        return GB_NATIVE_REPLACE_ERROR;
    }
    if (call->phase != GB_NATIVE_PHASE_REPLACEMENT) {
        call->failed = 1u;
        fail_context(call->ctx, call->binding->patch_id,
                     "gb_native_call_original is only valid in a replacement callback");
        return GB_NATIVE_REPLACE_ERROR;
    }
    if (call->original_requested) {
        call->failed = 1u;
        fail_context(call->ctx, call->binding->patch_id,
                     "the generated original was requested more than once");
        return GB_NATIVE_REPLACE_ERROR;
    }
    call->original_requested = 1u;
    return GB_NATIVE_REPLACE_USE_ORIGINAL;
}

GBNativeReplaceResult gb_native_fail(GBNativeCall* call, const char* message) {
    if (call != NULL && call->ctx != NULL && call->binding != NULL) {
        call->failed = 1u;
        fail_context(call->ctx, call->binding->patch_id, message);
    }
    return GB_NATIVE_REPLACE_ERROR;
}

void gbrt_native_patch_mark_call(GBContext* ctx,
                                 GBNativeFunctionId function_id,
                                 uint16_t return_pc) {
    GBNativePatchState* state = get_state(ctx, 1);
    GBNativePendingCall* pending;
    if (state == NULL) {
        fail_context(ctx, NULL, "could not allocate native patch state");
        return;
    }
    if (state->failed) return;
    if (state->pending_count >= GBRT_NATIVE_PATCH_MAX_DEPTH) {
        fail_context(ctx, NULL, "native patch pending-call depth exceeded 32");
        return;
    }
    pending = &state->pending[state->pending_count++];
    pending->function_id = function_id;
    pending->entry_sp = ctx->sp;
    pending->return_pc = return_pc;
}

GBNativeEnterResult gbrt_native_patch_enter(GBContext* ctx,
                                             const GBNativeBinding* binding,
                                             const void* original_body_key) {
    GBNativePatchState* state = get_state(ctx, 1);
    GBNativePendingCall* pending = NULL;
    GBNativeInvocation* invocation;
    GBNativeReplaceResult replacement_result = GB_NATIVE_REPLACE_USE_ORIGINAL;
    if (state == NULL || binding == NULL || original_body_key == NULL) {
        fail_context(ctx, binding != NULL ? binding->patch_id : NULL,
                     "invalid generated native patch binding");
        return GB_NATIVE_ENTER_ERROR;
    }
    if (state->failed) return GB_NATIVE_ENTER_ERROR;
    if (!validate_identity(ctx, state, binding)) return GB_NATIVE_ENTER_ERROR;

    if (state->pending_count > 0u) {
        GBNativePendingCall* candidate = &state->pending[state->pending_count - 1u];
        if (candidate->function_id == binding->function_id &&
            candidate->entry_sp == ctx->sp &&
            ctx->pc == (uint16_t)binding->function_id) {
            pending = candidate;
        }
    }

    if (pending == NULL && state->invocation_count > 0u) {
        invocation = &state->invocations[state->invocation_count - 1u];
        if (invocation->phase == GB_NATIVE_PHASE_ORIGINAL &&
            invocation->original_body_key == original_body_key &&
            invocation->entry_sp == ctx->sp) {
            return GB_NATIVE_ENTER_RUN_ORIGINAL;
        }
    }

    if (pending == NULL && binding->allow_return_stack_entry) {
        const bool stack_is_wram =
            ctx->sp >= 0xc000u && ctx->sp <= 0xdffeu;
        const bool stack_is_hram =
            ctx->sp >= 0xff80u && ctx->sp <= 0xfffdu;
        if (!stack_is_wram && !stack_is_hram) {
            fail_context(
                ctx,
                binding->patch_id,
                "return-stack entry did not have a readable WRAM/HRAM return frame");
            return GB_NATIVE_ENTER_ERROR;
        }
        if (state->pending_count >= GBRT_NATIVE_PATCH_MAX_DEPTH) {
            fail_context(
                ctx,
                binding->patch_id,
                "native patch pending-call depth exceeded 32");
            return GB_NATIVE_ENTER_ERROR;
        }
        pending = &state->pending[state->pending_count++];
        pending->function_id = binding->function_id;
        pending->entry_sp = ctx->sp;
        pending->return_pc = gb_read16(ctx, ctx->sp);
    }

    if (pending == NULL) {
        fail_context(ctx, binding->patch_id,
                     "patched function was entered without a generated CALL/RST contract");
        return GB_NATIVE_ENTER_ERROR;
    }
    if (state->invocation_count >= GBRT_NATIVE_PATCH_MAX_DEPTH) {
        fail_context(ctx, binding->patch_id, "native patch invocation depth exceeded 32");
        return GB_NATIVE_ENTER_ERROR;
    }

    ++state->invocation_count;
    invocation = &state->invocations[state->invocation_count - 1u];
    memset(invocation, 0, sizeof(*invocation));
    invocation->binding = binding;
    invocation->original_body_key = original_body_key;
    invocation->entry_sp = pending->entry_sp;
    invocation->return_pc = pending->return_pc;
    invocation->call.ctx = ctx;
    invocation->call.binding = binding;
    --state->pending_count;

    if (!run_hook(invocation, GB_NATIVE_PHASE_PRE, binding->pre, "pre")) {
        --state->invocation_count;
        return GB_NATIVE_ENTER_ERROR;
    }
    if (ctx->sp != invocation->entry_sp || ctx->pc != (uint16_t)binding->function_id) {
        fail_context(ctx, binding->patch_id, "pre callback changed the guest call frame");
        --state->invocation_count;
        return GB_NATIVE_ENTER_ERROR;
    }

    invocation->phase = GB_NATIVE_PHASE_REPLACEMENT;
    invocation->call.phase = GB_NATIVE_PHASE_REPLACEMENT;
    invocation->call.original_requested = 0u;
    invocation->call.failed = 0u;
    if (binding->replace != NULL) {
        replacement_result = binding->replace(&invocation->call);
    }
    if (invocation->call.failed || replacement_result == GB_NATIVE_REPLACE_ERROR) {
        if (!invocation->call.failed) {
            fail_context(ctx, binding->patch_id, "replacement callback returned an error");
        }
        --state->invocation_count;
        return GB_NATIVE_ENTER_ERROR;
    }
    if (replacement_result == GB_NATIVE_REPLACE_USE_ORIGINAL) {
        if (ctx->sp != invocation->entry_sp || ctx->pc != (uint16_t)binding->function_id) {
            fail_context(ctx, binding->patch_id,
                         "replacement changed the guest call frame before requesting original");
            --state->invocation_count;
            return GB_NATIVE_ENTER_ERROR;
        }
        invocation->phase = GB_NATIVE_PHASE_ORIGINAL;
        invocation->call.phase = GB_NATIVE_PHASE_ORIGINAL;
        return GB_NATIVE_ENTER_RUN_ORIGINAL;
    }
    if (replacement_result != GB_NATIVE_REPLACE_HANDLED) {
        fail_context(ctx, binding->patch_id, "replacement returned an invalid disposition");
        --state->invocation_count;
        return GB_NATIVE_ENTER_ERROR;
    }
    if (invocation->call.original_requested) {
        fail_context(ctx, binding->patch_id,
                     "replacement requested original but returned handled");
        --state->invocation_count;
        return GB_NATIVE_ENTER_ERROR;
    }
    if (ctx->sp != (uint16_t)(invocation->entry_sp + 2u) || ctx->pc != invocation->return_pc) {
        fail_context(ctx, binding->patch_id,
                     "handled replacement did not complete the promised guest RET");
        --state->invocation_count;
        return GB_NATIVE_ENTER_ERROR;
    }
    if (!run_hook(invocation, GB_NATIVE_PHASE_POST, binding->post, "post")) {
        --state->invocation_count;
        return GB_NATIVE_ENTER_ERROR;
    }
    --state->invocation_count;
    return GB_NATIVE_ENTER_SKIP;
}

void gbrt_native_patch_on_return(GBContext* ctx) {
    GBNativePatchState* state = get_state(ctx, 0);
    GBNativeInvocation* invocation;
    if (state == NULL || state->failed || state->invocation_count == 0u) return;
    invocation = &state->invocations[state->invocation_count - 1u];
    if (invocation->phase != GB_NATIVE_PHASE_ORIGINAL) return;
    if (ctx->sp != (uint16_t)(invocation->entry_sp + 2u) || ctx->pc != invocation->return_pc) {
        return;
    }
    (void)run_hook(invocation, GB_NATIVE_PHASE_POST, invocation->binding->post, "post");
    --state->invocation_count;
}

void gbrt_native_patch_reset(GBContext* ctx) {
    GBNativePatchState* state = get_state(ctx, 0);
    if (state != NULL) memset(state, 0, sizeof(*state));
}

void gbrt_native_patch_destroy(GBContext* ctx) {
    if (ctx == NULL) return;
    free(ctx->native_patch_state);
    ctx->native_patch_state = NULL;
}
