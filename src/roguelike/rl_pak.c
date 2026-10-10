/* PURE */
#include "rl_pak.h"

/* Controller Pak font: space=15, A=26. "STADIUM RL", extension "V1". */
const RlU8 gRlPakName[16] = { 44, 45, 26, 29, 34, 46, 38, 15, 43, 37, 0, 0, 0, 0, 0, 0 };
const RlU8 gRlPakExtension[4] = { 47, 17, 0, 0 };

static unsigned be16(const RlU8 *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

static int equal(const RlU8 *a, const RlU8 *b, unsigned n) {
    unsigned i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

RlSaveStatus rl_pak_validate(const RlU8 m[RL_PAK_METADATA_SIZE], RlPakLayout *out) {
    RlPakLayout layout;
    RlU8 used[128];
    const RlU8 *id, *inode, *dir;
    unsigned i, j, sum, inverse, page, count, found, own;
    static const unsigned copies[3] = { 3, 4, 6 };
    if (!m || !out) return RL_SAVE_INVALID;
    id = m + 32;
    sum = inverse = 0;
    for (i = 0; i < 28; i += 2) {
        j = be16(id + i);
        sum += j;
        inverse += (~j) & 65535U;
    }
    if ((sum & 65535U) != be16(id + 28) ||
        (inverse & 65535U) != be16(id + 30)) return RL_SAVE_UNSUPPORTED;
    if (!(be16(id + 24) & 1) || id[26] != 1) return RL_SAVE_UNSUPPORTED;
    for (i = 0; i < 3; i++)
        if (!equal(id, m + copies[i] * 32, 32)) return RL_SAVE_BAD_DATA;
    inode = m + 256;
    if (!equal(inode, m + 512, 256)) return RL_SAVE_BAD_DATA;
    sum = 0;
    for (i = 10; i < 256; i++) sum += inode[i];
    if ((sum & 255) != inode[1]) return RL_SAVE_BAD_DATA;
    for (i = 0; i < 128; i++) used[i] = 0;
    for (i = 0; i < RL_PAK_PAGES; i++) layout.pages[i] = 0;
    layout.free_pages = layout.free_notes = 0;
    layout.note = 16;
    found = 0;
    for (i = 0; i < 16; i++) {
        dir = m + 768 + i * 32;
        own = be16(dir + 4) == RL_PAK_COMPANY_CODE &&
            dir[0] == 'R' && dir[1] == 'L' && dir[2] == '0' && dir[3] == '1' &&
            equal(dir + 12, gRlPakExtension, 4) && equal(dir + 16, gRlPakName, 16);
        if (!be16(dir + 4) && !dir[0] && !dir[1] && !dir[2] && !dir[3]) {
            layout.free_notes++;
            continue;
        }
        if (!be16(dir + 4) || (!dir[0] && !dir[1] && !dir[2] && !dir[3]))
            return RL_SAVE_BAD_DATA;
        if (own && found++) return RL_SAVE_BAD_DATA;
        page = be16(dir + 6);
        count = 0;
        while (page != 1) {
            if (page < 5 || page >= 128 || used[page]) return RL_SAVE_BAD_DATA;
            used[page] = 1;
            if (own && count < RL_PAK_PAGES) layout.pages[count] = (RlU8)page;
            count++;
            page = be16(inode + page * 2);
        }
        if (own) {
            if (count != RL_PAK_PAGES) return RL_SAVE_BAD_DATA;
            layout.note = i;
        }
    }
    for (i = 5; i < 128; i++) {
        page = be16(inode + i * 2);
        if (!used[i]) {
            if (page != 3) return RL_SAVE_BAD_DATA;
            layout.free_pages++;
        }
    }
    *out = layout;
    return found ? RL_SAVE_OK : RL_SAVE_NOT_FOUND;
}
