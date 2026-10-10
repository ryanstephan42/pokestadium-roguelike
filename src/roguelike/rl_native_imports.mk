RL_IMPORT_ASM := $(BUILD_DIR)/src/roguelike/gen_archive_imports.s
RL_IMPORT_OBJ := $(RL_IMPORT_ASM:.s=.o)
O_FILES += $(RL_IMPORT_OBJ)
RL_MAIN_O += $(RL_IMPORT_OBJ)

$(RL_IMPORT_ASM): $(VANILLA_MAP:.map=.elf) tools/rl/gen_archive_imports.py $(RL_DIR)/regen_fragments.txt
	$(V)$(PYTHON) tools/rl/gen_archive_imports.py $< $@ --regen-fragments $(RL_REGEN_FRAGMENTS)

$(RL_IMPORT_OBJ): $(RL_IMPORT_ASM)
	$(V)$(AS) $(ASFLAGS) $(ENDIAN) -o $@ $<
