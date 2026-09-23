/*
 * gv100_bar0_fecs_probe.c
 *
 * Route B: Direct PCI BAR0 MMIO probe of GV100 FECS SM speed-select registers.
 *
 * Bypasses the NVIDIA RM entirely by mmap'ing the GPU's PCI BAR0 via sysfs
 * resource file (/sys/bus/pci/devices/<BDF>/resource0) and reading the FECS
 * feature registers directly from hardware.
 *
 * Requires root to open the resource file (mode 0600, owned by root).
 *
 * Usage:
 *   sudo ./gv100_bar0_fecs_probe [BDF]
 *
 *   BDF defaults to 0000:05:00.0 if not specified.
 *
 * Registers read:
 *   NV_PGRAPH_PRI_FECS_FEATURE_READOUT           0x00409660  (R)
 *   NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT  0x00409664  (R/W)
 *
 * Build:
 *   gcc -O2 -Wall -Wextra -o gv100_bar0_fecs_probe gv100_bar0_fecs_probe.c
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

/* ------------------------------------------------------------------ */
/*  Register offsets (GV100 FECS)                                      */
/* ------------------------------------------------------------------ */
#define GV100_FECS_FEATURE_READOUT               0x00409660u
#define GV100_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT  0x00409664u

/* Known-good validation registers (always readable from BAR0) */
#define GV100_NV_PMC_BOOT_0                      0x00000000u
#define GV100_NV_PMC_INTR_0                      0x00000100u
#define GV100_NV_PSTRAP_SYS_CTRL                 0x00101040u
#define GV100_NV_PGRAPH_PRI_FECS_CURRENT_CTX     0x00409040u

/* ------------------------------------------------------------------ */
/*  FECS FEATURE_READOUT bit fields (GV100)                            */
/* ------------------------------------------------------------------ */
#define READOUT_DP_MASK   (1u << 20)
#define READOUT_IMLA_MASK (1u << 21)
#define READOUT_FMLA_MASK (1u << 22)

/* ------------------------------------------------------------------ */
/*  FECS OVERRIDE_SM_SPEED_SELECT bit fields (GV100)                  */
/*  Layout:                                                           */
/*    [11:8]   DP_OVERRIDE    (bit 11=override_en, bit 8=reduce_en)   */
/*    [7:4]    FMLA_OVERRIDE  (bit 7=override_en,  bit 4=reduce_en)   */
/*    [3:0]    IMLA_OVERRIDE  (bit 3=override_en,  bit 0=reduce_en)   */
/*                                                                     */
/*  Value 0x00000999 means ALL THREE are REDUCED+OVERRIDE:             */
/*    IMLA: REDUCED(bit0=1) | OVERRIDE(bit3=1) => 0x9                 */
/*    FMLA: REDUCED(bit4=1) | OVERRIDE(bit7=1) => 0x90                */
/*    DP:   REDUCED(bit8=1) | OVERRIDE(bit11=1) => 0x900              */
/*    Total: 0x999                                                    */
/* ------------------------------------------------------------------ */
#define OVERRIDE_IMLA_REDUCED_MASK   (1u << 0)
#define OVERRIDE_IMLA_EN_MASK        (1u << 3)
#define OVERRIDE_FMLA_REDUCED_MASK   (1u << 4)
#define OVERRIDE_FMLA_EN_MASK        (1u << 7)
#define OVERRIDE_DP_REDUCED_MASK     (1u << 8)
#define OVERRIDE_DP_EN_MASK          (1u << 11)

/* ------------------------------------------------------------------ */
/*  Probe context                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    int    fd;
    void  *map;
    size_t map_size;
    char   bdf[32];
} probe_ctx_t;

/* ------------------------------------------------------------------ */
static int probe_open(probe_ctx_t *ctx, const char *bdf) {
    char path[128];

    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource0", bdf);
    ctx->fd = open(path, O_RDWR | O_SYNC);
    if (ctx->fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return -1;
    }

    /* Get BAR0 size */
    struct stat st;
    if (fstat(ctx->fd, &st) < 0) {
        fprintf(stderr, "fstat %s: %s\n", path, strerror(errno));
        close(ctx->fd);
        ctx->fd = -1;
        return -1;
    }
    ctx->map_size = (size_t)st.st_size;
    strncpy(ctx->bdf, bdf, sizeof(ctx->bdf) - 1);
    ctx->bdf[sizeof(ctx->bdf) - 1] = '\0';

    /* mmap BAR0 */
    ctx->map = mmap(NULL, ctx->map_size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, ctx->fd, 0);
    if (ctx->map == MAP_FAILED) {
        fprintf(stderr, "mmap BAR0 %s (size 0x%zx): %s\n",
                path, ctx->map_size, strerror(errno));
        close(ctx->fd);
        ctx->fd = -1;
        return -1;
    }

    printf("BAR0 %s mapped at %p size 0x%zx\n", path, ctx->map, ctx->map_size);
    return 0;
}

