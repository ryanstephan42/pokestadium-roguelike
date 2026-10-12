SC_AI_DIR := $(BUILD_DIR)/story/ai
SC_AI_HEADER := $(BUILD_DIR)/src/story/sc_ai_header.o
SC_AI_OBJECTS := $(addprefix $(BUILD_DIR)/src/story/,$(addsuffix .o,sc_ai_bank sc_ai_select sc_ai_decision sc_ai_overlay)) $(SC_AI_DIR)/owned-closure.o
SC_AI_RELOCS := $(RL_DIR)/fragmentAI_relocs.o
SC_AI_RELOCS0 := $(RL_DIR)/fragmentAI_relocs_pass1.o
ifeq ($(STORY_SERVICE_CANDIDATE),1)
ifeq ($(strip $(SC_AI_BASE)),)
$(error STORY_SERVICE_CANDIDATE requires explicit SC_AI_BASE ELF)
endif
else
SC_AI_BASE ?= .local/rltest/story-six-natural-source-review-c399bd7/probe.elf
endif
O_FILES += $(SC_AI_HEADER) $(SC_AI_OBJECTS)
$(shell mkdir -p $(SC_AI_DIR))

$(SC_AI_DIR)/owned-closure.c: tools/rltest/build_story_ai_prototype.py $(SC_AI_BASE) src/fragments/62/fragment62_361050.c src/fragments/62/fragment62_359F90.c
	$(V)$(PYTHON) tools/rltest/build_story_ai_prototype.py --elf $(SC_AI_BASE) --out $(SC_AI_DIR)

$(SC_AI_DIR)/owned-closure.o: $(SC_AI_DIR)/owned-closure.c
	$(V)$(CC) -c $(CFLAGS) $(BUILD_DEFINES) $(IINC) $(WARNINGS) $(MIPS_VERSION) $(ENDIAN) $(COMMON_DEFINES) $(RELEASE_DEFINES) $(GBI_DEFINES) $(LIBULTRA_DEFINES) $(C_DEFINES) $(OPTFLAGS) -o $@ $<

$(SC_AI_RELOCS0:.o=.s): tools/rl/gen_rl_relocs.py
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py - $@ --section .fragmentAI

$(SC_AI_RELOCS:.o=.s): $(RL_ELF_PASS1) tools/rl/gen_rl_relocs.py
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py $< $@ --section .fragmentAI
