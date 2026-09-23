/*
 * gv100_cmp_pri_scan.c
 *
 * Rapid PRI address-space probe for CMP 100-210 (0x1D84) GV100.
 *
 * Maps the FECS/GR register region to find exactly which addresses
 * decode vs return 0xFFFFFFFF, then attempts FUSE controller access.
 *
 * Build:
 *   gcc -O2 -Wall -o gv100_cmp_pri_scan gv100_cmp_pri_scan.c
 * Usage:
 *   sudo ./gv100_cmp_pri_scan <minor>
 *
 * The 0xFFFFFFFF readback pattern means "no PRI slave claims this addr".
 * Normal registers return their actual silicon value.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

typedef uint8_t  NvU8;
typedef uint16_t NvU16;
typedef uint32_t NvU32;
typedef uint64_t NvU64;

/* --- ioctl constants (from original tool) --- */
#define NV_IOCTL_MAGIC          'F'
#define NV_ESC_CARD_INFO        200
#define NV_ESC_REGISTER_FD     201
#define NV_ESC_RM_CONTROL      0x2A
#define NV_ESC_RM_ALLOC        0x2B

#define NV01_NULL_OBJECT      0
#define NV01_ROOT              0x00000000
#define NV01_DEVICE_0          0x00000080
#define NV20_SUBDEVICE_0       0x00002080

#define NV0000_CTRL_GPU_MAX_ATTACHED_GPUS 32
#define NV0000_CTRL_GPU_INVALID_ID        0xffffffffu
#define NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS 0x201u
#define NV0000_CTRL_CMD_GPU_GET_ID_INFO   0x202u
#define NV2080_CTRL_CMD_GPU_EXEC_REG_OPS  0x20800122u

#define NV2080_CTRL_GPU_REG_OP_READ_32    0x00000000
#define NV2080_CTRL_GPU_REG_OP_WRITE_32   0x00000001
#define NV2080_CTRL_GPU_REG_OP_TYPE_GLOBAL 0x0u

/* --- data structures (minimal subset) --- */
typedef struct {
    NvU32 domain;
    NvU8  bus;
    NvU8  slot;
    NvU8  function;
    NvU16 vendor_id;
    NvU16 device_id;
} nv_pci_info_t;

typedef struct {
    NvU32            valid;
    nv_pci_info_t    pci_info;
    NvU32            gpu_id;
    NvU16            interrupt_line;
    NvU64            reg_address;
    NvU64            reg_size;
    NvU64            fb_address;
    NvU64            fb_size;
    NvU32            minor_number;
    NvU8             dev_name[10];
} nv_ioctl_card_info_t;

typedef struct {
    int ctl_fd;
} nv_ioctl_register_fd_t;

typedef struct {
    NvU32 hRoot;
    NvU32 hObjectParent;
    NvU32 hObjectNew;
    NvU32 hClass;
    NvU64 pAllocParms;
    NvU32 status;
} NVOS21_PARAMETERS;

typedef struct {
    NvU32 hClient;
    NvU32 hObject;
    NvU32 cmd;
    NvU32 flags;
    NvU64 params;
    NvU32 paramsSize;
    NvU32 status;
} NVOS54_PARAMETERS;

typedef struct {
    NvU32 deviceId;
} NV0080_ALLOC_PARAMETERS;

typedef struct {
    NvU32 subDeviceId;
} NV2080_ALLOC_PARAMETERS;

typedef struct {
    NvU32 gpuIds[NV0000_CTRL_GPU_MAX_ATTACHED_GPUS];
} NV0000_CTRL_GPU_GET_ATTACHED_IDS_PARAMS;

typedef struct {
    NvU32 gpuId;
    NvU32 deviceInstance;
} NV0000_CTRL_GPU_GET_ID_INFO_PARAMS;

typedef struct {
    NvU8  regOp;
    NvU8  regType;
    NvU8  regStatus;
    NvU8  regQuad;
    NvU32 regGroupMask;
    NvU32 regSubGroupMask;
    NvU32 regOffset;
    NvU32 regValueHi;
    NvU32 regValueLo;
    NvU32 regAndNMaskHi;
    NvU32 regAndNMaskLo;
} NV2080_CTRL_GPU_REG_OP;

