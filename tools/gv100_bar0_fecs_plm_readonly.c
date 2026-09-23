/*
 * Read-only GV100 BAR0 probe for FECS feature-override and Falcon PLM state.
 *
 * This intentionally opens sysfs resource0 read-only and maps PROT_READ only.
 * It does not write registers.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char *name;
    uint32_t offset;
} reg_desc_t;

static const reg_desc_t regs[] = {
    {"NV_PMC_BOOT_0", 0x00000000u},
    {"NV_PSTRAP_SYS_CTRL", 0x00101040u},
    {"FECS_FALCON_IRQSTAT", 0x00409008u},
    {"FECS_FALCON_IRQMODE", 0x0040900cu},
    {"FECS_FALCON_ITFEN", 0x00409048u},
    {"FECS_FALCON_CURCTX", 0x00409050u},
    {"FECS_FALCON_CTXACK", 0x00409058u},
    {"FECS_FALCON_PRIVSTATE", 0x00409060u},
    {"FECS_FALCON_SFTRESET", 0x0040907cu},
    {"FECS_FALCON_DEBUG1", 0x00409090u},
    {"FECS_FALCON_IBRKPT1", 0x00409098u},
    {"FECS_FALCON_IBRKPT2", 0x0040909cu},
    {"FECS_FALCON_ADDR", 0x004090acu},
    {"FECS_FALCON_IBRKPT3", 0x004090b0u},
    {"FECS_FALCON_IBRKPT4", 0x004090b4u},
    {"FECS_FALCON_IBRKPT5", 0x004090b8u},
    {"FECS_FALCON_CPUCTL", 0x00409100u},
    {"FECS_FALCON_BOOTVEC", 0x00409104u},
    {"FECS_FALCON_DMACTL", 0x0040910cu},
    {"FECS_FALCON_IMCTL", 0x00409140u},
    {"FECS_FALCON_IMSTAT", 0x00409144u},
    {"FECS_FALCON_TRACEPC", 0x0040914cu},
    {"FECS_FALCON_ICD_CMD", 0x00409200u},
    {"FECS_FALCON_ICD_ADDR", 0x00409204u},
    {"FECS_FALCON_ICD_RDATA", 0x0040920cu},
    {"FECS_FALCON_SCTL", 0x00409240u},
    {"FECS_FALCON_SCTL1", 0x00409250u},
    {"FECS_FALCON_DMEM_DUMMY", 0x00409258u},
    {"FECS_FALCON_DBGCTL", 0x00409260u},
    {"FECS_FALCON_IMEM_PLM", 0x00409280u},
    {"FECS_FALCON_DMEM_PLM", 0x00409284u},
    {"FECS_FALCON_CPUCTL_PLM", 0x00409288u},
    {"FECS_FALCON_EXE_PLM", 0x0040928cu},
    {"FECS_FALCON_IRQTMR_PLM", 0x00409290u},
    {"FECS_FALCON_MTHDCTX_PLM", 0x00409294u},
    {"FECS_FALCON_SCTL_PLM", 0x00409298u},
    {"FECS_FALCON_WDTMR_PLM", 0x0040929cu},
    {"FECS_FALCON_DMAINFO_CTL", 0x004092e0u},
    {"FECS_FEATURE_OVERRIDE_ECC_PLM", 0x0040964cu},
    {"FECS_FEATURE_OVERRIDE_PLM", 0x00409650u},
    {"FECS_FEATURE_OVERRIDE_QUADRO", 0x00409654u},
    {"FECS_FEATURE_OVERRIDE_ECC", 0x00409658u},
    {"FECS_FEATURE_READOUT", 0x00409660u},
    {"FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT", 0x00409664u},
    {"FECS_FEATURE_SECURITY_PLM", 0x00409670u},
    {"FECS_FEATURE_SECURITY_LOCK", 0x00409674u},
    {"FECS_CURRENT_CTX", 0x00409040u},
};

static int read_hex_file(const char *path, unsigned *value) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int ok = fscanf(f, "%x", value);
    fclose(f);
    return ok == 1 ? 0 : -1;
}

static uint32_t read32(const void *base, uint32_t offset) {
    const volatile uint32_t *p = (const volatile uint32_t *)((uintptr_t)base + offset);
    return *p;
}

static void decode_plm(uint32_t value) {
    unsigned read = value & 0x7u;
    unsigned write = (value >> 4) & 0x7u;
    printf(" read_mask=0x%x write_mask=0x%x", read, write);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <pci-bdf> [<pci-bdf>...]\n", argv[0]);
        return 2;
    }

    for (int argi = 1; argi < argc; argi++) {
        const char *bdf = argv[argi];
        char path[256];
        unsigned vendor = 0, device = 0;

        snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/vendor", bdf);
        read_hex_file(path, &vendor);
        snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/device", bdf);
        read_hex_file(path, &device);

        printf("== %s vendor=0x%04x device=0x%04x ==\n", bdf, vendor, device);

        snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource0", bdf);
        int fd = open(path, O_RDONLY | O_SYNC);
        if (fd < 0) {
            printf("open %s failed: %s\n", path, strerror(errno));
            continue;
        }

        struct stat st;
        if (fstat(fd, &st) != 0) {
            printf("fstat %s failed: %s\n", path, strerror(errno));
            close(fd);
            continue;
        }
        size_t map_size = (size_t)st.st_size;
        void *bar0 = mmap(NULL, map_size, PROT_READ, MAP_SHARED, fd, 0);
        if (bar0 == MAP_FAILED) {
            printf("mmap %s failed: %s\n", path, strerror(errno));
            close(fd);
            continue;
        }

        for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
            uint32_t val = 0xffffffffu;
            if (regs[i].offset + sizeof(uint32_t) <= map_size) {
                val = read32(bar0, regs[i].offset);
            }
            printf("0x%08x %-44s 0x%08x", regs[i].offset, regs[i].name, val);
            if (strstr(regs[i].name, "PLM")) {
                decode_plm(val);
            }
            printf("\n");
        }

        munmap(bar0, map_size);
        close(fd);
    }

    return 0;
}
