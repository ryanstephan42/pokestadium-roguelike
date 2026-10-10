# These stock libultra objects are discarded by the vanilla linker script.
# Extract distinct objects so only the RL linker keeps the required APIs.
RL_SAVE_PFS := pfsallocatefile pfssearchfile pfsfreeblocks
RL_SAVE_PFS_O := $(foreach f,$(RL_SAVE_PFS),$(RL_DIR)/save_$(f).o)
RL_MAIN_O += $(RL_SAVE_PFS_O)

$(RL_SAVE_PFS_O): $(RL_DIR)/save_%.o: $(LIBULTRA_LIB) src/roguelike/rl_save.mk
	$(V)$(AR) p $< $*.o > $@
	$(V)$(OBJCOPY) --redefine-sym osPfsAllocateFile=rlPfsAllocateFile \
		--redefine-sym __osPfsDeclearPage=rlPfsDeclearPage \
		--redefine-sym osPfsFindFile=rlPfsFindFile \
		--redefine-sym osPfsFreeBlocks=rlPfsFreeBlocks $@
