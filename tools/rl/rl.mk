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
RL_ROSTERS_O    := $(BUILD_DIR)/$(RL_MAIN_DIR)/rl_rosters.o
RL_MAIN_O       := $(filter-out $(RL_ENTRY_O) $(RL_ROSTERS_O),$(filter $(BUILD_DIR)/$(RL_MAIN_DIR)/%,$(O_FILES)))
RL_FRAG_O       := $(filter-out $(RL_HEADER_O),$(filter $(BUILD_DIR)/$(RL_FRAG_DIR)/%,$(O_FILES))) $(RL_ROSTERS_O)
RL_RELOCS_O     := $(RL_DIR)/fragmentRL_relocs.o
RL_RELOCS0_O    := $(RL_DIR)/fragmentRL_relocs_pass1.o
RL_ELF_PASS1    := $(RL_DIR)/pass1.elf
# fragment31 holds ROGUELIKE-guarded edits (shadow/CI/callback fixes) that move its functions.
RL_REGEN_FRAGMENTS ?= 62 31
RL_NATIVE_HEADERS := $(foreach n,$(RL_REGEN_FRAGMENTS),$(RL_DIR)/fragment$(n)_header.o)
RL_NATIVE_RELOCS := $(foreach n,$(RL_REGEN_FRAGMENTS),$(RL_DIR)/fragment$(n)_relocs.o)
RL_NATIVE_RELOCS0 := $(foreach n,$(RL_REGEN_FRAGMENTS),$(RL_DIR)/fragment$(n)_relocs_pass1.o)

