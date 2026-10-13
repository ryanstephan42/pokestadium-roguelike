#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
elf="${1:?Supply an audited ELF}"
out="${2:?Supply an isolated output directory}"
python tools/rltest/build_story_ai_prototype.py --elf "$elf" --out "$out"
flags=(-c -O2 -mips2 -G 0 -non_shared -Xcpluscomm -nostdinc
    -woff 838,649,624,807 -D_LANGUAGE_C -D_MIPS_SZLONG=32 -DF3DEX_GBI_2
    -Iinclude -Isrc -I. -Ibuild -Iassets/us
    -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido)
for file in src/story/sc_ai_bank.c src/story/sc_ai_select.c \
    src/story/sc_ai_decision.c src/story/sc_ai_overlay.c "$out/owned-closure.c"; do
    tools/ido/linux/7.1/cc "${flags[@]}" "$file" -o "$out/$(basename "${file%.c}").o"
    python tools/asm-processor/asm_processor.py -O2 --force --convert-statics local \
        --post-process "$out/$(basename "${file%.c}").o" \
        --assembler "mips-linux-gnu-as -march=vr4300 -32" "$file"
done
tools/ido/linux/7.1/cc "${flags[@]}" src/story/sc_ai_lease.c -o "$out/sc_ai_lease.o"
mips-linux-gnu-as -march=vr4300 -32 -G0 -Iinclude src/story/sc_ai_header.s -o "$out/sc_ai_header.o"
printf '.section .ai_loader_table, "a"\n.word 0\n' > "$out/empty-relocs.s"
mips-linux-gnu-as -32 "$out/empty-relocs.s" -o "$out/relocs.o"
objects=("$out/sc_ai_header.o" "$out/sc_ai_bank.o" "$out/sc_ai_select.o"
    "$out/sc_ai_decision.o" "$out/sc_ai_overlay.o" "$out/owned-closure.o" "$out/relocs.o")
python tools/rltest/audit_story_ai_prototype.py imports "$elf" "$out" "${objects[@]}"
mips-linux-gnu-ld --emit-relocs -T "$out/ai.ld" -T "$out/native-imports.ld" \
    "${objects[@]}" -Map "$out/ai.map" -o "$out/ai.elf"
python tools/rl/gen_rl_relocs.py "$out/ai.elf" "$out/ai-relocs.s" --section .fragmentAI
sed 's/\.rodata/.ai_loader_table/' "$out/ai-relocs.s" > "$out/packed-relocs.s"
mips-linux-gnu-as -32 "$out/packed-relocs.s" -o "$out/relocs.o"
mips-linux-gnu-ld --emit-relocs -T "$out/ai.ld" -T "$out/native-imports.ld" \
    "${objects[@]}" -Map "$out/ai.map" -o "$out/ai.elf"
mips-linux-gnu-readelf -rW "$out/ai.elf" > "$out/ai-relocations.txt"
mips-linux-gnu-objdump -d "$out/ai.elf" > "$out/ai-disassembly.txt"
mips-linux-gnu-nm -S "$out/ai.elf" > "$out/ai-symbols.txt"
mips-linux-gnu-size -A "$out/ai.elf"
python tools/rltest/audit_story_ai_prototype.py image "$elf" "$out"
python tools/rltest/audit_story_ai_prototype.py stack "$elf" "$out"
