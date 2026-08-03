#!/usr/bin/env python3
"""Build a generated project with a source-built headless port module."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


MODULE = r"""
#include "gbrt_port.h"

typedef struct FixtureState {
    int visible;
} FixtureState;

static FixtureState fixture_state;

static bool activate(void* user, const GBPortServices* services) {
    return user != 0 && services != 0 && services->headless &&
           services->semantic_reader != 0;
}

static void input(void* user, const GBPortServices* services,
                  const GBPortInputEvent* event) {
    (void)services;
    FixtureState* state = (FixtureState*)user;
    if (event->pressed && event->action == GB_PORT_INPUT_TOGGLE_UI) {
        state->visible = !state->visible;
    }
}

static void render(void* user, const GBPortServices* services,
                   GBPortFrame* frame) {
    (void)services;
    if (((FixtureState*)user)->visible) {
        gbrt_port_frame_panel(frame, 1, 1, 10, 10, 0xffffffffu);
    }
}

static const GBPortModule module = {
    .abi_version = GB_PORT_ABI_VERSION,
    .module_id = "fixture",
    .module_version = 1,
    .rom_sha256 = "@ROM_SHA256@",
    .rom_size = @ROM_SIZE@u,
    .user = &fixture_state,
    .activate = activate,
    .input = input,
    .render = render,
};

const GBPortModule* gb_port_module_get(void) {
    return &module;
}
"""

EXTENSIONS = r"""
#include "gbrt_port.h"

typedef struct FixtureExtensionState {
    int visible;
} FixtureExtensionState;

static FixtureExtensionState alpha_state;
static FixtureExtensionState beta_state;

static bool extension_activate(
    void* user,
    const GBPortServices* services) {
    return user != 0 && services != 0 && services->headless &&
           services->semantic_reader != 0 &&
           services->semantic_edit_user == 0 &&
           services->run_semantic_edit == 0;
}

static void extension_input(
    void* user,
    const GBPortServices* services,
    const GBPortInputEvent* event) {
    (void)services;
    FixtureExtensionState* state = (FixtureExtensionState*)user;
    if (event->pressed &&
        event->action == GB_PORT_INPUT_TOGGLE_ENCOUNTERS) {
        state->visible = !state->visible;
    }
}

static void render_alpha(
    void* user,
    const GBPortServices* services,
    GBPortFrame* frame) {
    (void)services;
    if (((FixtureExtensionState*)user)->visible) {
        gbrt_port_frame_text(frame, 1, 1, 0xffffffffu, "alpha");
    }
}

static void render_beta(
    void* user,
    const GBPortServices* services,
    GBPortFrame* frame) {
    (void)services;
    if (((FixtureExtensionState*)user)->visible) {
        gbrt_port_frame_text(frame, 1, 2, 0xffffffffu, "beta");
    }
}

static const GBPortExtension alpha = {
    .abi_version = GB_PORT_EXTENSION_ABI_VERSION,
    .extension_id = "fixture.alpha",
    .extension_version = 1,
    .priority = 100,
    .rom_sha256 = "@ROM_SHA256@",
    .rom_size = @ROM_SIZE@u,
    .user = &alpha_state,
    .activate = extension_activate,
    .input = extension_input,
    .render = render_alpha,
};

static const GBPortExtension beta = {
    .abi_version = GB_PORT_EXTENSION_ABI_VERSION,
    .extension_id = "fixture.beta",
    .extension_version = 1,
    .priority = 200,
    .rom_sha256 = "@ROM_SHA256@",
    .rom_size = @ROM_SIZE@u,
    .user = &beta_state,
    .activate = extension_activate,
    .input = extension_input,
    .render = render_beta,
};

const GBPortExtension* fixture_alpha_get(void) {
    return &alpha;
}

const GBPortExtension* fixture_beta_get(void) {
    return &beta;
}
"""

REGISTRY = r"""
#include "gbrt_port.h"

extern const GBPortExtension* fixture_alpha_get(void);
extern const GBPortExtension* fixture_beta_get(void);

static const GBPortExtensionRegistration registrations[] = {
    {
        .get = fixture_alpha_get,
        .expected_id = "fixture.alpha",
        .expected_version = 1,
        .expected_priority = 100,
    },
    {
        .get = fixture_beta_get,
        .expected_id = "fixture.beta",
        .expected_version = 1,
        .expected_priority = 200,
    },
};

static const GBPortExtensionSet extension_set = {
    .abi_version = GB_PORT_EXTENSION_ABI_VERSION,
    .count = sizeof(registrations) / sizeof(registrations[0]),
    .registrations = registrations,
};

