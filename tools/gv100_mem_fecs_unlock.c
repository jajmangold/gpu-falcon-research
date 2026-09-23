/*
 * gv100_mem_fecs_unlock.c
 *
 * Direct PCI BAR0 MMIO write of GV100 FECS SM speed-select override.
 *
 * Bypasses the NVIDIA RM entirely by mmap'ing the GPU's PCI BAR0 via the
 * sysfs resource file (/sys/bus/pci/devices/<BDF>/resource0) and writing
 * the FECS FEATURE_OVERRIDE_SM_SPEED_SELECT register directly from user
 * space with root.
 *
 * All three user-space RM control paths are exhausted:
 *   - FECS PRI (0x409664): dead decode on CMP, writes non-persistent on V100
 *   - FUSE controller (0x0082381c): not implemented on GV100 (0xbadf1100)
 *   - GR_CTX exec_reg_ops (0x00019900): rejected by RM validation
 *
 * This tool tests if the BAR0 aperture accepts the write even when the RM
 * path is blocked. The hardware may accept the MMIO write at the PCI level
 * even if the RM's PRI decode checks fail, because BAR0 access bypasses the
 * RM's privilege/validation layer.
 *
 * Usage:
 *   sudo ./gv100_mem_fecs_unlock [BDF] [value]
 *
 *   BDF   defaults to 0000:05:00.0
 *   value defaults to 0x00000000 (clear all override bits = FULL SPEED)
 *
 * Strategy:
 *   1. Read FEATURE_READOUT  (0x409660) — query current hardware select
 *   2. Read OVERRIDE         (0x409664) — read current override value
 *   3. WRITE 0x00000000 to OVERRIDE     — clear reduce+override bits
 *   4. Read back OVERRIDE               — verify write took effect
 *   5. Read FEATURE_READOUT             — check if hardware state changed
 *   6. Optionally re-write 0x00000999   — restore if needed
 *
 * Build:
 *   gcc -O2 -Wall -Wextra -o gv100_mem_fecs_unlock gv100_mem_fecs_unlock.c
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

/* Validation registers (always readable from BAR0) */
#define GV100_NV_PMC_BOOT_0                      0x00000000u
#define GV100_NV_PSTRAP_SYS_CTRL                 0x00101040u

/* ------------------------------------------------------------------ */
/*  FECS OVERRIDE_SM_SPEED_SELECT bit fields (GV100)                  */
/*  Layout:                                                           */
/*    [11:8]   DP_OVERRIDE    (bit 11=override_en, bit 8=reduce_en)   */
/*    [7:4]    FMLA_OVERRIDE  (bit 7=override_en,  bit 4=reduce_en)   */
/*    [3:0]    IMLA_OVERRIDE  (bit 3=override_en,  bit 0=reduce_en)   */
/*                                                                     */
/*  Value 0x00000999 means ALL THREE set to REDUCED+OVERRIDE:          */
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

#define OVERRIDE_FULL_SPEED          0x00000000u
#define OVERRIDE_LOCKED_SPEED        0x00000999u

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

    /* mmap BAR0 with read+write */
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

static inline void probe_write32(const probe_ctx_t *ctx, uint32_t offset, uint32_t val) {
    volatile uint32_t *p = (volatile uint32_t *)((uintptr_t)ctx->map + offset);
    *p = val;
    __sync_synchronize();  /* memory barrier to flush write */
}

