SC_PROBE_C := $(wildcard src/story/*.c)
SC_PROBE_O := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SC_PROBE_C))
SC_PROBE_CATALOG := $(BUILD_DIR)/story/sc_data.o
SC_COLD_NAMES := sc_core sc_graph sc_bridge sc_save sc_storage sc_pak_probe sc_cold sc_native_cold sc_make
SC_COLD_O := $(addprefix $(BUILD_DIR)/src/story/,$(addsuffix .o,$(SC_COLD_NAMES))) $(SC_PROBE_CATALOG)
ifeq ($(STORY_EMBER_PROBE),1)
SC_EMBER_O := $(BUILD_DIR)/tools/rltest/story_ember_fixture.o
SC_COLD_O += $(SC_EMBER_O)
O_FILES += $(SC_EMBER_O)
$(SC_EMBER_O): tools/rltest/story_ember_fixture.h $(wildcard src/story/*.h)
$(shell mkdir -p $(BUILD_DIR)/tools/rltest)
endif
SC_HEADER_O := $(BUILD_DIR)/src/story/sc_cold_header.o
SC_RELOCS_O := $(RL_DIR)/fragmentSC_relocs.o
SC_RELOCS0_O := $(RL_DIR)/fragmentSC_relocs_pass1.o
SC_POOL_TRACE_O := $(BUILD_DIR)/src/story/sc_pool_trace.o
RL_MAIN_O += $(SC_POOL_TRACE_O)
O_FILES += $(SC_POOL_TRACE_O)
$(BUILD_DIR)/src/memory_main.o: src/story/sc_runtime_probe.h
RL_MAIN_O += $(filter-out $(SC_COLD_O),$(SC_PROBE_O))
O_FILES += $(SC_PROBE_O) $(SC_PROBE_CATALOG) $(SC_HEADER_O)

$(SC_PROBE_O): $(wildcard src/story/*.h)
$(BUILD_DIR)/src/fragments/62/fragment62_2EA8E0.o: src/story/sc_runtime_probe.h
$(BUILD_DIR)/src/fragments/62/fragment62_315D50.o: src/story/sc_runtime_probe.h

$(SC_RELOCS0_O:.o=.s): tools/rl/gen_rl_relocs.py
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py - $@ --section .fragmentSC

$(SC_RELOCS_O:.o=.s): $(RL_ELF_PASS1) tools/rl/gen_rl_relocs.py
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py $< $@ --section .fragmentSC

$(BUILD_DIR)/story/sc_data.c: tools/story_data.py tools/story_graph.py
	$(V)$(PYTHON) tools/story_data.py --c-out $@ --out $(BUILD_DIR)/story/data.json

$(BUILD_DIR)/story/sc_data.o: $(BUILD_DIR)/story/sc_data.c
	$(V)$(CC) -c $(CFLAGS) $(BUILD_DEFINES) $(IINC) $(WARNINGS) $(MIPS_VERSION) $(ENDIAN) $(COMMON_DEFINES) $(RELEASE_DEFINES) $(GBI_DEFINES) $(LIBULTRA_DEFINES) $(C_DEFINES) $(OPTFLAGS) -o $@ $<

$(shell mkdir -p $(BUILD_DIR)/src/story $(BUILD_DIR)/story)
