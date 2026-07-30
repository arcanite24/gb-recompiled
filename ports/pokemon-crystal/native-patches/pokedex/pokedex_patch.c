#include "gbrt_native_patch.h"
#include "gbrt_port.h"

GB_NATIVE_REPLACEMENT(crystal_native_pokedex) {
    if (!gb_native_use_host_presentation(call)) {
        return gb_native_call_original(call);
    }

    GBContext* context = gb_native_context(call);
    const GBPortInputEvent event = {
        .action = GB_PORT_INPUT_OPEN_UI,
        .pressed = true,
    };
    if (gbrt_port_input(context, &event) != GB_PORT_OK) {
        return gb_native_fail(
            call, "native Pokedex requires the exact-ROM port module");
    }

    /*
     * The host surface owns the overlay and browsing controls, while the
     * original body retains every guest-visible timing, mapper, and save-side
     * effect. The captured farcall frame keeps post-return scheduling honest.
     */
    return gb_native_call_original(call);
}
