#ifndef STORY_IDO_STDINT_H
#define STORY_IDO_STDINT_H
/* Isolated o32 side build only; never added to production include paths. */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef signed int int32_t;
#ifndef _COMMON_H
typedef unsigned int uintptr_t;
#endif
#define UINT32_C(value) value##U
#define UINT32_MAX 0xffffffffU
#define UINTPTR_MAX 0xffffffffU
#endif
