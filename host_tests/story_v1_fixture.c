#include <stdio.h>
#include "src/story/sc_save.h"

int main(void) {
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 1};
    ScSnapshot snapshot;
    RlU8 bytes[SC_SAVE_SIZE];
    if (sc_new(&snapshot.state, &sc_catalog, &options, 123) != SC_OK) {
        fputs("Cannot construct original v1 state\n", stderr);
        return 1;
    }
    snapshot.interrupted = 0;
    if (sc_save_encode(&snapshot, &sc_catalog, 7, bytes) != SC_SAVE_OK) {
        fputs("Cannot encode original v1 fixture\n", stderr);
        return 1;
    }
    if (fwrite(bytes, 1, sizeof(bytes), stdout) != sizeof(bytes)) {
        perror("Writing original v1 fixture");
        return 1;
    }
    return 0;
}