typedef struct {
    NvU32 hClientTarget;
    NvU32 hChannelTarget;
    NvU32 bNonTransactional;
    NvU32 reserved00[2];
    NvU32 regOpCount;
    NvU64 regOps;
    NvU64 grRouteInfo_pad[2];
} NV2080_CTRL_GPU_EXEC_REG_OPS_PARAMS;

/* --- helpers --- */
static int rm_ioctl(int fd, unsigned nr, void *arg, size_t size) {
    unsigned long request = _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, nr, size);
    return ioctl(fd, request, arg);
}

static NvU32 rm_alloc21(int ctl, NVOS21_PARAMETERS *p) {
    if (rm_ioctl(ctl, NV_ESC_RM_ALLOC, p, sizeof(*p)) < 0) return 0xffffffffu;
    return p->status;
}

static NvU32 rm_control(int ctl, NvU32 client, NvU32 object,
                         NvU32 cmd, void *params, NvU32 params_size) {
    NVOS54_PARAMETERS p;
    memset(&p, 0, sizeof(p));
    p.hClient    = client;
    p.hObject    = object;
    p.cmd        = cmd;
    p.params     = (NvU64)(uintptr_t)params;
    p.paramsSize = params_size;
    if (rm_ioctl(ctl, NV_ESC_RM_CONTROL, &p, sizeof(p)) < 0) return 0xffffffffu;
    return p.status;
}

static NvU32 rm_reg_read(int ctl, NvU32 hClient, NvU32 hObject,
                          NvU32 offset, NvU8 *reg_status) {
    NV2080_CTRL_GPU_REG_OP op;
    NV2080_CTRL_GPU_EXEC_REG_OPS_PARAMS p;
    memset(&op, 0, sizeof(op));
    memset(&p,  0, sizeof(p));
    op.regOp     = NV2080_CTRL_GPU_REG_OP_READ_32;
    op.regType   = NV2080_CTRL_GPU_REG_OP_TYPE_GLOBAL;
    op.regOffset = offset;
    p.bNonTransactional = 1;
    p.regOpCount = 1;
    p.regOps     = (NvU64)(uintptr_t)&op;
    NvU32 st = rm_control(ctl, hClient, hObject,
                          NV2080_CTRL_CMD_GPU_EXEC_REG_OPS, &p, sizeof(p));
    if (reg_status) *reg_status = op.regStatus;
    if (st != 0) return 0xFFFFFFFFu;
    return op.regValueLo;
}

/* --- BAR0 /dev/mem --- */
typedef struct {
    int      fd;
    void    *map;
    size_t   map_size;
    NvU64    bar0_base;
} bar0_handle_t;

static int bar0_open(bar0_handle_t *h, NvU64 base, NvU64 size) {
    memset(h, 0, sizeof(*h));
    h->bar0_base = base;
    h->map_size  = size;
    h->fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (h->fd < 0) { perror("open /dev/mem"); return -1; }
    h->map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, h->fd, base);
    if (h->map == MAP_FAILED) { perror("mmap"); close(h->fd); h->fd = -1; return -1; }
    return 0;
}

static void bar0_close(bar0_handle_t *h) {
    if (h->map && h->map != MAP_FAILED) munmap(h->map, h->map_size);
    if (h->fd >= 0) close(h->fd);
    memset(h, 0, sizeof(*h));
}

static NvU32 bar0_read32(bar0_handle_t *h, NvU32 offset) {
    return *(volatile NvU32 *)((uintptr_t)h->map + offset);
}

/* --- Scan a range, print values that DON'T match the dead pattern --- */
static void scan_range(bar0_handle_t *bar0, const char *label,
                        NvU32 start, NvU32 end, NvU32 step) {
    printf("--- %s (0x%08x - 0x%08x) ---\n", label, start, end);
    int found = 0;
    for (NvU32 off = start; off <= end; off += step) {
        NvU32 v = bar0_read32(bar0, off);
        if (v != 0xFFFFFFFFu && v != 0x00000000u) {
            printf("  0x%08x = 0x%08x\n", off, v);
            found++;
        }
    }
    if (found == 0) printf("  (all 0xFFFFFFFF or 0x00000000 in range)\n");
}

