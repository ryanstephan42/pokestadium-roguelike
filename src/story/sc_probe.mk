SC_PROBE_C := $(wildcard src/story/*.c)
SC_PROBE_O := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SC_PROBE_C))
SC_PROBE_CATALOG := $(BUILD_DIR)/story/sc_data.o
RL_MAIN_O += $(SC_PROBE_O)
O_FILES += $(SC_PROBE_O) $(SC_PROBE_CATALOG)

$(BUILD_DIR)/story/sc_data.c: tools/story_data.py tools/story_graph.py
	$(V)$(PYTHON) tools/story_data.py --c-out $@ --out $(BUILD_DIR)/story/data.json

$(BUILD_DIR)/story/sc_data.o: $(BUILD_DIR)/story/sc_data.c
	$(V)$(CC) -c $(CFLAGS) $(BUILD_DEFINES) $(IINC) $(WARNINGS) $(MIPS_VERSION) $(ENDIAN) $(COMMON_DEFINES) $(RELEASE_DEFINES) $(GBI_DEFINES) $(LIBULTRA_DEFINES) $(C_DEFINES) $(OPTFLAGS) -o $@ $<

$(shell mkdir -p $(BUILD_DIR)/src/story $(BUILD_DIR)/story)
