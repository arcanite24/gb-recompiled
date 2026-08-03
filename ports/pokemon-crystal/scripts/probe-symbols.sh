#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
port_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_root=$(CDPATH= cd -- "$port_dir/../.." && pwd)

rom_path=${1:-"$repo_root/roms/selected_gbc_top10/pokemon_crystal.gbc"}
gbrecomp_bin=${GBRECOMP_BIN:-"$repo_root/build/bin/gbrecomp"}
symbol_path="$port_dir/references/vendor/pokecrystal-symbols/pokecrystal11.sym"

python3 "$script_dir/verify_rom.py" "$rom_path"

if [ ! -x "$gbrecomp_bin" ]; then
    echo "error: missing GB Recompiled executable: $gbrecomp_bin" >&2
    exit 1
fi

exec "$gbrecomp_bin" "$rom_path" \
    --symbols "$symbol_path" \
    --symbol-policy names-only \
    --analyze \
    --limit 50000
