#!/usr/bin/env python3
"""Compile and exercise generated Crystal semantic accessors."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


HARNESS = r"""
#include "crystal_semantic.h"
#include "gbrt.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint8_t durable_save[0x8000];
static bool fail_persistence;

bool gb_context_save_battery_snapshot(
    GBContext* context,
    const uint8_t* data,
    size_t size) {
    (void)context;
    if (fail_persistence || data == NULL || size != sizeof(durable_save)) {
        return false;
    }
    memcpy(durable_save, data, size);
    return true;
}

static bool read_live(void* user, GBSemanticMemorySpace space,
                      uint16_t bank, uint16_t address,
                      uint8_t* output, size_t width) {
    const uint8_t* wram = (const uint8_t*)user;
    if (space != GB_SEMANTIC_BANKED_WRAM || bank != 1 ||
        address < 0xD000 || address >= 0xE000) {
        return false;
    }
    const size_t offset =
        (size_t)bank * 0x1000u + (size_t)(address - 0xD000u);
    if (offset > 0x8000u || width > 0x8000u - offset) {
        return false;
    }
    memcpy(output, wram + offset, width);
    return true;
}

static bool read_rom(void* user, GBSemanticMemorySpace space,
                     uint16_t bank, uint16_t address,
                     uint8_t* output, size_t width) {
    const uint8_t* rom = (const uint8_t*)user;
    if (space != GB_SEMANTIC_PHYSICAL_ROM ||
        (bank == 0 && address >= 0x4000u) ||
        (bank != 0 && (address < 0x4000u || address >= 0x8000u))) {
        return false;
    }
    const size_t offset = bank == 0
        ? address
        : (size_t)bank * 0x4000u + (size_t)(address - 0x4000u);
    if (offset > 0x200000u || width > 0x200000u - offset) {
        return false;
    }
    memcpy(output, rom + offset, width);
    return true;
}

static void set_pair(uint8_t* wram, size_t live_offset,
                     uint8_t* save, size_t save_offset,
                     const uint8_t* data, size_t width) {
    memcpy(wram + live_offset, data, width);
    memcpy(save + save_offset, data, width);
}

static void encoded_name(uint8_t* output, uint8_t letter) {
    memset(output, 0x50, CRYSTAL_NAME_LENGTH);
    output[0] = letter;
}

static void make_valid_party(uint8_t* party, uint8_t species) {
    memset(party, 0, CRYSTAL_PARTY_RECORD_SIZE);
    party[0] = 1;
    party[1] = species;
    party[2] = 0xff;
    party[8] = species;
    party[8 + 31] = 7;
    party[8 + 34] = 0;
    party[8 + 35] = 20;
    party[8 + 36] = 0;
    party[8 + 37] = 20;
    encoded_name(party + 296, 0x80);
    encoded_name(party + 362, 0x81);
}

static void make_valid_box(uint8_t* box, uint8_t species) {
    memset(box, 0, CRYSTAL_ACTIVE_BOX_RECORD_SIZE);
    box[0] = 1;
    box[1] = species;
    box[2] = 0xff;
    box[22] = species;
    box[22 + 31] = 7;
    encoded_name(box + 662, 0x80);
    encoded_name(box + 882, 0x81);
}

static void update_checksum(
    uint8_t* save,
    size_t check1,
    size_t start,
    size_t end,
    size_t checksum,
    size_t check2) {
    uint16_t value = 0;
    save[check1] = 99;
    save[check2] = 127;
    for (size_t index = start; index < end; ++index) {
        value = (uint16_t)(value + save[index]);
    }
    save[checksum] = (uint8_t)value;
    save[checksum + 1] = (uint8_t)(value >> 8u);
}