/* ===== main ===== */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: sudo %s <minor>\n", argv[0]);
        return 1;
    }
    int target_minor = atoi(argv[1]);
    if (geteuid() != 0) { fprintf(stderr, "must be root\n"); return 1; }

    /* --- Open nvidiactl --- */
    int ctl = open("/dev/nvidiactl", O_RDWR | O_CLOEXEC);
    if (ctl < 0) { perror("open /dev/nvidiactl"); return 1; }

    /* --- Enumerate --- */
    nv_ioctl_card_info_t cards[32];
    memset(cards, 0, sizeof(cards));
    rm_ioctl(ctl, NV_ESC_CARD_INFO, cards, sizeof(cards));

    int card_index = -1;
    NvU64 bar0_base = 0, bar0_size = 0;
    for (int i = 0; i < 32; i++) {
        if (cards[i].valid) {
            printf("card[%d]: minor=%u dev=0x%04x pci=%04x:%02x:%02x.%u BAR0=0x%016lx+0x%lx\n",
                   i, cards[i].minor_number, cards[i].pci_info.device_id,
                   cards[i].pci_info.domain, cards[i].pci_info.bus,
                   cards[i].pci_info.slot, cards[i].pci_info.function,
                   cards[i].reg_address, cards[i].reg_size);
            if ((int)cards[i].minor_number == target_minor) {
                card_index = i;
                bar0_base = cards[i].reg_address;
                bar0_size = cards[i].reg_size;
            }
        }
    }
    if (card_index < 0) {
        fprintf(stderr, "ERROR: minor %d not found\n", target_minor);
        return 2;
    }
    printf("\nTarget: minor=%d dev=0x%04x BAR0=0x%016lx+0x%lx\n\n",
           target_minor, cards[card_index].pci_info.device_id,
           bar0_base, bar0_size);

    /* --- Allocate RM objects --- */
    NVOS21_PARAMETERS root;
    memset(&root, 0, sizeof(root));
    root.hClass = NV01_ROOT;
    NvU32 st = rm_alloc21(ctl, &root);
    if (st != 0) { fprintf(stderr, "ERROR: alloc root: 0x%08x\n", st); return 3; }
    NvU32 hClient = root.hObjectNew;
    printf("[OK] hClient=0x%08x\n", hClient);

    /* Map gpu_id to deviceInstance */
    NV0000_CTRL_GPU_GET_ATTACHED_IDS_PARAMS attached;
    memset(&attached, 0xff, sizeof(attached));
    rm_control(ctl, hClient, hClient, NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS,
               &attached, sizeof(attached));
    NvU32 device_instance = 0xffffffffu;
    for (int i = 0; i < NV0000_CTRL_GPU_MAX_ATTACHED_GPUS; i++) {
        if (attached.gpuIds[i] == NV0000_CTRL_GPU_INVALID_ID) continue;
        NV0000_CTRL_GPU_GET_ID_INFO_PARAMS id;
        memset(&id, 0, sizeof(id));
        id.gpuId = attached.gpuIds[i];
        rm_control(ctl, hClient, hClient, NV0000_CTRL_CMD_GPU_GET_ID_INFO,
                   &id, sizeof(id));
        if (attached.gpuIds[i] == cards[card_index].gpu_id)
            device_instance = id.deviceInstance;
    }
    if (device_instance == 0xffffffffu) {
        fprintf(stderr, "ERROR: cannot map gpu_id\n"); return 5;
    }
    printf("[OK] device_instance=%u\n", device_instance);

    /* Register fd & alloc device/subdevice */
    char devpath[64];
    snprintf(devpath, sizeof(devpath), "/dev/nvidia%d", target_minor);
    int devfd = open(devpath, O_RDWR | O_CLOEXEC);
    if (devfd < 0) { perror("open /dev/nvidiaN"); return 6; }
    nv_ioctl_register_fd_t reg_fd;
    memset(&reg_fd, 0, sizeof(reg_fd));
    reg_fd.ctl_fd = ctl;
    rm_ioctl(devfd, NV_ESC_REGISTER_FD, &reg_fd, sizeof(reg_fd));

    NvU32 hDevice = 0xd0000000u | ((target_minor & 0xffu) << 8);
    NV0080_ALLOC_PARAMETERS devp;
    memset(&devp, 0, sizeof(devp));
    devp.deviceId = device_instance;
    NVOS21_PARAMETERS devalloc;
    memset(&devalloc, 0, sizeof(devalloc));
    devalloc.hRoot         = hClient;
    devalloc.hObjectParent = hClient;
    devalloc.hObjectNew    = hDevice;
    devalloc.hClass        = NV01_DEVICE_0;
    devalloc.pAllocParms   = (NvU64)(uintptr_t)&devp;
    st = rm_alloc21(ctl, &devalloc);
    if (st != 0) { fprintf(stderr, "ERROR: alloc device: 0x%08x\n", st); return 7; }

    NvU32 hSubdev = hDevice | 1u;
    NV2080_ALLOC_PARAMETERS subp;
    memset(&subp, 0, sizeof(subp));
    subp.subDeviceId = 0;
    NVOS21_PARAMETERS suballoc;
    memset(&suballoc, 0, sizeof(suballoc));
    suballoc.hRoot         = hClient;
    suballoc.hObjectParent = hDevice;
    suballoc.hObjectNew    = hSubdev;
    suballoc.hClass        = NV20_SUBDEVICE_0;
    suballoc.pAllocParms   = (NvU64)(uintptr_t)&subp;
    st = rm_alloc21(ctl, &suballoc);
    if (st != 0) { fprintf(stderr, "ERROR: alloc subdevice: 0x%08x\n", st); return 8; }
    printf("[OK] hDevice=0x%08x hSubdev=0x%08x\n\n", hDevice, hSubdev);

    /* --- Open BAR0 /dev/mem --- */
    bar0_handle_t bar0;
    if (bar0_open(&bar0, bar0_base, bar0_size) < 0) {
        fprintf(stderr, "FATAL: Cannot mmap BAR0\n");
        return 9;
    }
    printf("[OK] BAR0 /dev/mem mapped\n\n");

    /* ==================================================================
     * PHASE 1: RM register probe — find which RM offsets are accessible
     * ================================================================== */
    printf("=== PHASE 1: RM Register Access Probe ===\n\n");

    /* Known-good FECS registers near the base of the block */
    const NvU32 probe_offsets[] = {
        /* Lower FECS block — should be safe */
        0x00409000, 0x00409004, 0x00409008, 0x0040900c,
        0x00409010, 0x00409014, 0x00409018, 0x0040901c,
        0x00409020, 0x00409024, 0x00409028, 0x0040902c,
        /* FECS_CFG range */
        0x00409600, 0x00409604, 0x00409608, 0x0040960c,
        0x00409610, 0x00409614,  /* FECS_CFG1 known */
        0x00409618, 0x0040961c, 0x00409620, 0x00409624,
        0x00409628, 0x0040962c, 0x00409630, 0x00409634,
        0x00409638, 0x0040963c, 0x00409640, 0x00409644, /* CFG2 */
        0x00409648, 0x0040964c,  /* ECC_PLM */
        0x00409650,              /* PRIV_LEVEL_MASK */
        0x00409654,              /* QUADRO */
        0x00409658,              /* ECC */
        0x0040965c, 0x00409660, /* READOUT */
        0x00409664,              /* SM_SPEED_SELECT */
        0x00409668, 0x0040966c,
        0x00409670,              /* SEC_PLM */
        0x00409674,              /* SEC_LOCK */
        0x00409678, 0x0040967c,
        /* End of FECS region */
        0x00409700, 0x00409704,
        /* PEEPHOLE / GR near range */
        0x0040a000, 0x0040a004,
    };

    for (size_t i = 0; i < sizeof(probe_offsets)/sizeof(probe_offsets[0]); i++) {
        NvU32 off = probe_offsets[i];
        NvU8 rs = 0;
        NvU32 v = rm_reg_read(ctl, hClient, hSubdev, off, &rs);
        const char *status_str = (v == 0xFFFFFFFFu) ? "DEAD" : (rs != 0) ? "ERR" : "OK";
        if (v != 0xFFFFFFFFu) {
            printf("  RM 0x%08x = 0x%08x [%s]\n", off, v, status_str);
        }
    }

    printf("\n");

    /* ==================================================================
     * PHASE 2: BAR0 /dev/mem coarse scan of FECS region (0x409000-0x40a000)
     * ================================================================== */
    printf("=== PHASE 2: BAR0 FECS Region Scan (0x408000-0x410000) ===\n\n");

    /* Scan for any non-0xFFFFFFFF, non-0x0 values in FECS range */
    int found_any = 0;
    for (NvU32 off = 0x00408000; off <= 0x0040ffff; off += 4) {
        NvU32 v = bar0_read32(&bar0, off);
        if (v != 0xFFFFFFFFu && v != 0x00000000u) {
            if (found_any < 100) { /* limit output */
                printf("  0x%08x = 0x%08x\n", off, v);
            }
            found_any++;
        }
    }
    printf("  Total non-dead values in 0x408000-0x410000: %d\n\n", found_any);

    /* ==================================================================
     * PHASE 3: Find the FUSE controller base
     * FUSE registers are typically in the 0x0002xxxx range (BAR0 offset)
     * but on GV100 they may be in BAR1 space or at a different PRI base.
     * ================================================================== */
    printf("=== PHASE 3: FUSE Controller Region Scan (0x00020000-0x00024000) ===\n\n");
    /* PMC_BOOT_0 at 0x00000000 tells us the chip ID; FUSE is often nearby */
    NvU32 boot0 = bar0_read32(&bar0, 0x00000000);
    printf("  PMC_BOOT0(0x00000000) = 0x%08x\n", boot0);

    /* Read a few known FECS-adjacent areas to verify mapping */
    NvU32 test_vals[] = {
        0x00000000, /* PMC_BOOT0 */
        0x00000004,
        0x00000008,
        0x00000100, /* PMC or PTOP */
        0x00000104,
        0x00000800,
        0x00000804,
        0x00020000, /* FUSE region base guess */
        0x00020004,
        0x00020008,
        0x0002000c,
        0x00021000,
        0x00021004,
        0x00021008,
        0x0002100c,
        0x00021010,
        0x000210fc, /* FUSE OPT PRIV_LEVEL_MASK */
        0x00021224, /* NV_FUSE_OPT_DP_SPEED_SELECT */
        0x00021410, /* NV_FUSE_OPT_SM_IMLA_SPEED_SELECT */
        0x000214e0, /* NV_FUSE_OPT_SM_FMLA_SPEED_SELECT */
        0x0002381c, /* Ampere-style FEATURE_OVERRIDE (GV100 might not have) */
        0x0082381c, /* GA100-style offset (completely different aperture) */
    };

    printf("  Key BAR0 reads:\n");
    for (size_t i = 0; i < sizeof(test_vals)/sizeof(test_vals[0]); i++) {
        NvU32 off = test_vals[i];
        NvU32 v = bar0_read32(&bar0, off);
        printf("    0x%08x = 0x%08x%s\n", off, v,
               (v == 0xFFFFFFFFu) ? " [DEAD]" :
               (v == 0x00000000u) ? " [ZERO]" : "");
    }
    printf("\n");

    /* ==================================================================
     * PHASE 4: Check FB (BAR1/FB) region — FUSE might be in BAR1 space
     * ================================================================== */
    printf("=== PHASE 4: FB Region Scan ===\n");
    /* The card_info gives us fb_address (BAR1) - map it if we can */
    /* For now, just check some high offsets in BAR0 to see if FUSE aperture
     * appears elsewhere. On GV100, the FUSE controller is at PRI offset
     * that's routed through BAR0 but may be at a different base.
     *
     * Try scanning the 0x008xxxxx range (used for FUSE on Ampere) */
    printf("  Scanning 0x00800000-0x00804000 for FUSE...\n");

    NvU32 found_fuse = 0;
    for (NvU32 off = 0x00800000; off <= 0x00803fff; off += 4) {
        NvU32 v = bar0_read32(&bar0, off);
        if (v != 0xFFFFFFFFu && v != 0x00000000u) {
            if (found_fuse < 50)
                printf("    0x%08x = 0x%08x\n", off, v);
            found_fuse++;
        }
    }
    printf("  Non-dead values in 0x00800000-0x00803fff: %d\n\n", found_fuse);

    /* ==================================================================
     * PHASE 5: Try PRI offset decode — GR is at 0x00400000
     * Scan for the boundary where the FECS feature override region dies
     * ================================================================== */
    printf("=== PHASE 5: GR PRI Boundary Scan (0x00409000-0x00409f00) ===\n\n");
    printf("  Offset   : Value\n");
    printf("  ---------:---------\n");
    for (NvU32 off = 0x00409000; off < 0x00409f00; off += 0x10) {
        NvU32 v = bar0_read32(&bar0, off);
        printf("  0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
               off,
               bar0_read32(&bar0, off),
               bar0_read32(&bar0, off+4),
               bar0_read32(&bar0, off+8),
               bar0_read32(&bar0, off+0xc));
    }

    /* Cleanup */
    bar0_close(&bar0);
    close(devfd);
    close(ctl);

    printf("\nDone.\n");
    return 0;
}
