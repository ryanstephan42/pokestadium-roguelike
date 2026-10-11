#include "src/story/sc_native_ui.h"
#include "src/story/sc_native_loop.h"

unsigned sc_native_ui_bytes(void) { return sizeof(ScNativeUi); }
unsigned sc_native_pak_bytes(void) { return sizeof(ScNativePak); }
unsigned sc_ui_driver_bytes(void) { return sizeof(ScUiDriver); }
unsigned sc_ui_bytes(void) { return sizeof(ScUi); }
unsigned sc_request_bytes(void) { return sizeof(ScUiBattleRequest); }
unsigned sc_loop_bytes(void) { return sizeof(ScNativeLoop); }
unsigned sc_display_bytes(void) { return sizeof(ScNativeDisplay); }
unsigned sc_event_bytes(void) { return sizeof(ScNativeCampaignEvent); }
