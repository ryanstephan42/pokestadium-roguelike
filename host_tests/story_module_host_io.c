#include <stdio.h>
#include <sys/mman.h>

int sc_host_read_image(const char *path, unsigned char *destination, unsigned bytes) {
    FILE *file = fopen(path, "rb");
    int ok;
    if (!file) {
        perror(path);
        return 0;
    }

    ok = fread(destination, 1, bytes, file) == bytes && fgetc(file) == EOF;
    if (!ok) fprintf(stderr, "Invalid image length/read: %s\n", path);
    if (fclose(file) != 0) {
        perror(path);
        return 0;
    }
    return ok;
}

void *sc_host_fixed_backing(unsigned address, unsigned bytes) {
    void *p = mmap((void *)address, bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) {
        perror("isolated fixed HOST backing");
        return 0;
    }
    return p;
}

int sc_host_unmap_backing(void *p, unsigned bytes) {
    if (munmap(p, bytes) == 0) return 1;
    perror("isolated HOST backing release");
    return 0;
}
