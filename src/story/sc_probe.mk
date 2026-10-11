SC_PROBE_C := $(wildcard src/story/*.c)
SC_PROBE_O := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SC_PROBE_C))
SC_PROBE_CATALOG := $(BUILD_DIR)/story/sc_data.o
SC_COLD_NAMES := sc_core sc_graph sc_bridge sc_save sc_storage sc_pak_probe sc_cold sc_native_cold sc_make
SC_COLD_O := $(addprefix $(BUILD_DIR)/src/story/,$(addsuffix .o,$(SC_COLD_NAMES))) $(SC_PROBE_CATALOG)
SC_HEADER_O := $(BUILD_DIR)/src/story/sc_cold_header.o
SC_RELOCS_O := $(RL_DIR)/fragmentSC_relocs.o
SC_RELOCS0_O := $(RL_DIR)/fragmentSC_relocs_pass1.o
RL_MAIN_O += $(filter-out $(SC_COLD_O),$(SC_PROBE_O))
O_FILES += $(SC_PROBE_O) $(SC_PROBE_CATALOG) $(SC_HEADER_O)

$(SC_RELOCS0_O:.o=.s): tools/rl/gen_rl_relocs.py
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py - $@ --section .fragmentSC

$(SC_RELOCS_O:.o=.s): $(RL_ELF_PASS1) tools/rl/gen_rl_relocs.py
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py $< $@ --section .fragmentSC

$(BUILD_DIR)/story/sc_data.c: tools/story_data.py tools/story_graph.py
	$(V)$(PYTHON) tools/story_data.py --c-out $@ --out $(BUILD_DIR)/story/data.json

$(BUILD_DIR)/story/sc_data.o: $(BUILD_DIR)/story/sc_data.c
	$(V)$(CC) -c $(CFLAGS) $(BUILD_DEFINES) $(IINC) $(WARNINGS) $(MIPS_VERSION) $(ENDIAN) $(COMMON_DEFINES) $(RELEASE_DEFINES) $(GBI_DEFINES) $(LIBULTRA_DEFINES) $(C_DEFINES) $(OPTFLAGS) -o $@ $<

$(shell mkdir -p $(BUILD_DIR)/src/story $(BUILD_DIR)/story)