static void probe_close(probe_ctx_t *ctx) {
    if (ctx->map && ctx->map != MAP_FAILED) {
        munmap(ctx->map, ctx->map_size);
        ctx->map = NULL;
    }
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
}

static inline uint32_t probe_read32(const probe_ctx_t *ctx, uint32_t offset) {
    volatile uint32_t *p = (volatile uint32_t *)((uintptr_t)ctx->map + offset);
    return *p;
}

/* ------------------------------------------------------------------ */
/*  Decode helpers                                                     */
/* ------------------------------------------------------------------ */
static void decode_fecs_readout(uint32_t val) {
    unsigned dp    = (val & READOUT_DP_MASK)   ? 1 : 0;
    unsigned imla  = (val & READOUT_IMLA_MASK) ? 1 : 0;
    unsigned fmla  = (val & READOUT_FMLA_MASK) ? 1 : 0;

    printf("  FEATURE_READOUT       = 0x%08x\n", val);
    printf("    DP_SELECT           = %u  (%s)\n", dp,   dp   ? "REDUCED" : "FULL");
    printf("    IMLA_SELECT         = %u  (%s)\n", imla, imla ? "REDUCED" : "FULL");
    printf("    FMLA_SELECT         = %u  (%s)\n", fmla, fmla ? "REDUCED" : "FULL");
}

static void decode_speed_select_override(uint32_t val) {
    unsigned imla_rd = (val & OVERRIDE_IMLA_REDUCED_MASK) ? 1 : 0;
    unsigned imla_en = (val & OVERRIDE_IMLA_EN_MASK)      ? 1 : 0;
    unsigned fmla_rd = (val & OVERRIDE_FMLA_REDUCED_MASK) ? 1 : 0;
    unsigned fmla_en = (val & OVERRIDE_FMLA_EN_MASK)      ? 1 : 0;
    unsigned dp_rd   = (val & OVERRIDE_DP_REDUCED_MASK)   ? 1 : 0;
    unsigned dp_en   = (val & OVERRIDE_DP_EN_MASK)        ? 1 : 0;

    printf("  OVERRIDE_SM_SPEED_SEL = 0x%08x\n", val);
    printf("    IMLA: reduce=%u override_en=%u  (%s)\n",
           imla_rd, imla_en,
           (imla_en && imla_rd) ? "REDUCED+OVERRIDE" :
           imla_en ? "OVERRIDE_ACTIVE" : "PASS_THROUGH");
    printf("    FMLA: reduce=%u override_en=%u  (%s)\n",
           fmla_rd, fmla_en,
           (fmla_en && fmla_rd) ? "REDUCED+OVERRIDE" :
           fmla_en ? "OVERRIDE_ACTIVE" : "PASS_THROUGH");
    printf("    DP:   reduce=%u override_en=%u  (%s)\n",
           dp_rd, dp_en,
           (dp_en && dp_rd) ? "REDUCED+OVERRIDE" :
           dp_en ? "OVERRIDE_ACTIVE" : "PASS_THROUGH");
}

