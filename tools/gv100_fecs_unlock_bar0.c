/*
 * gv100_fecs_unlock_bar0.c
 *
 * Direct BAR0 MMIO unlock for GV100 FECS SM speed-select register (0x409664).
 *
 * This completely bypasses the RM driver (Path D). It maps the GPU's
 * PCI BAR0 via /dev/mem and writes directly to the hardware register.
 *
 * Advantages over Path C (RM control ioctl):
 *   - Works even if the RM driver has additional checks not visible in source
 *   - Does not require allocating RM objects
 *   - Can be used on driver versions where EXEC_REG_OPS is restricted
 *
 * Disadvantages:
 *   - Not coordinated with RM driver state
 *   - May conflict with driver context restore
 *   - Requires knowing the GPU's BAR0 physical address
 *
 * Usage:
 *   sudo lspci -D | grep -i nvidia  (find GPU BDF, e.g. 0000:01:00.0)
 *   sudo ./gv100_fecs_unlock_bar0 0000:01:00.0
 *
 * Build:
 *   gcc -O2 -Wall -o gv100_fecs_unlock_bar0 gv100_fecs_unlock_bar0.c
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Register offset within BAR0 */
#define FECS_SM_SPEED_SELECT_OFFSET 0x00409664u
#define CLEAR_VALUE 0x00000000u

/* Read a 32-bit value from BAR0 at given offset */
static uint32_t bar0_read32(volatile void *bar0, uint32_t offset) {
    return *(volatile uint32_t *)((volatile uint8_t *)bar0 + offset);
}

/* Write a 32-bit value to BAR0 at given offset */
static void bar0_write32(volatile void *bar0, uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)((volatile uint8_t *)bar0 + offset) = val;
}

static void decode_speed_select(uint32_t val) {
    unsigned imla          = (val >> 0) & 1u;
    unsigned imla_override = (val >> 3) & 1u;
    unsigned fmla          = (val >> 4) & 1u;
    unsigned fmla_override = (val >> 7) & 1u;
    unsigned dp            = (val >> 8) & 1u;
    unsigned dp_override   = (val >> 11) & 1u;

    printf("  FECS SM_SPEED_SELECT = 0x%08x\n", val);
    printf("    IMLA[0]=%u IMLA_OVERRIDE[3]=%u  -> %s\n",
           imla, imla_override,
           (imla_override && imla) ? "1/16 rate" : "full rate");
    printf("    FMLA[4]=%u FMLA_OVERRIDE[7]=%u  -> %s\n",
           fmla, fmla_override,
           (fmla_override && fmla) ? "1/16 rate" : "full rate");
    printf("    DP[8]=%u   DP_OVERRIDE[11]=%u   -> %s\n",
           dp, dp_override,
           (dp_override && dp) ? "1/16 rate" : "full rate");
}

/* Read BAR0 physical address from sysfs */
static void read_bar0_addr(const char *bdf, uint64_t *addr, uint64_t *size) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource", bdf);
    FILE *f = fopen(path, "r");
    if (!f) {
        *addr = 0;
        *size = 0;
        return;
    }

    unsigned long long start, end, flags;
    for (int i = 0; i < 6; i++) {
        if (fscanf(f, "%llx %llx %llx", &start, &end, &flags) != 3)
            break;
        if (!(flags & 0x1) && start != 0) {
            /* First memory BAR with non-zero address */
            *addr = start;
            *size = end - start + 1;
            fclose(f);
            return;
        }
    }
    fclose(f);
    *addr = 0;
    *size = 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <BDF> [offset]\n", argv[0]);
        fprintf(stderr, "  BDF = PCI Bus:Device.Function, e.g. 0000:01:00.0\n");
        fprintf(stderr, "  offset = register offset (default: 0x%08x)\n",
                FECS_SM_SPEED_SELECT_OFFSET);
        fprintf(stderr, "\nFind BDF:\n");
        fprintf(stderr, "  sudo lspci -D | grep -i nvidia\n");
        return 1;
    }

    const char *bdf = argv[1];
    uint32_t offset = FECS_SM_SPEED_SELECT_OFFSET;
    if (argc > 2) offset = strtoul(argv[2], NULL, 0);

    if (geteuid() != 0) {
        fprintf(stderr, "ERROR: must be run as root (requires /dev/mem access)\n");
        return 1;
    }

    printf("=== GV100 FECS Unlock via Direct BAR0 MMIO ===\n");
    printf("Target BDF: %s\n", bdf);
    printf("Register offset: 0x%08x\n\n", offset);

    /* Get BAR0 physical address */
    uint64_t bar0_paddr = 0, bar0_size = 0;
    read_bar0_addr(bdf, &bar0_paddr, &bar0_size);

    if (bar0_paddr == 0 || bar0_size == 0) {
        fprintf(stderr, "ERROR: cannot determine BAR0 address for %s\n", bdf);
        fprintf(stderr, "Check that the device exists:\n");
        fprintf(stderr, "  ls -la /sys/bus/pci/devices/%s/\n", bdf);
        return 2;
    }
    printf("BAR0 physical address: 0x%lx\n", (unsigned long)bar0_paddr);
    printf("BAR0 size:            %lu bytes (%.1f MB)\n",
           (unsigned long)bar0_size, (double)bar0_size / (1024*1024));

    if (offset >= bar0_size) {
        fprintf(stderr, "ERROR: offset 0x%08x exceeds BAR0 size 0x%lx\n",
                offset, (unsigned long)bar0_size);
        return 3;
    }

    /* Map BAR0 via /dev/mem */
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 4;
    }

    volatile void *bar0 = mmap(NULL, bar0_size, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, bar0_paddr);
    if (bar0 == MAP_FAILED) {
        perror("mmap BAR0");
        close(fd);
        return 5;
    }
    printf("[OK] Mapped BAR0 at virtual address %p\n\n", bar0);

    /* Step 1: Read current state */
    printf("=== Step 1: Read current FECS SM_SPEED_SELECT ===\n");
    uint32_t before = bar0_read32(bar0, offset);
    decode_speed_select(before);
    printf("\n");

    /* Step 2: Write clear value */
    printf("=== Step 2: Writing CLEAR_VALUE (0x%08x) to 0x%08x ===\n",
           CLEAR_VALUE, offset);
    bar0_write32(bar0, offset, CLEAR_VALUE);
    printf("[OK] Write completed\n\n");

    /* Step 3: Read-back verification */
    printf("=== Step 3: Read-back verification ===\n");
    uint32_t after = bar0_read32(bar0, offset);
    decode_speed_select(after);

    if (after == 0) {
        printf("\n*** SUCCESS: FECS SM speed-select overrides cleared! ***\n");
        printf("    IMLA, FMLA, DP should now run at full issue rate.\n");
        printf("\nNOTE: The RM driver may restore this register on context switch.\n");
        printf("If the override returns, consider:\n");
        printf("  1. Using the RMOverrideSmSpeedSelect regkey to persist the clear\n");
        printf("  2. Writing the register periodically via a background task\n");
        printf("  3. Running benchmark immediately after this tool exits\n");
    } else {
        printf("\n*** NOTE: Value is 0x%08x (expected 0x00000000) ***\n", after);
        printf("    Check if write-combining or write-protection is active.\n");
        printf("    BAR0 may be mapped WC (write-combining) by the kernel.\n");
    }

    munmap((void *)bar0, bar0_size);
    close(fd);
    return 0;
}