# Optional per-workstream make fragments (e.g. generated tables) live next to the sources.
-include $(wildcard $(RL_MAIN_DIR)/*.mk $(RL_FRAG_DIR)/*.mk)
ifeq ($(STORY_PROBE),1)
include src/story/sc_probe.mk
endif

$(shell mkdir -p $(RL_DIR))
RL_NATIVE_CONFIG := $(RL_DIR)/regen_fragments.txt
$(shell if ! test -f $(RL_NATIVE_CONFIG) || test "$$(cat $(RL_NATIVE_CONFIG))" != "$(RL_REGEN_FRAGMENTS)"; then printf '%s\n' "$(RL_REGEN_FRAGMENTS)" > $(RL_NATIVE_CONFIG); fi)

RL_LD_ARGS = $(foreach f,$(RL_SYM_OUT),-T $f)

$(VANILLA_MAP):
	$(error $(VANILLA_MAP) missing: build the vanilla ROM first (`make rl` does this))

$(LDSCRIPT) $(RL_SYM_OUT) &: linker_scripts/$(VERSION)/$(TARGET).ld $(VANILLA_MAP) $(RL_SYM_PP) tools/rl/gen_rl_ld.py $(MAKEFILE_LIST) $(RL_MAIN_O) $(RL_FRAG_O) $(SC_COLD_O) $(SC_AI_OBJECTS) $(RL_NATIVE_CONFIG)
	$(call print,Generating RL linker script:,$<,$(LDSCRIPT))
	$(V)$(PYTHON) tools/rl/gen_rl_ld.py --vanilla-ld $< --vanilla-map $(VANILLA_MAP) --build-dir $(BUILD_DIR) \
		--out-ld $(LDSCRIPT) --out-sym-dir $(RL_DIR) --out-anchors $(RL_DIR)/anchors.txt \
		--frag-header $(RL_HEADER_O) --frag-relocs $(RL_RELOCS_O) \
		--main-objs $(RL_MAIN_O) $(SC_DP_LIBRARY_OBJS) --frag-objs $(RL_FRAG_O) --sym-files $(RL_SYM_PP) \
		$(if $(SC_PROBE_CATALOG),--story-catalog $(SC_PROBE_CATALOG) --story-header $(SC_HEADER_O) --story-relocs $(SC_RELOCS_O) --story-objs $(SC_COLD_O)) \
		$(if $(SC_AI_HEADER),--ai-header $(SC_AI_HEADER) --ai-relocs $(SC_AI_RELOCS) --ai-objs $(SC_AI_OBJECTS)) \
		--regen-fragments $(RL_REGEN_FRAGMENTS)

$(RL_DIR)/%.o: $(RL_DIR)/%.s
	$(V)$(AS) $(ASFLAGS) $(ENDIAN) -o $@ $<

$(RL_DIR)/fragmentRL_relocs_pass1.s: tools/rl/gen_rl_relocs.py
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py - $@

define RL_NATIVE_RULES
$(RL_DIR)/fragment$(1)_header.s: assets/$(VERSION)/fragments/$(1)/fragment$(1)_header.textbin.bin tools/rl/gen_native_header.py $(VANILLA_MAP)
	$$(V)$$(PYTHON) tools/rl/gen_native_header.py $(1) $$< $$@ --vanilla-map $$(VANILLA_MAP)
$(RL_DIR)/fragment$(1)_relocs_pass1.s: tools/rl/gen_rl_relocs.py
	$$(V)$$(PYTHON) tools/rl/gen_rl_relocs.py - $$@ --section .fragment$(1)
$(RL_DIR)/fragment$(1)_relocs.s: $(RL_ELF_PASS1) tools/rl/gen_rl_relocs.py $(VANILLA_MAP) assets/$(VERSION)/fragments/$(1)/fragment$(1)_reloc.rodatabin.bin
	$$(V)$$(PYTHON) tools/rl/gen_rl_relocs.py $$< $$@ --section .fragment$(1) --original assets/$$(VERSION)/fragments/$(1)/fragment$(1)_reloc.rodatabin.bin --anchors $$(RL_DIR)/anchors.txt --map $$(RL_DIR)/pass1.map --vanilla-map $$(VANILLA_MAP)
endef
$(foreach n,$(RL_REGEN_FRAGMENTS),$(eval $(call RL_NATIVE_RULES,$(n))))

RL_LINK = $(LD) $(LDFLAGS) -T $(1) $(RL_LD_ARGS) -Map $(2) $(LIBULTRA_LIB) -o $(3)

# Pass 1: link with an empty reloc table to learn the fragment's relocations.
$(RL_ELF_PASS1): $(O_FILES) $(LIBULTRA_LIB) $(LDSCRIPT) $(RL_SYM_OUT) $(RL_RELOCS0_O) $(SC_RELOCS0_O) $(SC_AI_RELOCS0) $(RL_NATIVE_HEADERS) $(RL_NATIVE_RELOCS0)
	@$(PRINT) "$(GREEN)Linking RL pass 1:  $(BLUE)$@ $(NO_COL)\n"
	$(V)sed -e 's|$(RL_RELOCS_O)|$(RL_RELOCS0_O)|' $(if $(SC_RELOCS_O),-e 's|$(SC_RELOCS_O)|$(SC_RELOCS0_O)|') $(if $(SC_AI_RELOCS),-e 's|$(SC_AI_RELOCS)|$(SC_AI_RELOCS0)|') $(foreach n,$(RL_REGEN_FRAGMENTS),-e 's|$(RL_DIR)/fragment$(n)_relocs.o|$(RL_DIR)/fragment$(n)_relocs_pass1.o|') $(LDSCRIPT) > $(RL_DIR)/pass1.ld
	$(V)$(call RL_LINK,$(RL_DIR)/pass1.ld,$(RL_DIR)/pass1.map,$@)

$(RL_DIR)/fragmentRL_relocs.s: $(RL_ELF_PASS1) tools/rl/gen_rl_relocs.py
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py $< $@

# Pass 2: final link with the real reloc table, then check the table is stable.
$(ELF): $(RL_ELF_PASS1) $(RL_RELOCS_O) $(SC_RELOCS_O) $(SC_AI_RELOCS) $(RL_NATIVE_RELOCS) tools/rl/check_rl_anchors.py tools/rl/check_native_thunks.py $(if $(SC_RELOCS_O),tools/rl/check_story_residency.py tools/rl/check_story_frames.py)
	@$(PRINT) "$(GREEN)Linking RL ELF file:  $(BLUE)$@ $(NO_COL)\n"
	$(V)$(call RL_LINK,$(LDSCRIPT),$(MAP),$@)
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py $@ $(RL_DIR)/fragmentRL_relocs_check.s >/dev/null
	$(V)cmp -s $(RL_DIR)/fragmentRL_relocs.s $(RL_DIR)/fragmentRL_relocs_check.s || (echo "rl: fragment relocs changed between passes" && false)
	$(if $(SC_RELOCS_O),$(V)$(PYTHON) tools/rl/gen_rl_relocs.py $@ $(RL_DIR)/fragmentSC_relocs_check.s --section .fragmentSC && cmp $(RL_DIR)/fragmentSC_relocs.s $(RL_DIR)/fragmentSC_relocs_check.s)
	$(if $(SC_AI_RELOCS),$(V)$(PYTHON) tools/rl/gen_rl_relocs.py $@ $(RL_DIR)/fragmentAI_relocs_check.s --section .fragmentAI && cmp $(RL_DIR)/fragmentAI_relocs.s $(RL_DIR)/fragmentAI_relocs_check.s)
	$(V)$(foreach n,$(RL_REGEN_FRAGMENTS),$(PYTHON) tools/rl/gen_rl_relocs.py $@ $(RL_DIR)/fragment$(n)_relocs_check.s --section .fragment$(n) --original assets/$(VERSION)/fragments/$(n)/fragment$(n)_reloc.rodatabin.bin --anchors $(RL_DIR)/anchors.txt --map $(MAP) --patch-data --vanilla-map $(VANILLA_MAP) && cmp $(RL_DIR)/fragment$(n)_relocs.s $(RL_DIR)/fragment$(n)_relocs_check.s && ) true
	$(V)$(foreach n,$(RL_REGEN_FRAGMENTS),$(PYTHON) tools/rl/check_native_thunks.py $@ $(MAP) $(VANILLA_MAP:.map=.elf) $(VANILLA_MAP) $(n) assets/$(VERSION)/fragments/$(n)/fragment$(n)_reloc.rodatabin.bin && ) true
	$(V)$(PYTHON) tools/rl/check_rl_anchors.py $(RL_DIR)/anchors.txt $(MAP)
	$(if $(SC_RELOCS_O),$(V)$(PYTHON) tools/rl/check_story_residency.py $@)
	$(if $(SC_RELOCS_O),$(V)$(PYTHON) tools/rl/check_story_frames.py $@)

# Extracted asm that needs position-independence fixups (see tools/rl/asm_fixups.py).
RL_ASM_FIXUP_FILES := asm/$(VERSION)/49190.s
RL_ASM_FIXUP_OBJS  := $(foreach f,$(RL_ASM_FIXUP_FILES),$(BUILD_DIR)/$(f:.s=.o))

$(RL_ASM_FIXUP_OBJS): $(BUILD_DIR)/%.o: %.s tools/rl/asm_fixups.py
	$(call print,Assembling (rl fixups):,$<,$@)
	$(V)$(PYTHON) tools/rl/asm_fixups.py $< | $(ICONV) $(ICONV_FLAGS) | $(AS) $(ASFLAGS) $(ENDIAN) $(IINC) -I $(dir $*) -o $@