const GBPortExtensionSet* gb_port_extension_set_get(void) {
    return &extension_set;
}
"""


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(command, check=True, **kwargs)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gbrecomp", type=Path, required=True)
    parser.add_argument("--fixture-generator", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as raw:
        temp = Path(raw)
        rom = temp / "fixture.gb"
        output = temp / "generated"
        run(
            [
                "python3",
                str(args.fixture_generator),
                "--mapper",
                "mbc1",
                "--output",
                str(rom),
            ]
        )
        run(
            [
                str(args.gbrecomp),
                str(rom),
                "--no-scan",
                "--output",
                str(output),
            ],
            stdout=subprocess.DEVNULL,
        )
        digest = hashlib.sha256(rom.read_bytes()).hexdigest()
        source = (
            MODULE.replace("@ROM_SHA256@", digest)
            .replace("@ROM_SIZE@", str(rom.stat().st_size))
        )
        extensions = (
            EXTENSIONS.replace("@ROM_SHA256@", digest)
            .replace("@ROM_SIZE@", str(rom.stat().st_size))
        )
        (output / "fixture_port.c").write_text(source, encoding="utf-8")
        (output / "fixture_extensions.c").write_text(
            extensions, encoding="utf-8"
        )
        registry = output / "fixture_extension_registry.c"
        registry.write_text(REGISTRY, encoding="utf-8")
        with (output / "CMakeLists.txt").open("a", encoding="utf-8") as handle:
            handle.write(
                "\ntarget_sources(gbrt PRIVATE fixture_port.c "
                "fixture_extensions.c fixture_extension_registry.c)\n"
                "target_compile_definitions(gbrt PUBLIC "
                "GBRT_ENABLE_PORT_MODULE GBRT_ENABLE_PORT_EXTENSIONS)\n"
            )
        build = output / "build"
        run(
            [
                "cmake",
                "-G",
                "Ninja",
                "-S",
                str(output),
                "-B",
                str(build),
                "-DGBRECOMP_GENERATED_OPT_LEVEL=0",
                "-DGBRECOMP_ENABLE_STRIP=OFF",
            ],
            stdout=subprocess.DEVNULL,
        )
        run(["ninja", "-C", str(build)], stdout=subprocess.DEVNULL)
        executable = build / "fixture"

        hidden_guest = temp / "hidden-guest.json"
        hidden_port = temp / "hidden-port.json"
        open_guest = temp / "open-guest.json"
        open_port = temp / "open-port.json"
        toggled_guest = temp / "toggled-guest.json"
        toggled_port = temp / "toggled-port.json"
        extensions_guest = temp / "extensions-guest.json"
        extensions_port = temp / "extensions-port.json"
        common = [
            str(executable),
            "--headless",
            "--limit-frames",
            "2",
            "--ignore-rtc-persistence",
        ]
        run(
            common
            + [
                "--dump-state",
                str(hidden_guest),
                "--port-state",
                str(hidden_port),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=30,
        )
        run(
            common
            + [
                "--port-ui-open",
                "--dump-state",
                str(open_guest),
                "--port-state",
                str(open_port),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=30,
        )
        run(
            common
            + [
                "--port-ui-open",
                "--port-toggle-frame",
                "1",
                "--port-toggle-frame",
                "2",
                "--dump-state",
                str(toggled_guest),
                "--port-state",
                str(toggled_port),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=30,
        )
        run(
            common
            + [
                "--port-input-frame",
                "1:encounters",
                "--dump-state",
                str(extensions_guest),
                "--port-state",
                str(extensions_port),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=30,
        )
        hidden = json.loads(hidden_port.read_text(encoding="utf-8"))
        opened = json.loads(open_port.read_text(encoding="utf-8"))
        toggled = json.loads(toggled_port.read_text(encoding="utf-8"))
        composed = json.loads(extensions_port.read_text(encoding="utf-8"))
        expected_extensions = [
            {"id": "fixture.alpha", "version": 1, "priority": 100},
            {"id": "fixture.beta", "version": 1, "priority": 200},
        ]
        composed_texts = [
            command.get("text")
            for command in composed["frame"]["commands"]
            if command.get("type") == "text"
        ]
        if (
            not hidden["active"]
            or not hidden["headless"]
            or hidden["updates"] != 2
            or hidden["renders"] != 2
            or hidden["last_command_count"] != 0
            or opened["input_events"] != 1
            or opened["last_command_count"] != 1
            or toggled["input_events"] != 3
            or toggled["last_command_count"] != 1
            or hidden["extensions"] != expected_extensions
            or composed["extensions"] != expected_extensions
            or composed["input_events"] != 1
            or composed["last_command_count"] != 2
            or composed_texts != ["alpha", "beta"]
        ):
            raise AssertionError("generated port lifecycle did not run")
        if (
            hidden_guest.read_bytes() != open_guest.read_bytes()
            or hidden_guest.read_bytes() != toggled_guest.read_bytes()
            or hidden_guest.read_bytes() != extensions_guest.read_bytes()
        ):
            raise AssertionError("opening the port changed guest state")
        invalid = subprocess.run(
            common
            + [
                "--port-toggle-frame",
                "2",
                "--port-toggle-frame",
                "1",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=30,
        )
        if invalid.returncode == 0:
            raise AssertionError("unordered port toggle frames were accepted")

        registry.write_text(
            REGISTRY.replace(
                '.expected_version = 1,\n        .expected_priority = 200,',
                '.expected_version = 2,\n        .expected_priority = 200,',
            ),
            encoding="utf-8",
        )
        run(["ninja", "-C", str(build)], stdout=subprocess.DEVNULL)
        rejected = subprocess.run(
            common,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=30,
        )
        if rejected.returncode == 0:
            raise AssertionError(
                "runtime accepted extension registration/descriptor mismatch"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
