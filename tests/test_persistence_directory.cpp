#include "gbrt.h"
#include "platform_sdl.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>

static GBContext* make_context(uint64_t unix_time) {
    GBConfig config = {};
    config.rtc_unix_time_override_enabled = true;
    config.rtc_unix_time_override = unix_time;
    GBContext* ctx = gb_context_create(&config);
    if (!ctx) {
        return nullptr;
    }
    gb_context_set_save_id(ctx, "isolated");
    gb_platform_register_context(ctx);

    uint8_t rom[32u * 1024u] = {};
    rom[0x147] = 0x10;  // MBC3 + timer + RAM + battery
    rom[0x149] = 0x02;  // 8 KiB external RAM
    if (!gb_context_load_rom(ctx, rom, sizeof(rom))) {
        gb_context_destroy(ctx);
        return nullptr;
    }
    return ctx;
}

static std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

static bool write_file(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return stream.good();
}

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("gbrecomp-persistence-" + std::to_string((long long)getpid()));
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    if (!std::filesystem::create_directory(directory, error) || error) {
        std::fputs("failed to create persistence test directory\n", stderr);
        return 2;
    }
    if (!gb_platform_set_persistence_dir(directory.string().c_str())) {
        std::fputs("existing persistence directory was rejected\n", stderr);
        std::filesystem::remove_all(directory, error);
        return 1;
    }

    GBContext* ctx = make_context(1000);
    if (!ctx) {
        std::filesystem::remove_all(directory, error);
        return 2;
    }
    ctx->eram[0] = 0x5A;
    ctx->rtc.s = 4;
    ctx->rtc.m = 3;
    ctx->rtc.h = 2;
    ctx->rtc.dl = 1;
    ctx->rtc.dh = 0;
    ctx->rtc.active = true;
    const bool saved = gb_context_save_ram(ctx);
    gb_context_destroy(ctx);

    const std::filesystem::path save = directory / "isolated.sav";
    const std::filesystem::path rtc = directory / "isolated.rtc";
    const std::vector<uint8_t> good_save = read_file(save);
    const std::vector<uint8_t> good_rtc = read_file(rtc);
    bool passed =
        saved &&
        good_save.size() == 8192u &&
        good_rtc.size() == 40u &&
        good_rtc[4] == 2u &&
        !std::filesystem::exists(save.string() + ".tmp-v1") &&
        !std::filesystem::exists(rtc.string() + ".tmp-v1");

    GBContext* fault_context = make_context(1000);
    if (!fault_context) {
        std::filesystem::remove_all(directory, error);
        return 2;
    }
    std::vector<uint8_t> changed_save = good_save;
    changed_save[0] = 0xA6;
    std::vector<uint8_t> changed_rtc = good_rtc;
    changed_rtc[24] = 42;

    for (GBPersistenceTestFault fault : {
             GB_PERSISTENCE_TEST_FAULT_SHORT_WRITE,
             GB_PERSISTENCE_TEST_FAULT_FULL_DISK,
             GB_PERSISTENCE_TEST_FAULT_TRUNCATION,
         }) {
        gb_platform_test_inject_persistence_fault(
            GB_PERSISTENCE_TEST_TARGET_BATTERY, fault);
        passed = passed &&
            !fault_context->callbacks.save_battery_ram(
                fault_context,
                "isolated",
                changed_save.data(),
                changed_save.size()) &&
            read_file(save) == good_save &&
            !std::filesystem::exists(save.string() + ".tmp-v1");
    }

    gb_platform_test_inject_persistence_fault(
        GB_PERSISTENCE_TEST_TARGET_BATTERY,
        GB_PERSISTENCE_TEST_FAULT_INTERRUPTION);
    passed = passed &&
        !fault_context->callbacks.save_battery_ram(
            fault_context,
            "isolated",
            changed_save.data(),
            changed_save.size()) &&
        read_file(save) == good_save &&
        std::filesystem::exists(save.string() + ".tmp-v1");

    GBContext* interrupted_restart = make_context(1000);
    if (!interrupted_restart) {
        std::filesystem::remove_all(directory, error);
        return 2;
    }
    passed = passed &&
        interrupted_restart->eram[0] == 0x5A &&
        std::filesystem::exists(save.string() + ".tmp-v1");
    interrupted_restart->persistence_load_failed = true;
    gb_context_destroy(interrupted_restart);

    for (GBPersistenceTestFault fault : {
             GB_PERSISTENCE_TEST_FAULT_SHORT_WRITE,
             GB_PERSISTENCE_TEST_FAULT_FULL_DISK,
             GB_PERSISTENCE_TEST_FAULT_TRUNCATION,
         }) {
        gb_platform_test_inject_persistence_fault(
            GB_PERSISTENCE_TEST_TARGET_RTC, fault);
        passed = passed &&
            !fault_context->callbacks.save_rtc_data(
                fault_context,
                "isolated",
                changed_rtc.data(),
                changed_rtc.size()) &&
            read_file(rtc) == good_rtc &&
            !std::filesystem::exists(rtc.string() + ".tmp-v1");
    }

    gb_platform_test_inject_persistence_fault(
        GB_PERSISTENCE_TEST_TARGET_RTC,
        GB_PERSISTENCE_TEST_FAULT_INTERRUPTION);
    passed = passed &&
        !fault_context->callbacks.save_rtc_data(
            fault_context,
            "isolated",
            changed_rtc.data(),
            changed_rtc.size()) &&
        read_file(rtc) == good_rtc &&
        std::filesystem::exists(rtc.string() + ".tmp-v1") &&
        write_file(rtc.string() + ".tmp-v1", {0xAA, 0xBB}) &&
        fault_context->callbacks.save_rtc_data(
            fault_context,
            "isolated",
            changed_rtc.data(),
            changed_rtc.size()) &&
        read_file(rtc) == changed_rtc &&
        !std::filesystem::exists(rtc.string() + ".tmp-v1") &&
        fault_context->callbacks.save_rtc_data(
            fault_context,
            "isolated",
            good_rtc.data(),
            good_rtc.size()) &&
        read_file(rtc) == good_rtc;

    passed = passed &&
        write_file(save.string() + ".tmp-v1", {0xCC, 0xDD}) &&
        fault_context->callbacks.save_battery_ram(
            fault_context,
            "isolated",
            changed_save.data(),
            changed_save.size()) &&
        read_file(save) == changed_save &&
        !std::filesystem::exists(save.string() + ".tmp-v1") &&
        fault_context->callbacks.save_battery_ram(
            fault_context,
            "isolated",
            good_save.data(),
            good_save.size()) &&
        read_file(save) == good_save;
    fault_context->persistence_load_failed = true;
    gb_context_destroy(fault_context);

    std::vector<uint8_t> legacy_rtc = good_rtc;
    legacy_rtc[4] = 1;
    passed = passed && write_file(rtc, legacy_rtc);
    GBContext* legacy_restart = make_context(1000);
    if (!legacy_restart) {
        std::filesystem::remove_all(directory, error);
        return 2;
    }
    passed = passed &&
        legacy_restart->rtc.s == 4 &&
        legacy_restart->rtc.m == 3 &&
        legacy_restart->rtc.h == 2;
    gb_context_destroy(legacy_restart);
    const std::vector<uint8_t> migrated_rtc = read_file(rtc);
    passed = passed &&
        migrated_rtc.size() == 40u &&
        migrated_rtc[4] == 2u;

    GBContext* restart_one = make_context(1060);
    if (!restart_one) {
        std::filesystem::remove_all(directory, error);
        return 2;
    }
    gb_context_reset(restart_one, true);
    passed = passed &&
        restart_one->eram[0] == 0x5A &&
        restart_one->rtc.s == 4 &&
        restart_one->rtc.m == 4 &&
        restart_one->rtc.h == 2 &&
        restart_one->rtc.dl == 1;
    gb_context_destroy(restart_one);

    GBContext* restart_two = make_context(1120);
    if (!restart_two) {
        std::filesystem::remove_all(directory, error);
        return 2;
    }
    gb_context_reset(restart_two, true);
    passed = passed &&
        restart_two->eram[0] == 0x5A &&
        restart_two->rtc.s == 4 &&
        restart_two->rtc.m == 5 &&
        restart_two->rtc.h == 2 &&
        restart_two->rtc.dl == 1;
    gb_context_destroy(restart_two);

    const std::vector<uint8_t> truncated_save = {0xA5};
    passed = passed && write_file(save, truncated_save);
    GBContext* bad_save = make_context(1180);
    if (!bad_save) {
        std::filesystem::remove_all(directory, error);
        return 2;
    }
    passed = passed && bad_save->eram[0] == 0;
    gb_context_destroy(bad_save);
    passed = passed && read_file(save) == truncated_save;

    passed = passed && write_file(save, good_save);
    std::vector<uint8_t> unsupported_rtc = migrated_rtc;
    unsupported_rtc[4] = 99;
    passed = passed && write_file(rtc, unsupported_rtc);
    GBContext* bad_rtc_version = make_context(1240);
    if (!bad_rtc_version) {
        std::filesystem::remove_all(directory, error);
        return 2;
    }
    passed = passed &&
        bad_rtc_version->eram[0] == 0x5A &&
        bad_rtc_version->rtc.s == 0 &&
        bad_rtc_version->persistence_load_failed;
    gb_context_destroy(bad_rtc_version);
    passed = passed && read_file(rtc) == unsupported_rtc;

    const std::vector<uint8_t> mismatched_rtc = {0x43, 0x54, 0x52, 0x47};
    passed = passed && write_file(rtc, mismatched_rtc);
    GBContext* bad_rtc = make_context(1240);
    if (!bad_rtc) {
        std::filesystem::remove_all(directory, error);
        return 2;
    }
    passed = passed &&
        bad_rtc->eram[0] == 0x5A &&
        bad_rtc->rtc.s == 0 &&
        bad_rtc->rtc.m == 0 &&
        bad_rtc->rtc.h == 0;
    gb_context_destroy(bad_rtc);
    passed = passed && read_file(rtc) == mismatched_rtc;

    std::filesystem::remove_all(directory, error);
    if (!passed) {
        std::fputs(
            "save identity, RTC restart, or rejected-file preservation failed\n",
            stderr);
        return 1;
    }
    return 0;
}