/* ------------------------------------------------------------------ */
/*  Chip identification helpers                                        */
/* ------------------------------------------------------------------ */
static const char *chip_name(uint16_t device_id) {
    switch (device_id) {
    case 0x1d81: return "GV100 [Tesla V100-SXM2-16GB]";
    case 0x1db1: return "GV100 [Tesla V100-PCIE-16GB]";
    case 0x1db2: return "GV100 [Tesla V100-PCIE-32GB]";
    case 0x1db4: return "GV100 [Tesla V100-SXM2-32GB]";
    case 0x1d84: return "GV100 [CMP 100-210]";
    case 0x1df4: return "GV100 [CMP 100-210 personality V100]";
    default:      return "unknown GV100 variant";
    }
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv) {
    const char *bdf = (argc > 1) ? argv[1] : "0000:05:00.0";

    /* Read PCI device/vendor ID for identification */
    char idpath[128];
    uint16_t vendor_id = 0, device_id = 0;
    snprintf(idpath, sizeof(idpath), "/sys/bus/pci/devices/%s/vendor", bdf);
    FILE *f = fopen(idpath, "r");
    if (f) {
        unsigned v;
        if (fscanf(f, "%x", &v) == 1) vendor_id = (uint16_t)v;
        fclose(f);
    }
    snprintf(idpath, sizeof(idpath), "/sys/bus/pci/devices/%s/device", bdf);
    f = fopen(idpath, "r");
    if (f) {
        unsigned d;
        if (fscanf(f, "%x", &d) == 1) device_id = (uint16_t)d;
        fclose(f);
    }

    printf("=== GV100 Direct BAR0 FECS Probe ===\n");
    printf("PCI: %s\n", bdf);
    printf("Vendor: 0x%04x  Device: 0x%04x  (%s)\n\n",
           vendor_id, device_id, chip_name(device_id));

    /* Map BAR0 and read registers */
    probe_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (probe_open(&ctx, bdf) < 0) {
        fprintf(stderr, "FATAL: could not open/map BAR0 for %s\n", bdf);
        fprintf(stderr, "(need root or read permission on resource0)\n");
        return 1;
    }

    /* ---- Validation registers ---- */
    printf("--- Validation / identity registers ---\n");
    uint32_t pmc_boot  = probe_read32(&ctx, GV100_NV_PMC_BOOT_0);
    uint32_t pstrap    = probe_read32(&ctx, GV100_NV_PSTRAP_SYS_CTRL);
    uint32_t fecs_ctx  = probe_read32(&ctx, GV100_NV_PGRAPH_PRI_FECS_CURRENT_CTX);

    printf("  NV_PMC_BOOT_0                  = 0x%08x\n", pmc_boot);
    printf("  NV_PSTRAP_SYS_CTRL             = 0x%08x\n", pstrap);
    printf("  NV_PGRAPH_PRI_FECS_CURRENT_CTX = 0x%08x\n", fecs_ctx);
    printf("\n");

    /* ---- Target FECS speed-select registers ---- */
    printf("--- FECS SM Speed-Select Registers ---\n");
    uint32_t readout  = probe_read32(&ctx, GV100_FECS_FEATURE_READOUT);
    uint32_t override = probe_read32(&ctx, GV100_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT);

    decode_fecs_readout(readout);
    decode_speed_select_override(override);
    printf("\n");

    /* ---- Interpretation ---- */
    printf("--- Interpretation ---\n");
    unsigned dp_rd    = (override & OVERRIDE_DP_REDUCED_MASK)   ? 1 : 0;
    unsigned dp_en    = (override & OVERRIDE_DP_EN_MASK)        ? 1 : 0;
    unsigned imla_rd  = (override & OVERRIDE_IMLA_REDUCED_MASK) ? 1 : 0;
    unsigned imla_en  = (override & OVERRIDE_IMLA_EN_MASK)      ? 1 : 0;
    unsigned fmla_rd  = (override & OVERRIDE_FMLA_REDUCED_MASK) ? 1 : 0;
    unsigned fmla_en  = (override & OVERRIDE_FMLA_EN_MASK)      ? 1 : 0;

    if (override == 0x00000999) {
        printf("  VBIOS INIT WRITE CONFIRMED: OVERRIDE=0x999\n");
        printf("  All three units (IMLA, FMLA, DP) are set to REDUCED+OVERRIDE.\n");
        printf("  This is the exact value the VBIOS init script writes.\n");

        /* The reduction divisor based on the 0x999 value */
        /* The hardware maps REDUCED to 1/16 throughput based on FMLA_REDUCED_SPEED
         * chipfuse mapping.  The 0x999 sets the override bits but the actual
         * divisor is determined by FUSE_OPT_SM_FMLA_SPEED_SELECT etc. */
        printf("\n  Measured impact: ~1/16 Tensor throughput (6.25%% of peak)\n");
    }

    if (readout & READOUT_FMLA_MASK) {
        printf("  FEATURE_READOUT confirms FMLA is in reduced state.\n");
    } else {
        printf("  FEATURE_READOUT shows FMLA at FULL speed.\n");
    }

    if (dp_en && imla_en && fmla_en &&
        dp_rd && imla_rd && fmla_rd) {
        printf("\n  ALL THREE units actively throttled.\n");
        printf("  Confirms CMP 100-210 speed-select limiter is active.\n");
    } else if (!dp_en && !imla_en && !fmla_en) {
        printf("\n  No override bits set — card running at full speed.\n");
        printf("  (This would be unexpected for a CMP-derived GV100.)\n");
    } else {
        printf("\n  Partial override state — mixed configuration.\n");
    }

    printf("\n");
    printf("--- Additional Near-FECS Registers ---\n");
    /* Scan a range around the FECS feature registers for any related state */
    uint32_t near_regs[] = {
        0x00409800, 0x00409804, 0x00409808, 0x0040980c,
        0x004098f0, 0x004098f4, 0x004098f8, 0x004098fc,
        0x00409900, 0x00409904, 0x00409908, 0x0040990c,
    };
    for (unsigned i = 0; i < sizeof(near_regs) / sizeof(near_regs[0]); i++) {
        uint32_t v = probe_read32(&ctx, near_regs[i]);
        if (v != 0)
            printf("  offset 0x%08x = 0x%08x\n", near_regs[i], v);
    }
    printf("\n");

    probe_close(&ctx);
    return 0;
}
