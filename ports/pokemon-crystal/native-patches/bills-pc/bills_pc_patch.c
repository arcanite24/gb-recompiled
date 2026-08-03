#include "gbrt_native_patch.h"
#include "gbrt_port.h"

GB_NATIVE_REPLACEMENT(crystal_native_bills_pc) {
    if (!gb_native_use_host_presentation(call)) {
        return gb_native_call_original(call);
    }

    GBContext* context = gb_native_context(call);
    const GBPortInputEvent event = {
        .action = GB_PORT_INPUT_OPEN_PC,
        .pressed = true,
    };
    if (gbrt_port_input(context, &event) != GB_PORT_OK) {
        return gb_native_fail(
            call, "native BillsPC requires the exact-ROM Crystal port module");
    }

    /*
     * Native edits remain runtime-owned semantic transactions. The original
     * body still owns guest timing, farcall return state, and the normal
     * save-related safepoints captured by this replacement frame.
     */
    return gb_native_call_original(call);
}