int main(void) {
    uint8_t wram[0x8000] = {0};
    uint8_t save[0x8000] = {0};
    static uint8_t rom[0x200000] = {0};
    const uint8_t location[4] = {3, 4, 12, 9};
    uint8_t party[CRYSTAL_PARTY_RECORD_SIZE] = {
        2, 25, 152, 0xff, 0xff, 0xff, 0xff
    };
    const uint8_t badges[2] = {0x13, 0x80};
    uint8_t pokedex[64] = {0};
    pokedex[0] = 0x05;
    pokedex[31] = 0x80;
    pokedex[32] = 0x0f;
    pokedex[40] = 0x01;

    set_pair(wram, 0x1cb5, save, 0x2843, location, sizeof(location));
    set_pair(wram, 0x1cd7, save, 0x2865, party, sizeof(party));
    set_pair(wram, 0x1857, save, 0x23e5, badges, sizeof(badges));
    set_pair(wram, 0x1e99, save, 0x2a27, pokedex, sizeof(pokedex));
    memcpy(save + 0x1a43, location, sizeof(location));
    memcpy(save + 0x1a65, party, sizeof(party));
    memcpy(save + 0x15e5, badges, sizeof(badges));
    memcpy(save + 0x1c27, pokedex, sizeof(pokedex));
    update_checksum(save, 0x2008, 0x2009, 0x2b83, 0x2d0d, 0x2d0f);
    update_checksum(save, 0x1208, 0x1209, 0x1d83, 0x1f0d, 0x1f0f);

    GBSemanticReader live = {
        .abi_version = GB_SEMANTIC_READER_ABI_VERSION,
        .rom_sha256 = CRYSTAL_SEMANTIC_ROM_SHA256,
        .mode = GB_SEMANTIC_READ_LIVE,
        .user = wram,
        .read = read_live,
    };
    const size_t base_offset =
        20u * 0x4000u + (0x5424u - 0x4000u) + 154u * 32u;
    const uint8_t base_data[32] = {
        155, 39, 52, 43, 65, 60, 50, 20, 20, 45, 65
    };
    memcpy(rom + base_offset, base_data, sizeof(base_data));
    const size_t pointer_offset =
        16u * 0x4000u + (0x65b1u - 0x4000u) + 154u * 2u;
    rom[pointer_offset] = 0x00;
    rom[pointer_offset + 1] = 0x70;
    const uint8_t evolution_moves[] = {
        1, 14, 156, 0,
        1, 33,
        6, 108,
        0,
    };
    memcpy(
        rom + 16u * 0x4000u + (0x7000u - 0x4000u),
        evolution_moves,
        sizeof(evolution_moves));

    GBSemanticSaveSource save_source = {
        .data = save,
        .size = sizeof(save),
        .rom = rom,
        .rom_size = sizeof(rom),
    };
    GBSemanticReader saved = {0};
    if (gbrt_semantic_reader_init_save(
            &saved, &save_source, CRYSTAL_SEMANTIC_ROM_SHA256) !=
        GB_SEMANTIC_OK) {
        return 1;
    }

    CrystalLocation live_location = {0}, saved_location = {0};
    CrystalParty live_party = {0}, saved_party = {0};
    CrystalBadges live_badges = {0}, saved_badges = {0};
    CrystalPokedexProgress live_dex = {0}, saved_dex = {0};
    if (crystal_semantic_read_location(
            &live, GB_SEMANTIC_READ_LIVE, &live_location) != GB_SEMANTIC_OK ||
        crystal_semantic_read_location(
            &saved, GB_SEMANTIC_READ_SAVE, &saved_location) != GB_SEMANTIC_OK ||
        memcmp(&live_location, &saved_location, sizeof(live_location)) != 0 ||
        live_location.map_group != 3 || live_location.map_number != 4 ||
        live_location.y != 12 || live_location.x != 9) {
        return 2;
    }
    if (crystal_semantic_read_party(
            &live, GB_SEMANTIC_READ_LIVE, &live_party) != GB_SEMANTIC_OK ||
        crystal_semantic_read_party(
            &saved, GB_SEMANTIC_READ_SAVE, &saved_party) != GB_SEMANTIC_OK ||
        memcmp(&live_party, &saved_party, sizeof(live_party)) != 0 ||
        live_party.count != 2 || live_party.species[0] != 25 ||
        live_party.species[1] != 152) {
        return 3;
    }
    if (crystal_semantic_read_badges(
            &live, GB_SEMANTIC_READ_LIVE, &live_badges) != GB_SEMANTIC_OK ||
        crystal_semantic_read_badges(
            &saved, GB_SEMANTIC_READ_SAVE, &saved_badges) != GB_SEMANTIC_OK ||
        memcmp(&live_badges, &saved_badges, sizeof(live_badges)) != 0 ||
        live_badges.johto_count != 3 || live_badges.kanto_count != 1 ||
        live_badges.total_count != 4) {
        return 4;
    }
    if (crystal_semantic_read_pokedex(
            &live, GB_SEMANTIC_READ_LIVE, &live_dex) != GB_SEMANTIC_OK ||
        crystal_semantic_read_pokedex(
            &saved, GB_SEMANTIC_READ_SAVE, &saved_dex) != GB_SEMANTIC_OK ||
        memcmp(&live_dex, &saved_dex, sizeof(live_dex)) != 0 ||
        live_dex.caught_count != 3 || live_dex.seen_count != 5) {
        return 5;
    }
    save[0x2008] = 0;
    memset(&saved_location, 0, sizeof(saved_location));
    if (crystal_semantic_read_location(
            &saved, GB_SEMANTIC_READ_SAVE, &saved_location) !=
            GB_SEMANTIC_OK ||
        memcmp(&live_location, &saved_location, sizeof(live_location)) != 0) {
        return 19;
    }
    save[0x1208] = 0;
    if (crystal_semantic_read_location(
            &saved, GB_SEMANTIC_READ_SAVE, &saved_location) !=
            GB_SEMANTIC_INVALID_DATA) {
        return 20;
    }
    save[0x2008] = 99;
    save[0x1208] = 99;
    GBSemanticReader live_rom = {
        .abi_version = GB_SEMANTIC_READER_ABI_VERSION,
        .rom_sha256 = CRYSTAL_SEMANTIC_ROM_SHA256,
        .mode = GB_SEMANTIC_READ_LIVE,
        .user = rom,
        .read = read_rom,
    };
    CrystalSpeciesPage live_species = {0}, saved_species = {0};
    if (crystal_semantic_read_species(
            &live_rom, GB_SEMANTIC_READ_LIVE, 155, &live_species) !=
            GB_SEMANTIC_OK ||
        crystal_semantic_read_species(
            &saved, GB_SEMANTIC_READ_SAVE, 155, &saved_species) !=
            GB_SEMANTIC_OK ||
        memcmp(&live_species, &saved_species, sizeof(live_species)) != 0 ||
        live_species.hp != 39 || live_species.attack != 52 ||
        live_species.evolution_count != 1 ||
        live_species.evolutions[0].method != 1 ||
        live_species.evolutions[0].parameter != 14 ||
        live_species.evolutions[0].target_species != 156 ||
        live_species.level_move_count != 2 ||
        live_species.level_moves[0].level != 1 ||
        live_species.level_moves[0].move_id != 33 ||
        live_species.encounter_knowledge !=
            CRYSTAL_KNOWLEDGE_NOT_MODELED) {
        return 10;
    }

    if (crystal_semantic_read_view(
            &live, GB_SEMANTIC_READ_LIVE, CRYSTAL_SEMANTIC_LOCATION,
            &live_location, sizeof(live_location) - 1) !=
        GB_SEMANTIC_OUT_OF_RANGE) {
        return 6;
    }
    if (crystal_semantic_read_location(
            &saved, GB_SEMANTIC_READ_LIVE, &saved_location) !=
        GB_SEMANTIC_WRONG_MODE) {
        return 7;
    }
    uint8_t raw[4];
    if (gbrt_semantic_read(
            &live, CRYSTAL_SEMANTIC_ROM_SHA256, GB_SEMANTIC_READ_LIVE,
            GB_SEMANTIC_BANKED_WRAM, 2, 0xDCB5, raw, sizeof(raw)) !=
        GB_SEMANTIC_READ_FAILED) {
        return 8;
    }
    wram[0x1cd7] = 7;
    if (crystal_semantic_read_party(
            &live, GB_SEMANTIC_READ_LIVE, &live_party) !=
        GB_SEMANTIC_INVALID_DATA) {
        return 9;
    }

    uint8_t encoded[CRYSTAL_NAME_LENGTH];
    if (crystal_semantic_encode_name("Crystal9", encoded) !=
            GB_SEMANTIC_OK ||
        encoded[0] != 0x82 || encoded[7] != 0xff ||
        encoded[8] != 0x50 ||
        crystal_semantic_encode_name("bad!", encoded) !=
            GB_SEMANTIC_INVALID_DATA ||
        crystal_semantic_encode_name("abcdefghijk", encoded) !=
            GB_SEMANTIC_OUT_OF_RANGE) {
        return 11;
    }

    uint8_t valid_party[CRYSTAL_PARTY_RECORD_SIZE];
    uint8_t valid_box[CRYSTAL_ACTIVE_BOX_RECORD_SIZE];
    make_valid_party(valid_party, 155);
    make_valid_box(valid_box, 156);
    memcpy(wram + 0x1cd7, valid_party, sizeof(valid_party));
    memcpy(save + 0x2865, valid_party, sizeof(valid_party));
    memcpy(save + 0x1a65, valid_party, sizeof(valid_party));
    memcpy(save + 0x2d10, valid_box, sizeof(valid_box));
    save[0x2700] = 0;
    for (size_t box_index = 0; box_index < CRYSTAL_BOX_COUNT; ++box_index) {
        const size_t box_offset =
            (box_index < 7 ? 0x4000u : 0x6000u) +
            (box_index % 7u) * CRYSTAL_ACTIVE_BOX_RECORD_SIZE;
        save[box_offset] = 0;
        save[box_offset + 1] = 0xFFu;
    }
    memcpy(save + 0x4000, valid_box, sizeof(valid_box));
    update_checksum(save, 0x2008, 0x2009, 0x2b83, 0x2d0d, 0x2d0f);
    update_checksum(save, 0x1208, 0x1209, 0x1d83, 0x1f0d, 0x1f0f);
    memcpy(durable_save, save, sizeof(save));

    GBContext context = {0};
    context.rom = rom;
    context.rom_size = sizeof(rom);
    context.eram = save;
    context.eram_size = sizeof(save);
    context.wram = wram;
    GBSemanticTransaction transaction = {0};
    uint8_t edited_party[CRYSTAL_PARTY_RECORD_SIZE];
    memcpy(edited_party, valid_party, sizeof(edited_party));
    encoded_name(edited_party + 362, 0x83);
    if (gbrt_semantic_transaction_begin(
            &transaction,
            &context,
            CRYSTAL_SEMANTIC_ROM_SHA256,
            CRYSTAL_SEMANTIC_ROM_SHA256) != GB_SEMANTIC_OK ||
        crystal_semantic_stage_party(
            &transaction, edited_party, sizeof(edited_party)) !=
            GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_validate(
            &transaction, crystal_semantic_validate_transaction, NULL) !=
            GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_commit(&transaction) != GB_SEMANTIC_OK ||
        memcmp(wram + 0x1cd7, edited_party, sizeof(edited_party)) != 0 ||
        memcmp(save + 0x2865, edited_party, sizeof(edited_party)) != 0 ||
        memcmp(save + 0x1a65, edited_party, sizeof(edited_party)) != 0 ||
        memcmp(durable_save, save, sizeof(save)) != 0 ||
        context.semantic_transaction_outcome !=
            GB_SEMANTIC_TRANSACTION_COMMITTED ||
        context.semantic_transaction_dirty_count != 7) {
        return 12;
    }

    uint8_t invalid_party[CRYSTAL_PARTY_RECORD_SIZE];
    memcpy(invalid_party, edited_party, sizeof(invalid_party));
    invalid_party[0] = 7;
    if (gbrt_semantic_transaction_begin(
            &transaction,
            &context,
            CRYSTAL_SEMANTIC_ROM_SHA256,
            CRYSTAL_SEMANTIC_ROM_SHA256) != GB_SEMANTIC_OK ||
        crystal_semantic_stage_party(
            &transaction, invalid_party, sizeof(invalid_party)) !=
            GB_SEMANTIC_INVALID_DATA ||
        crystal_semantic_stage_party(
            &transaction, edited_party, sizeof(edited_party) - 1) !=
            GB_SEMANTIC_INVALID_DATA ||
        gbrt_semantic_transaction_abort(&transaction) != GB_SEMANTIC_OK) {
        return 13;
    }
    memcpy(invalid_party, edited_party, sizeof(invalid_party));
    invalid_party[1] = 0;
    if (gbrt_semantic_transaction_begin(
            &transaction,
            &context,
            CRYSTAL_SEMANTIC_ROM_SHA256,
            CRYSTAL_SEMANTIC_ROM_SHA256) != GB_SEMANTIC_OK ||
        crystal_semantic_stage_party(
            &transaction, invalid_party, sizeof(invalid_party)) !=
            GB_SEMANTIC_INVALID_DATA ||
        gbrt_semantic_transaction_abort(&transaction) != GB_SEMANTIC_OK) {
        return 14;
    }

    uint8_t invalid_box[CRYSTAL_ACTIVE_BOX_RECORD_SIZE];
    memcpy(invalid_box, valid_box, sizeof(invalid_box));
    invalid_box[0] = 21;
    if (gbrt_semantic_transaction_begin(
            &transaction,
            &context,
            CRYSTAL_SEMANTIC_ROM_SHA256,
            CRYSTAL_SEMANTIC_ROM_SHA256) != GB_SEMANTIC_OK ||
        crystal_semantic_stage_active_box(
            &transaction, invalid_box, sizeof(invalid_box)) !=
            GB_SEMANTIC_INVALID_DATA ||
        gbrt_semantic_transaction_abort(&transaction) != GB_SEMANTIC_OK) {
        return 15;
    }

    uint8_t edited_box[CRYSTAL_ACTIVE_BOX_RECORD_SIZE];
    memcpy(edited_box, valid_box, sizeof(edited_box));
    encoded_name(edited_box + 882, 0x85);
    if (gbrt_semantic_transaction_begin(
            &transaction,
            &context,
            CRYSTAL_SEMANTIC_ROM_SHA256,
            CRYSTAL_SEMANTIC_ROM_SHA256) != GB_SEMANTIC_OK ||
        crystal_semantic_stage_active_box(
            &transaction, edited_box, sizeof(edited_box)) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_validate(
            &transaction, crystal_semantic_validate_transaction, NULL) !=
            GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_commit(&transaction) != GB_SEMANTIC_OK ||
        memcmp(save + 0x2d10, edited_box, sizeof(edited_box)) != 0 ||
        memcmp(save + 0x4000, edited_box, sizeof(edited_box)) != 0 ||
        memcmp(durable_save, save, sizeof(save)) != 0 ||
        context.semantic_transaction_dirty_count != 2) {
        return 18;
    }

    const uint8_t checksum_breaker = (uint8_t)(save[0x2865] ^ 1u);
    uint8_t before_failure[sizeof(save)];
    memcpy(before_failure, save, sizeof(save));
    if (gbrt_semantic_transaction_begin(
            &transaction,
            &context,
            CRYSTAL_SEMANTIC_ROM_SHA256,
            CRYSTAL_SEMANTIC_ROM_SHA256) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_write(
            &transaction,
            GB_SEMANTIC_EXTERNAL_RAM,
            1,
            0xA865,
            &checksum_breaker,
            1) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_validate(
            &transaction, crystal_semantic_validate_transaction, NULL) !=
            GB_SEMANTIC_INVALID_DATA ||
        memcmp(save, before_failure, sizeof(save)) != 0 ||
        memcmp(durable_save, before_failure, sizeof(save)) != 0) {
        return 16;
    }

    memcpy(invalid_party, edited_party, sizeof(invalid_party));
    encoded_name(invalid_party + 362, 0x84);
    fail_persistence = true;
    if (gbrt_semantic_transaction_begin(
            &transaction,
            &context,
            CRYSTAL_SEMANTIC_ROM_SHA256,
            CRYSTAL_SEMANTIC_ROM_SHA256) != GB_SEMANTIC_OK ||
        crystal_semantic_stage_party(
            &transaction, invalid_party, sizeof(invalid_party)) !=
            GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_validate(
            &transaction, crystal_semantic_validate_transaction, NULL) !=
            GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_commit(&transaction) !=
            GB_SEMANTIC_COMMIT_FAILED ||
        memcmp(save, before_failure, sizeof(save)) != 0 ||
        memcmp(durable_save, before_failure, sizeof(save)) != 0 ||
        context.semantic_transaction_outcome !=
            GB_SEMANTIC_TRANSACTION_COMMIT_FAILED) {
        return 17;
    }
    fail_persistence = false;
    return 0;
}
"""


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    generator = (
        root
        / "ports"
        / "pokemon-crystal"
        / "scripts"
        / "generate_semantic_accessors.py"
    )
    manifest = root / "ports/pokemon-crystal/semantic/package.json"
    compiler = os.environ.get("CC") or shutil.which("cc")
    if compiler is None:
        raise RuntimeError("no C compiler found")
    with tempfile.TemporaryDirectory() as raw:
        output = Path(raw)
        subprocess.run(
            [
                sys.executable,
                str(generator),
                "--manifest",
                str(manifest),
                "--output-dir",
                str(output),
            ],
            check=True,
        )
        header = (output / "crystal_semantic.h").read_text(encoding="utf-8")
        if "GBContext" in header or "_func_" in header:
            raise AssertionError("generated public API exposed runtime internals")
        harness = output / "harness.c"
        harness.write_text(HARNESS, encoding="utf-8")
        executable = output / "semantic-accessor-test"
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(root / "runtime/include"),
                "-I",
                str(output),
                str(root / "runtime/src/gbrt_semantic.c"),
                str(root / "runtime/src/gbrt_data_mod.c"),
                str(root / "runtime/src/gbrt_hash.c"),
                str(output / "crystal_semantic.c"),
                str(harness),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
