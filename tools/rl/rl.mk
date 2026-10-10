# Roguelike link rules. Included by the top-level Makefile only when ROGUELIKE=1.
# See docs/roguelike/BUILD.md.

RL_DIR          := $(BUILD_DIR)/rl
RL_SYM_SCRIPTS  := linker_scripts/$(VERSION)/hardware_regs.ld linker_scripts/$(VERSION)/undefined_syms.ld \
                   linker_scripts/$(VERSION)/unused_syms.ld linker_scripts/common_undef_syms.ld \
                   linker_scripts/$(VERSION)/auto/undefined_syms_auto.ld linker_scripts/$(VERSION)/auto/undefined_funcs_auto.ld
RL_SYM_PP       := $(foreach f,$(RL_SYM_SCRIPTS),$(BUILD_DIR)/$f)
RL_SYM_OUT      := $(foreach f,$(RL_SYM_SCRIPTS),$(RL_DIR)/$(notdir $f))

RL_ENTRY_O      := $(BUILD_DIR)/$(RL_MAIN_DIR)/rl_entry.o
RL_HEADER_O     := $(BUILD_DIR)/$(RL_FRAG_DIR)/rl_header.o
RL_MAIN_O       := $(filter-out $(RL_ENTRY_O),$(filter $(BUILD_DIR)/$(RL_MAIN_DIR)/%,$(O_FILES)))
RL_FRAG_O       := $(filter-out $(RL_HEADER_O),$(filter $(BUILD_DIR)/$(RL_FRAG_DIR)/%,$(O_FILES)))
RL_RELOCS_O     := $(RL_DIR)/fragmentRL_relocs.o
RL_RELOCS0_O    := $(RL_DIR)/fragmentRL_relocs_pass1.o
RL_ELF_PASS1    := $(RL_DIR)/pass1.elf

# Optional per-workstream make fragments (e.g. generated tables) live next to the sources.
-include $(wildcard $(RL_MAIN_DIR)/*.mk $(RL_FRAG_DIR)/*.mk)

$(shell mkdir -p $(RL_DIR))

RL_LD_ARGS = $(foreach f,$(RL_SYM_OUT),-T $f)

$(VANILLA_MAP):
	$(error $(VANILLA_MAP) missing: build the vanilla ROM first (`make rl` does this))

$(LDSCRIPT) $(RL_SYM_OUT) &: linker_scripts/$(VERSION)/$(TARGET).ld $(VANILLA_MAP) $(RL_SYM_PP) tools/rl/gen_rl_ld.py $(MAKEFILE_LIST)
	$(call print,Generating RL linker script:,$<,$(LDSCRIPT))
	$(V)$(PYTHON) tools/rl/gen_rl_ld.py --vanilla-ld $< --vanilla-map $(VANILLA_MAP) --build-dir $(BUILD_DIR) \
		--out-ld $(LDSCRIPT) --out-sym-dir $(RL_DIR) --out-anchors $(RL_DIR)/anchors.txt \
		--frag-header $(RL_HEADER_O) --frag-relocs $(RL_RELOCS_O) \
		--main-objs $(RL_MAIN_O) --frag-objs $(RL_FRAG_O) --sym-files $(RL_SYM_PP)

$(RL_DIR)/%.o: $(RL_DIR)/%.s
	$(V)$(AS) $(ASFLAGS) $(ENDIAN) -o $@ $<

$(RL_DIR)/fragmentRL_relocs_pass1.s: tools/rl/gen_rl_relocs.py
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py - $@

RL_LINK = $(LD) $(LDFLAGS) -T $(1) $(RL_LD_ARGS) -Map $(2) $(LIBULTRA_LIB) -o $(3)

# Pass 1: link with an empty reloc table to learn the fragment's relocations.
$(RL_ELF_PASS1): $(O_FILES) $(LIBULTRA_LIB) $(LDSCRIPT) $(RL_SYM_OUT) $(RL_RELOCS0_O)
	@$(PRINT) "$(GREEN)Linking RL pass 1:  $(BLUE)$@ $(NO_COL)\n"
	$(V)sed 's|$(RL_RELOCS_O)|$(RL_RELOCS0_O)|' $(LDSCRIPT) > $(RL_DIR)/pass1.ld
	$(V)$(call RL_LINK,$(RL_DIR)/pass1.ld,$(RL_DIR)/pass1.map,$@)

$(RL_DIR)/fragmentRL_relocs.s: $(RL_ELF_PASS1) tools/rl/gen_rl_relocs.py
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py $< $@

# Pass 2: final link with the real reloc table, then check the table is stable.
$(ELF): $(RL_ELF_PASS1) $(RL_RELOCS_O)
	@$(PRINT) "$(GREEN)Linking RL ELF file:  $(BLUE)$@ $(NO_COL)\n"
	$(V)$(call RL_LINK,$(LDSCRIPT),$(MAP),$@)
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py $@ $(RL_DIR)/fragmentRL_relocs_check.s >/dev/null
	$(V)cmp -s $(RL_DIR)/fragmentRL_relocs.s $(RL_DIR)/fragmentRL_relocs_check.s || (echo "rl: fragment relocs changed between passes" && false)
	$(V)$(PYTHON) tools/rl/check_rl_anchors.py $(RL_DIR)/anchors.txt $(MAP)

# Extracted asm that needs position-independence fixups (see tools/rl/asm_fixups.py).
RL_ASM_FIXUP_FILES := asm/$(VERSION)/49190.s
RL_ASM_FIXUP_OBJS  := $(foreach f,$(RL_ASM_FIXUP_FILES),$(BUILD_DIR)/$(f:.s=.o))

$(RL_ASM_FIXUP_OBJS): $(BUILD_DIR)/%.o: %.s tools/rl/asm_fixups.py
	$(call print,Assembling (rl fixups):,$<,$@)
	$(V)$(PYTHON) tools/rl/asm_fixups.py $< | $(ICONV) $(ICONV_FLAGS) | $(AS) $(ASFLAGS) $(ENDIAN) $(IINC) -I $(dir $*) -o $@
