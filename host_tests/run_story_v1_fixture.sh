#!/usr/bin/env bash
# Generate a genuine host save using the already accepted original v1 codec.
set -euo pipefail
cd "$(dirname "$0")/.."
root=build/story/v1-fixture
mkdir -p "$root"
git archive 764fb69435fdcaa24e7627668b3b97d832b6d5ba \
    src/story src/roguelike/rl_core.h src/roguelike/rl_types.h \
    tools/story_data.py tools/story_graph.py \
    | tar -x -C "$root"
python3 "$root/tools/story_data.py" --cache .local/story-source \
    --out "$root/data.json" --c-out "$root/sc_data.c"
"${CC:-gcc}" -std=c89 -pedantic -Wall -Wextra -Werror -O2 -I"$root" \
    host_tests/story_v1_fixture.c "$root/src/story/sc_core.c" \
    "$root/src/story/sc_make.c" "$root/src/story/sc_combat.c" \
    "$root/src/story/sc_graph.c" "$root/src/story/sc_progression.c" \
    "$root/src/story/sc_save.c" "$root/src/story/sc_crc.c" \
    "$root/sc_data.c" -o "$root/generate"
"$root/generate" > "$root/original-v1.sc01"
printf '%s  %s\n' \
    90e3def36936868c973c7b8c42bfd5813493ffd1a78fce4977df74203b2e8d57 \
    "$root/original-v1.sc01" | sha256sum -c
sha256sum "$root/original-v1.sc01"
