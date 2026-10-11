/* PURE */
#include "sc_save.h"

RlU32 sc_save_crc(const RlU8 *data, unsigned size) {
    RlU32 crc = 0xffffffffU;
    unsigned i, j;
    for (i = 0; i < size; i++) {
        crc ^= (i >= 8 && i < 12) ? 0 : data[i];
        for (j = 0; j < 8; j++) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    return ~crc;
}
