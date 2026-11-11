#!/usr/bin/env bash
# Record the README demo: NeoVim3D typing a small C++ program while the camera
# follows the cursor, showing an LSP hover window and saving the file.
#
# Neovim runs with your own config (~/.config/nvim), so your colour scheme,
# completion and LSP setup (clangd for C++) are what the recording shows. Set
# NV3D_DEMO_CLEAN=1 for a reproducible recording that ignores personal config
# (examples/demo/nvim.lua sets up clangd instead).
#
#   examples/demo/record.sh [output.webp|output.gif|output.mp4]
#
# Needs a built neovim3d (meson compile -C build), nvim and ffmpeg. A window
# opens for the duration of the recording; leave it alone until it closes.
# Environment: NEOVIM3D_BIN, NEOVIM3D_NVIM, NV3D_DEMO_WIDTH, NV3D_DEMO_FPS,
# NV3D_DEMO_QUALITY, NV3D_DEMO_COLORS, NV3D_DEMO_COLORSCHEME (default habamax),
# NV3D_DEMO_HOVER_KEYS (default <Space>i), NV3D_DEMO_CLEAN, and for the clean
# variant NV3D_DEMO_CLANGD (default: clangd on PATH, or the one Mason installed).
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
bin=${NEOVIM3D_BIN:-$root/build/neovim3d}
out=${1:-$root/docs/demo.webp}
colorscheme=${NV3D_DEMO_COLORSCHEME:-habamax}

if [[ ! -x $bin ]]; then
    echo "record.sh: $bin not found; build first with: meson compile -C build" >&2
    exit 1
fi
command -v ffmpeg >/dev/null || { echo "record.sh: ffmpeg is required" >&2; exit 1; }

mkdir -p "$(dirname "$out")"
out=$(cd "$(dirname "$out")" && pwd)/$(basename "$out")

# Edit a fresh file in a scratch directory so the demo never touches the repo.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"

if [[ ${NV3D_DEMO_CLEAN:-0} == 1 ]]; then
    nvim_args=(--clean --cmd "luafile ${root// /\\ }/examples/demo/nvim.lua")
else
    nvim_args=()
fi

source="$root/examples/demo/hello.cpp"
NV3D_DEMO_SOURCE="$source" NV3D_DEMO_OUT="$out" \
    NV3D_DEMO_COLORSCHEME="$colorscheme" \
    "$bin" --size-3d 1280x720 --config "$root/examples/demo/demo.lua" \
    "${nvim_args[@]}" hello-world.cpp

echo "Wrote $out ($(du -h "$out" | cut -f1))"

# The demo ends with ':w hello-world.cpp'; make sure what got typed is exact.
if ! cmp -s "$source" hello-world.cpp; then
    echo "record.sh: warning: the typed program differs from $source" >&2
    echo "(a plugin or setting in your Neovim config changed the text while typing):" >&2
    diff "$source" hello-world.cpp >&2 || true
fi
