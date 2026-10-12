SC_PROBE_C := $(filter-out src/story/sc_ai_%,$(wildcard src/story/*.c))
ifeq ($(STORY_AI_PROTOTYPE),1)
SC_PROBE_C += src/story/sc_ai_lease.c src/story/sc_ai_runtime.c
include src/story/sc_ai.mk
endif
ifeq ($(STORY_NATIVE_UI),1)
SC_DP_LIBRARY_OBJS := $(BUILD_DIR)/lib/libultra.a:getthreadid.o $(BUILD_DIR)/lib/libultra.a:vigetnextframebuf.o
endif
ifeq ($(STORY_SERVICE_CANDIDATE),1)
ifeq ($(strip $(SC_SERVICE_AUTH_DIR)),)
$(error STORY_SERVICE_CANDIDATE requires explicit SC_SERVICE_AUTH_DIR)
endif
ifeq ($(strip $(SC_SERVICE_SPAN_DIR)),)
$(error STORY_SERVICE_CANDIDATE requires explicit SC_SERVICE_SPAN_DIR)
endif
IINC := -Ihost_tests/story_ido_include -I$(SC_SERVICE_AUTH_DIR) -I$(SC_SERVICE_SPAN_DIR) $(IINC)
include src/story/sc_ai.mk
SC_SERVICE_NATIVE_C := host_tests/story_private_rom_loader.c host_tests/story_real_ai_image.c host_tests/story_private_rom_native.c
SC_SERVICE_NATIVE_O := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SC_SERVICE_NATIVE_C))
O_FILES += $(SC_SERVICE_NATIVE_O)
RL_MAIN_O += $(SC_SERVICE_NATIVE_O)
$(SC_SERVICE_NATIVE_O): $(wildcard host_tests/story_ido_include/*.h) $(wildcard host_tests/story_private_rom*.h) host_tests/story_real_ai_image.h $(wildcard $(SC_SERVICE_AUTH_DIR)/*.h) src/story/sc_probe.mk
$(BUILD_DIR)/src/story/sc_battle_service.o: $(wildcard $(SC_SERVICE_AUTH_DIR)/*.h) $(wildcard $(SC_SERVICE_SPAN_DIR)/*.h)
$(shell mkdir -p $(BUILD_DIR)/host_tests)
endif
SC_PROBE_O := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SC_PROBE_C))
SC_PROBE_CATALOG := $(BUILD_DIR)/story/sc_data.o
SC_COLD_NAMES := sc_core sc_graph sc_bridge sc_save sc_storage sc_pak_probe sc_cold sc_native_cold sc_make
SC_COLD_NAMES += sc_ui sc_scene sc_ui_driver
SC_COLD_NAMES += sc_native_ui sc_native_loop sc_native_display
SC_COLD_O := $(addprefix $(BUILD_DIR)/src/story/,$(addsuffix .o,$(SC_COLD_NAMES))) $(SC_PROBE_CATALOG)
ifeq ($(STORY_EMBER_PROBE),1)
SC_EMBER_O := $(BUILD_DIR)/tools/rltest/story_ember_fixture.o
SC_COLD_O += $(SC_EMBER_O)
O_FILES += $(SC_EMBER_O)
$(SC_EMBER_O): tools/rltest/story_ember_fixture.h $(wildcard src/story/*.h)
$(shell mkdir -p $(BUILD_DIR)/tools/rltest)
endif
ifeq ($(STORY_SIX_PROBE),1)
SC_SIX_O := $(BUILD_DIR)/tools/rltest/story_six_fixture.o
SC_COLD_O += $(SC_SIX_O)
O_FILES += $(SC_SIX_O)
$(SC_SIX_O): tools/rltest/story_six_fixture.h $(wildcard src/story/*.h)
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

$(SC_PROBE_O): $(wildcard src/story/*.h) src/story/sc_probe.mk
$(BUILD_DIR)/src/fragments/62/fragment62_2EA8E0.o $(BUILD_DIR)/src/fragments/62/fragment62_2F74E0.o: $(wildcard src/story/*.h)
$(BUILD_DIR)/src/fragments/62/fragment62_315D50.o: src/story/sc_runtime_probe.h

$(SC_RELOCS0_O:.o=.s): tools/rl/gen_rl_relocs.py
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py - $@ --section .fragmentSC

$(SC_RELOCS_O:.o=.s): $(RL_ELF_PASS1) tools/rl/gen_rl_relocs.py
	$(V)$(PYTHON) tools/rl/gen_rl_relocs.py $< $@ --section .fragmentSC

.PHONY: sc-catalog-mode-force
$(BUILD_DIR)/story/catalog-mode: sc-catalog-mode-force
	$(V)printf '%s\n' '$(STORY_MOON_STONES)' | cmp -s - $@ || printf '%s\n' '$(STORY_MOON_STONES)' > $@

$(BUILD_DIR)/story/sc_data.c: tools/story_data.py tools/story_graph.py tools/story_stones.py $(BUILD_DIR)/story/catalog-mode
	$(V)$(PYTHON) tools/story_data.py $(if $(filter 1,$(STORY_MOON_STONES)),--moon-stones) --c-out $@ --out $(BUILD_DIR)/story/data.json

$(BUILD_DIR)/story/sc_data.o: $(BUILD_DIR)/story/sc_data.c src/story/sc_data.h src/story/sc_graph.h src/story/sc_core.h src/story/sc_save.h
	$(V)$(CC) -c $(CFLAGS) $(BUILD_DEFINES) $(IINC) $(WARNINGS) $(MIPS_VERSION) $(ENDIAN) $(COMMON_DEFINES) $(RELEASE_DEFINES) $(GBI_DEFINES) $(LIBULTRA_DEFINES) $(C_DEFINES) $(OPTFLAGS) -o $@ $<

$(shell mkdir -p $(BUILD_DIR)/src/story $(BUILD_DIR)/story)
