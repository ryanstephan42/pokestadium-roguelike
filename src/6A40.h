#ifndef _6A40_H_
#define _6A40_H_

#include "global.h"

void func_80005E40(u32 arg0, s32 arg1);
#ifdef STORY_NATIVE_UI
s32 func_80005E40Checked(u32 bytes);
s32 func_80005E40ActivateChecked(void);
s32 func_80005E40SuspendChecked(void);
#endif
void func_80005EAC(void);
void func_80005EDC(void);
void func_80005F1C(Gfx** arg0, Gfx** arg1);
void* func_80005F5C(s32 arg0);
s32 func_80005F9C(void);

#endif // _6A40_H_
