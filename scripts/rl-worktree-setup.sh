#!/usr/bin/env bash
# Populate a fresh worktree with the git-ignored build inputs (baserom, extracted asm/assets,
# built tools, venv) from the project root checkout, then verify the vanilla build still matches.
set -euo pipefail

ROOT="${COPILOT_ROOT_PATH:-$HOME/workspace/pokestadium-roguelike}"
DEST="${COPILOT_WORKSPACE_PATH:-$(pwd)}"

if [ "$(realpath "$ROOT")" = "$(realpath "$DEST")" ]; then
    echo "Running in the root checkout; nothing to copy."
    exit 0
fi

cd "$DEST"
copy() {
    local p="$1"
    if [ -e "$ROOT/$p" ]; then
        mkdir -p "$(dirname "$p")"
        rsync -a --delete "$ROOT/$p" "$(dirname "$p")/"
    fi
}

for p in baseroms/us/baserom.z64 .venv .splat asm assets expected build \
         linker_scripts/us/auto linker_scripts/us/pokestadium.ld pokestadium-us.yaml \
         lib/ultralib/build tools/vtxdis; do
    copy "$p"
done

# Rebuild tools that are compiled in-tree (cheap if already up to date).
make setup >/dev/null 2>&1 || true

echo "Verifying vanilla matching build..."
make -j"$(nproc)" 2>&1 | tail -3
