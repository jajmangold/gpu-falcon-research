#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "nvdzip.h"

static NVD_CRYPT_KEY key = {
    0x4f, 0x77, 0x44, 0x65,
    0x75, 0x67, 0x44, 0x75,
    0x70, 0x20, 0x21, 0x19,
    0x4e, 0x56, 0x49, 0x44,
};

static int write_file(const char *dir, const char *name, const void *data, unsigned size) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return 1;
    }
    if (fwrite(data, 1, size, f) != size) {
        perror("fwrite");
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s dump.zip output-dir\n", argv[0]);
        return 2;
    }

    const char *zip = argv[1];
    const char *out = argv[2];
    mkdir(out, 0775);

    NVD_ZIP_HANDLE h = 0;
    NVD_STATUS st = nvdZip_Open(zip, "N/A", &key, NVD_ZIP_MODE_READ, &h);
    if (st != NVD_OK) {
        fprintf(stderr, "nvdZip_Open failed: %u\n", st);
        return 1;
    }

    char **files = NULL;
    NvU32 count = 0;
    st = nvdZip_ListFiles(h, &files, &count);
    if (st != NVD_OK) {
        fprintf(stderr, "nvdZip_ListFiles failed: %u\n", st);
        return 1;
    }

    for (NvU32 i = 0; i < count; i++) {
        void *data = NULL;
        NvU32 size = 0;
        st = nvdZip_ExtractFile(h, files[i], &data, &size);
        if (st != NVD_OK) {
            fprintf(stderr, "extract %s failed: %u\n", files[i], st);
            continue;
        }
        printf("%s %u\n", files[i], size);
        write_file(out, files[i], data, size);
        free(data);
    }

    nvdZip_ReleaseFileList(files, count);
    nvdZip_Close(h);
    return 0;
}
