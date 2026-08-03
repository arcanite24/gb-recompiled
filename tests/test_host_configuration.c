#include "gbrt_host_configuration.h"

#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

static const GBHostConfigurationContract contract = {
    GB_HOST_CONFIGURATION_ABI_VERSION,
    "gbrecomp.host-configuration",
    1,
    "challenge-v1",
    -5,
    5,
    1,
    100,
};

static int expect_status(
    const char* text,
    GBHostConfigurationStatus expected) {
    GBHostConfiguration value;
    memset(&value, 0xa5, sizeof(value));
    return gbrt_host_configuration_parse(
               (const uint8_t*)text,
               strlen(text),
               &contract,
               &value) == expected &&
           (expected == GB_HOST_CONFIGURATION_OK || value.present == 0);
}

int main(void) {
    static const char canonical[] =
        "{\"schema\":\"gbrecomp.host-configuration\",\"version\":1,"
        "\"policy_id\":\"challenge-v1\",\"applied\":true,\"enabled\":true,"
        "\"offset\":3,\"minimum\":1,\"maximum\":100}\n";
    static const char expected_hash[] =
        "f9ee2131f80d8194535bc7cd50fc26593516afb948506869350a14b5a5353f30";
    GBHostConfiguration value;
    char serialized[GB_HOST_CONFIGURATION_CANONICAL_CAPACITY];
    size_t serialized_size = 0;

    if (gbrt_host_configuration_parse(
            (const uint8_t*)canonical,
            strlen(canonical),
            &contract,
            &value) != GB_HOST_CONFIGURATION_OK ||
        !value.present || !value.applied || !value.enabled ||
        value.offset != 3 || value.minimum != 1 || value.maximum != 100 ||
        strcmp(value.sha256, expected_hash) != 0 ||
        gbrt_host_configuration_serialize(
            &value, serialized, sizeof(serialized), &serialized_size) !=
            GB_HOST_CONFIGURATION_OK ||
        serialized_size != strlen(canonical) ||
        memcmp(serialized, canonical, serialized_size) != 0) {
        return 1;
    }

    if (!expect_status(
            "{\"schema\":\"gbrecomp.host-configuration\",\"version\":1,"
            "\"policy_id\":\"challenge-v1\",\"applied\":true,"
            "\"enabled\":true,\"offset\":+3,\"minimum\":1,\"maximum\":100}\n",
            GB_HOST_CONFIGURATION_NON_CANONICAL) ||
        !expect_status("{}\n", GB_HOST_CONFIGURATION_MALFORMED) ||
        !expect_status(
            "{\"schema\":\"wrong\",\"version\":1,\"policy_id\":"
            "\"challenge-v1\",\"applied\":true,\"enabled\":true,"
            "\"offset\":3,\"minimum\":1,\"maximum\":100}\n",
            GB_HOST_CONFIGURATION_SCHEMA_MISMATCH) ||
        !expect_status(
            "{\"schema\":\"gbrecomp.host-configuration\",\"version\":2,"
            "\"policy_id\":\"challenge-v1\",\"applied\":true,"
            "\"enabled\":true,\"offset\":3,\"minimum\":1,\"maximum\":100}\n",
            GB_HOST_CONFIGURATION_SCHEMA_MISMATCH) ||
        !expect_status(
            "{\"schema\":\"gbrecomp.host-configuration\",\"version\":1,"
            "\"policy_id\":\"other-v1\",\"applied\":true,"
            "\"enabled\":true,\"offset\":3,\"minimum\":1,\"maximum\":100}\n",
            GB_HOST_CONFIGURATION_POLICY_MISMATCH) ||
        !expect_status(
            "{\"schema\":\"gbrecomp.host-configuration\",\"version\":1,"
            "\"policy_id\":\"challenge-v1\",\"applied\":true,"
            "\"enabled\":true,\"offset\":6,\"minimum\":1,\"maximum\":100}\n",
            GB_HOST_CONFIGURATION_OUT_OF_RANGE) ||
        strcmp(
            gbrt_host_configuration_status_string(
                GB_HOST_CONFIGURATION_POLICY_MISMATCH),
            "policy-mismatch") != 0) {
        return 1;
    }

#ifndef _WIN32
    char path[] = "/tmp/gbrecomp-host-configuration-XXXXXX";
    const int descriptor = mkstemp(path);
    if (descriptor < 0 || close(descriptor) != 0 || remove(path) != 0 ||
        gbrt_host_configuration_write_file(path, &value) !=
            GB_HOST_CONFIGURATION_OK) {
        return 1;
    }
    GBHostConfiguration loaded;
    if (gbrt_host_configuration_load_file(path, &contract, &loaded) !=
            GB_HOST_CONFIGURATION_OK ||
        strcmp(loaded.sha256, expected_hash) != 0 ||
        remove(path) != 0) {
        return 1;
    }
#endif

    return 0;
}
