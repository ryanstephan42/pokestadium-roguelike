/* PURE */
#include "sc_save.h"

/* Resident Pak identity shared by the native backend and cold codec. */
const RlU8 sc_save_name[16] = {44,45,26,29,34,46,38,15,44,45,40,43,50,0,0,0};
const RlU8 sc_save_extension[4] = {47,17,0,0};

RlU32 sc_save_crc(const RlU8 *data, unsigned size) {
    RlU32 crc = 0xffffffffU;
    unsigned i, j;
    for (i = 0; i < size; i++) {
        crc ^= (i >= 8 && i < 12) ? 0 : data[i];
        for (j = 0; j < 8; j++) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    return ~crc;
}