/* ------------------------------------------------------------------ */
/*  Decode helpers                                                     */
/* ------------------------------------------------------------------ */
static void decode_speed_select_override(uint32_t val, const char *label) {
    unsigned imla_rd = (val & OVERRIDE_IMLA_REDUCED_MASK) ? 1 : 0;
    unsigned imla_en = (val & OVERRIDE_IMLA_EN_MASK)      ? 1 : 0;
    unsigned fmla_rd = (val & OVERRIDE_FMLA_REDUCED_MASK) ? 1 : 0;
    unsigned fmla_en = (val & OVERRIDE_FMLA_EN_MASK)      ? 1 : 0;
    unsigned dp_rd   = (val & OVERRIDE_DP_REDUCED_MASK)   ? 1 : 0;
    unsigned dp_en   = (val & OVERRIDE_DP_EN_MASK)        ? 1 : 0;

    printf("  %s = 0x%08x\n", label, val);
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
/*  Chip identification                                                */
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
    uint32_t write_val = (argc > 2) ? (uint32_t)strtoul(argv[2], NULL, 0) : OVERRIDE_FULL_SPEED;

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

    printf("=== GV100 Direct BAR0 FECS Unlock ===\n");
    printf("PCI: %s\n", bdf);
    printf("Vendor: 0x%04x  Device: 0x%04x  (%s)\n\n",
           vendor_id, device_id, chip_name(device_id));

    /* Map BAR0 */
    probe_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (probe_open(&ctx, bdf) < 0) {
        fprintf(stderr, "FATAL: could not open/map BAR0 for %s\n", bdf);
        fprintf(stderr, "(need root: chmod 666 /sys/bus/pci/devices/%s/resource0 or sudo)\n", bdf);
        return 1;
    }

    /* ---- Step 0: Validation registers ---- */
    printf("--- Validation registers ---\n");
    uint32_t pmc_boot  = probe_read32(&ctx, GV100_NV_PMC_BOOT_0);
    uint32_t pstrap    = probe_read32(&ctx, GV100_NV_PSTRAP_SYS_CTRL);
    printf("  NV_PMC_BOOT_0      = 0x%08x\n", pmc_boot);
    printf("  NV_PSTRAP_SYS_CTRL = 0x%08x\n", pstrap);
    printf("\n");

    /* ---- Step 1: Read current state ---- */
    printf("--- Step 1: Read current FECS state ---\n");
    uint32_t readout_before  = probe_read32(&ctx, GV100_FECS_FEATURE_READOUT);
    uint32_t override_before = probe_read32(&ctx, GV100_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT);

    printf("  FEATURE_READOUT       = 0x%08x\n", readout_before);
    printf("    [20] DP_SELECT    = %u  (%s)\n",
           (readout_before >> 20) & 1u,
           (readout_before & (1u<<20)) ? "REDUCED" : "FULL");
    printf("    [21] IMLA_SELECT  = %u  (%s)\n",
           (readout_before >> 21) & 1u,
           (readout_before & (1u<<21)) ? "REDUCED" : "FULL");
    printf("    [22] FMLA_SELECT  = %u  (%s)\n",
           (readout_before >> 22) & 1u,
           (readout_before & (1u<<22)) ? "REDUCED" : "FULL");

    decode_speed_select_override(override_before, "OVERRIDE_SM_SPEED_SEL (before)");
    printf("\n");

    /* ---- Step 2: Write unlock value ---- */
    printf("--- Step 2: Write 0x%08x to OVERRIDE_SM_SPEED_SELECT ---\n", write_val);
    probe_write32(&ctx, GV100_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT, write_val);
    __sync_synchronize();
    usleep(1000);  /* wait 1ms for write to propagate */

    /* ---- Step 3: Read back ---- */
    printf("--- Step 3: Read back (immediate) ---\n");
    uint32_t override_after  = probe_read32(&ctx, GV100_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT);
    uint32_t readout_after   = probe_read32(&ctx, GV100_FECS_FEATURE_READOUT);

    decode_speed_select_override(override_after, "OVERRIDE_SM_SPEED_SEL (after write)");
    printf("  FEATURE_READOUT       = 0x%08x\n", readout_after);
    printf("    [20] DP_SELECT    = %u  (%s)\n",
           (readout_after >> 20) & 1u,
           (readout_after & (1u<<20)) ? "REDUCED" : "FULL");
    printf("    [21] IMLA_SELECT  = %u  (%s)\n",
           (readout_after >> 21) & 1u,
           (readout_after & (1u<<21)) ? "REDUCED" : "FULL");
    printf("    [22] FMLA_SELECT  = %u  (%s)\n",
           (readout_after >> 22) & 1u,
           (readout_after & (1u<<22)) ? "REDUCED" : "FULL");
    printf("\n");

    /* ---- Step 4: Re-read after short delay ---- */
    printf("--- Step 4: Re-read after 100ms ---\n");
    usleep(100000);
    uint32_t override_delayed = probe_read32(&ctx, GV100_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT);
    uint32_t readout_delayed  = probe_read32(&ctx, GV100_FECS_FEATURE_READOUT);
    decode_speed_select_override(override_delayed, "OVERRIDE_SM_SPEED_SEL (delayed)");
    printf("  FEATURE_READOUT       = 0x%08x\n", readout_delayed);
    printf("\n");

    /* ---- Step 5: Try burst write pattern ---- */
    printf("--- Step 5: Burst write test (5x rapid writes) ---\n");
    for (int i = 0; i < 5; i++) {
        probe_write32(&ctx, GV100_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT, write_val);
        __sync_synchronize();
    }
    usleep(10000);
    uint32_t override_burst = probe_read32(&ctx, GV100_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT);
    decode_speed_select_override(override_burst, "OVERRIDE_SM_SPEED_SEL (after burst)");
    printf("\n");

    /* ---- Results ---- */
    printf("========== RESULTS ==========\n");
    printf("Before:  0x%08x\n", override_before);
    printf("After:   0x%08x\n", override_after);
    printf("Delayed: 0x%08x\n", override_delayed);
    printf("Burst:   0x%08x\n", override_burst);

    if (override_before == OVERRIDE_LOCKED_SPEED) {
        printf("\nSTATUS: Card was LOCKED (0x999) before write.\n");
    } else if (override_before == 0x00000000) {
        printf("\nSTATUS: Card was already UNLOCKED (0x0) before write.\n");
    } else {
        printf("\nSTATUS: Unknown initial state 0x%08x\n", override_before);
    }

    if (override_after == write_val) {
        printf("WRITE VERIFIED: Register accepted new value 0x%08x\n", write_val);
        printf("The BAR0 aperture accepts writes to FECS OVERRIDE_SM_SPEED_SELECT.\n");
        printf("This means the lock CAN be bypassed from user space via /dev/mem.\n");
    } else if (override_after == override_before) {
        printf("WRITE REJECTED: Register retained original value 0x%08x\n", override_before);
        printf("The OVERRIDE register is read-only via BAR0 on this card.\n");
        printf("This suggests the register is gated by a higher privilege level\n");
        printf("that must be set by the VBIOS or PMU during boot.\n");
    } else if (override_after == 0xFFFFFFFF) {
        printf("WRITE REJECTED: Read returns 0xFFFFFFFF (dead/bus error)\n");
        printf("The FECS register block is not mapped in BAR0 on this device.\n");
    } else {
        printf("PARTIAL: Write changed value from 0x%08x to 0x%08x\n",
               override_before, override_after);
        printf("Some bits are read-only or masked.\n");
    }

    /* ---- Step 6: Restore original value (safety) ---- */
    if (override_after == write_val && write_val != override_before) {
        printf("\n--- Step 6: Restoring original value 0x%08x ---\n", override_before);
        probe_write32(&ctx, GV100_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT, override_before);
        __sync_synchronize();
        usleep(10000);
        uint32_t override_restore = probe_read32(&ctx, GV100_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT);
        printf("  Restored: 0x%08x %s\n", override_restore,
               (override_restore == override_before) ? "(MATCH)" : "(MISMATCH)");
    } else {
        printf("\n--- Step 6: Skipping restore (write had no effect or no change) ---\n");
    }

    probe_close(&ctx);
    return 0;
}
