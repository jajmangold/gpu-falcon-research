/*
 * gv100_fuse_unlock.c
 *
 * GV100 FUSE controller SM_SPEED_SELECT probe tool.
 *
 * Background:
 *   The FECS FEATURE_OVERRIDE_SM_SPEED_SELECT at BAR0 0x00409664 returns
 *   0xFFFFFFFF on CMP SKUs (0x1d84) — the entire PGRAPH PRI decode region
 *   is dead on those chips. The FUSE controller at 0x0082xxxx is a completely
 *   separate on-die bus segment and may still be alive on CMP.
 *
 * FUSE register map (dev_fuse.h):
 *   NV_FUSE_FEATURE_OVERRIDE_PRIV_LEVEL_MASK        0x00823804 (RW-4R)
 *   NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT         0x0082381c (RW-4R)
 *   NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT_1       0x00823820 (RW-4R)
 *   NV_FUSE_FEATURE_OVERRIDE_ROW_REMAPPER             0x00823824 (RW-4R)
 *
 * FUSE SM_SPEED_SELECT bit layout (Turing+ 3-bit per-datapath):
 *   0x0082381c: IMLA0[2:0], IMLA0_OVERRIDE[3],
 *               FMLA16[6:4], FMLA16_OVERRIDE[7],
 *               DP[8],       DP_OVERRIDE[11],
 *               FMLA32[14:12],
 *               FFMA[18:16],
 *               IMLA1[22:20], IMLA2[26:24], IMLA3[30:28]
 *   0x00823820: IMLA4 fields
 *
 * On GV100 these are 1-bit fields (same encoding as FECS override register).
 * The 3-bit encoding applies to Turing+.
 *
 * Full-speed values:
 *   FUSE_SS_FULL_SPEED_1BIT  = 0x00000000  (all bits zero = no override)
 *   FUSE_SS_FULL_SPEED_3BIT  = 0x00000888  (OVERRIDE bits set, datapath=0
 *                                            for each 3-bit field)
 *   FUSE_SS_1_FULL_SPEED     = 0x00000000  (IMLA4 fields)
 *
 * Usage:
 *   sudo ./gv100_fuse_unlock [minor_number]
 *
 * Build:
 *   gcc -O2 -Wall -o gv100_fuse_unlock gv100_fuse_unlock.c
 *
 * The ACR firmware on T234 writes these registers via priWrite() from
 * SEC2 Falcon — same offsets, same register definitions.
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
typedef  int32_t NvS32;
typedef uint32_t NvHandle;

/* --- ioctl constants --- */
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

/* --- FUSE controller register offsets (BAR0 absolute) --- */
#define FUSE_FEATURE_OVERRIDE_BASE          0x00823800u
#define FUSE_FEATURE_OVERRIDE_PLM_OFFSET    0x00823804u
#define FUSE_FEATURE_OVERRIDE_SS_OFFSET     0x0082381cu
#define FUSE_FEATURE_OVERRIDE_SS1_OFFSET    0x00823820u
#define FUSE_FEATURE_OVERRIDE_REMAP_OFFSET  0x00823824u

/* Probe range for FUSE bus health check */
#define FUSE_PROBE_START    0x00800000u
#define FUSE_PROBE_END      0x00803fffu
#define FUSE_PROBE_STRIDE   0x00000100u

/* PLM bitfields (same as FECS PLM) */
#define PLM_READ_PROTECTION_MASK    0x00000007u
#define PLM_READ_VIOLATION_MASK     0x00000008u
#define PLM_WRITE_PROTECTION_MASK   0x00000070u
#define PLM_WRITE_VIOLATION_MASK    0x00000080u
#define PLM_ALL_DISABLED            0x00000000u

/* Full-speed values for SM_SPEED_SELECT */
#define FUSE_SS_FULL_1BIT           0x00000000u
#define FUSE_SS_FULL_3BIT           0x00000888u
#define FUSE_SS1_FULL               0x00000000u

/* --- data structures --- */
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
    NvHandle hRoot;
    NvHandle hObjectParent;
    NvHandle hObjectNew;
    NvU32    hClass;
    NvU64    pAllocParms;
    NvU32    status;
} NVOS21_PARAMETERS;

typedef struct {
    NvHandle hRoot;
    NvHandle hObjectParent;
    NvHandle hObjectNew;
    NvU32    hClass;
    NvU64    pAllocParms;
    NvU64    pRightsRequested;
    NvU32    status;
} NVOS64_PARAMETERS;

typedef struct {
    NvHandle hClient;
    NvHandle hObject;
    NvU32    cmd;
    NvU32    flags;
    NvU64    params;
    NvU32    paramsSize;
    NvU32    status;
} NVOS54_PARAMETERS;

typedef struct {
    NvU32 deviceId;
    NvHandle hClientShare;
    NvHandle hTargetClient;
    NvHandle hTargetDevice;
    NvU32 flags;
    NvU64 vaSpaceSize;
    NvU64 vaStartInternal;
    NvU64 vaLimitInternal;
    NvU32 vaMode;
} NV0080_ALLOC_PARAMETERS;

typedef struct {
    NvU32 subDeviceId;
} NV2080_ALLOC_PARAMETERS;

typedef struct {
    NvU32 gpuIds[NV0000_CTRL_GPU_MAX_ATTACHED_GPUS];
} NV0000_CTRL_GPU_GET_ATTACHED_IDS_PARAMS;

typedef struct {
    NvU32 gpuId;
    NvU32 gpuFlags;
    NvU32 deviceInstance;
    NvU32 subDeviceInstance;
    NvU64 szName;
    NvU32 sliStatus;
    NvU32 boardId;
    NvU32 gpuInstance;
    NvS32 numaId;
} NV0000_CTRL_GPU_GET_ID_INFO_PARAMS;

typedef struct {
    NvU32 flags;
    NvU32 pad;
    NvU64 route;
} NV2080_CTRL_GR_ROUTE_INFO;

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
    NvHandle hClientTarget;
    NvHandle hChannelTarget;
    NvU32    bNonTransactional;
    NvU32    reserved00[2];
    NvU32    regOpCount;
    NvU64    regOps;
    NV2080_CTRL_GR_ROUTE_INFO grRouteInfo;
} NV2080_CTRL_GPU_EXEC_REG_OPS_PARAMS;

/* --- BAR0 /dev/mem handle --- */
typedef struct {
    int      fd;
    void    *map;
    size_t   map_size;
    NvU64    bar0_base;
} bar0_handle_t;

/* --- helpers --- */
static int rm_ioctl(int fd, unsigned nr, void *arg, size_t size) {
    unsigned long request = _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, nr, size);
    return ioctl(fd, request, arg);
}

static NvU32 rm_alloc21(int ctl, NVOS21_PARAMETERS *p) {
    if (rm_ioctl(ctl, NV_ESC_RM_ALLOC, p, sizeof(*p)) < 0) {
        fprintf(stderr, "ERROR: alloc21 ioctl: %s\n", strerror(errno));
        return 0xffffffffu;
    }
    return p->status;
}

static NvU32 rm_alloc64(int ctl, NVOS64_PARAMETERS *p) {
    if (rm_ioctl(ctl, NV_ESC_RM_ALLOC, p, sizeof(*p)) < 0) {
        fprintf(stderr, "ERROR: alloc64 ioctl: %s\n", strerror(errno));
        return 0xffffffffu;
    }
    return p->status;
}

static NvU32 rm_control(int ctl, NvHandle client, NvHandle object,
                         NvU32 cmd, void *params, NvU32 params_size) {
    NVOS54_PARAMETERS p;
    memset(&p, 0, sizeof(p));
    p.hClient    = client;
    p.hObject    = object;
    p.cmd        = cmd;
    p.params     = (NvU64)(uintptr_t)params;
    p.paramsSize = params_size;
    if (rm_ioctl(ctl, NV_ESC_RM_CONTROL, &p, sizeof(p)) < 0) {
        fprintf(stderr, "ERROR: control ioctl cmd=0x%08x: %s\n", cmd, strerror(errno));
        return 0xffffffffu;
    }
    return p.status;
}

/* RM regop read */
static NvU32 rm_reg_read(int ctl, NvHandle hClient, NvHandle hObject,
                          NvU32 offset) {
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
    if (st != 0) return 0xFFFFFFFFu;
    return op.regValueLo;
}

/* RM regop write, returns NV status */
static NvU32 rm_reg_write(int ctl, NvHandle hClient, NvHandle hObject,
                           NvU32 offset, NvU32 value) {
    NV2080_CTRL_GPU_REG_OP op;
    NV2080_CTRL_GPU_EXEC_REG_OPS_PARAMS p;
    memset(&op, 0, sizeof(op));
    memset(&p,  0, sizeof(p));
    op.regOp      = NV2080_CTRL_GPU_REG_OP_WRITE_32;
    op.regType    = NV2080_CTRL_GPU_REG_OP_TYPE_GLOBAL;
    op.regOffset  = offset;
    op.regValueLo = value;
    op.regAndNMaskLo = 0xFFFFFFFFu;
    p.bNonTransactional = 1;
    p.regOpCount = 1;
    p.regOps     = (NvU64)(uintptr_t)&op;
    NvU32 st = rm_control(ctl, hClient, hObject,
                          NV2080_CTRL_CMD_GPU_EXEC_REG_OPS, &p, sizeof(p));
    return st;
}

/* BAR0 /dev/mem helpers */
static int bar0_open(bar0_handle_t *h, NvU64 bar0_base, NvU64 bar0_size) {
    memset(h, 0, sizeof(*h));
    h->bar0_base = bar0_base;
    h->map_size  = bar0_size;
    h->fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (h->fd < 0) {
        perror("open /dev/mem");
        return -1;
    }
    h->map = mmap(NULL, bar0_size, PROT_READ | PROT_WRITE,
                  MAP_SHARED, h->fd, bar0_base);
    if (h->map == MAP_FAILED) {
        perror("mmap BAR0");
        close(h->fd);
        h->fd = -1;
        return -1;
    }
    return 0;
}

static void bar0_close(bar0_handle_t *h) {
    if (h->map && h->map != MAP_FAILED)
        munmap(h->map, h->map_size);
    if (h->fd >= 0)
        close(h->fd);
    memset(h, 0, sizeof(*h));
}

static NvU32 bar0_read32(bar0_handle_t *h, NvU32 offset) {
    volatile NvU32 *p = (volatile NvU32 *)((uintptr_t)h->map + offset);
    return *p;
}

static void bar0_write32(bar0_handle_t *h, NvU32 offset, NvU32 val) {
    volatile NvU32 *p = (volatile NvU32 *)((uintptr_t)h->map + offset);
    *p = val;
    __sync_synchronize();
}

/* ====================================================================
 * Decode functions
 * ==================================================================== */

/* Decode FUSE SM_SPEED_SELECT (Turing+ 3-bit per-datapath layout) */
static void decode_fuse_ss(NvU32 val) {
    unsigned imla0          = (val >> 0)  & 0x7u;
    unsigned imla0_ovr      = (val >> 3)  & 0x1u;
    unsigned fmla16         = (val >> 4)  & 0x7u;
    unsigned fmla16_ovr     = (val >> 7)  & 0x1u;
    unsigned dp             = (val >> 8)  & 0x1u;
    unsigned dp_ovr         = (val >> 11) & 0x1u;
    unsigned fmla32         = (val >> 12) & 0x7u;
    unsigned ffma           = (val >> 16) & 0x7u;
    unsigned imla1          = (val >> 20) & 0x7u;
    unsigned imla2          = (val >> 24) & 0x7u;
    unsigned imla3          = (val >> 28) & 0x7u;

    printf("  FUSE SS(0x2381c) = 0x%08x\n", val);
    printf("    IMLA0[2:0]=%u OVR[3]=%u  -> %s\n",
           imla0, imla0_ovr,
           imla0_ovr ? (imla0 ? "LIMITED" : "FULL") : "NO_OVERRIDE");
    printf("    FMLA16[6:4]=%u OVR[7]=%u -> %s\n",
           fmla16, fmla16_ovr,
           fmla16_ovr ? (fmla16 ? "LIMITED" : "FULL") : "NO_OVERRIDE");
    printf("    DP[8]=%u OVR[11]=%u     -> %s\n",
           dp, dp_ovr,
           dp_ovr ? (dp ? "LIMITED" : "FULL") : "NO_OVERRIDE");
    printf("    FMLA32[14:12]=%u FFMA[18:16]=%u\n",
           fmla32, ffma);
    printf("    IMLA1[22:20]=%u IMLA2[26:24]=%u IMLA3[30:28]=%u\n",
           imla1, imla2, imla3);
}

/* Decode FUSE SM_SPEED_SELECT_1 (IMLA4) */
static void decode_fuse_ss1(NvU32 val) {
    unsigned imla4 = (val >> 0) & 0x7u;
    unsigned imla4_ovr = (val >> 3) & 0x1u;
    printf("  FUSE SS1(0x23820) = 0x%08x\n", val);
    printf("    IMLA4[2:0]=%u OVR[3]=%u -> %s\n",
           imla4, imla4_ovr,
           imla4_ovr ? (imla4 ? "LIMITED" : "FULL") : "NO_OVERRIDE");
}

/* Decode PRIV_LEVEL_MASK */
static void decode_plm(const char *name, NvU32 val) {
    NvU32 rp = val & PLM_READ_PROTECTION_MASK;
    NvU32 rv = (val >> 3) & 1u;
    NvU32 wp = (val >> 4) & 0x07u;
    NvU32 wv = (val >> 7) & 1u;
    printf("  %s = 0x%08x\n", name, val);
    printf("    READ_PROTECTION[2:0]=0x%x ", rp);
    printf("(L0=%c L1=%c L2=%c)\n",
           (rp & 1) ? 'E' : 'D',
           (rp & 2) ? 'E' : 'D',
           (rp & 4) ? 'E' : 'D');
    printf("    READ_VIOLATION[3]=%u (%s)\n",
           rv, rv ? "REPORT_ERROR" : "SOLDIER_ON");
    printf("    WRITE_PROTECTION[6:4]=0x%x ", wp);
    printf("(L0=%c L1=%c L2=%c)\n",
           (wp & 1) ? 'E' : 'D',
           (wp & 2) ? 'E' : 'D',
           (wp & 4) ? 'E' : 'D');
    printf("    WRITE_VIOLATION[7]=%u (%s)\n",
           wv, wv ? "REPORT_ERROR" : "SOLDIER_ON");
}

/* ====================================================================
 * FUSE bus probe: scan 0x00800000-0x00803fff for live register ranges
 * ==================================================================== */
static void probe_fuse_bus(bar0_handle_t *bar0) {
    if (!bar0 || !bar0->map) return;

    printf("--- FUSE Bus Health Scan (0x00800000-0x00803fff) ---\n");
    int in_dead_run = 0;
    int live_count = 0;
    int dead_count = 0;

    for (NvU32 off = FUSE_PROBE_START; off <= FUSE_PROBE_END; off += FUSE_PROBE_STRIDE) {
        NvU32 val = bar0_read32(bar0, off);
        if (val == 0xFFFFFFFFu) {
            if (!in_dead_run) {
                if (live_count > 0)
                    printf("  ...dead zone starts at 0x%08x\n", off);
                in_dead_run = 1;
            }
            dead_count++;
        } else {
            if (in_dead_run) {
                printf("  ...live region resumes at 0x%08x (val=0x%08x)\n", off, val);
                in_dead_run = 0;
            }
            if (live_count == 0) {
                printf("  First live at 0x%08x = 0x%08x\n", off, val);
            }
            live_count++;
        }
    }

    printf("  FUSE bus scan complete: %d live, %d dead reads\n\n",
           live_count, dead_count);
}

/* ====================================================================
 * main
 * ==================================================================== */
int main(int argc, char **argv) {
    int target_minor = 14;
    if (argc > 1) target_minor = atoi(argv[1]);

    if (geteuid() != 0) {
        fprintf(stderr, "ERROR: must be run as root\n");
        return 1;
    }

    printf("=== GV100 FUSE Controller SM_SPEED_SELECT Probe Tool ===\n");
    printf("Target GPU minor: %d\n\n", target_minor);

    /* ---- Open nvidia control device ---- */
    int ctl = open("/dev/nvidiactl", O_RDWR | O_CLOEXEC);
    if (ctl < 0) { perror("open /dev/nvidiactl"); return 1; }

    /* ---- Enumerate cards ---- */
    nv_ioctl_card_info_t cards[32];
    memset(cards, 0, sizeof(cards));
    if (rm_ioctl(ctl, NV_ESC_CARD_INFO, cards, sizeof(cards)) < 0) {
        perror("ioctl CARD_INFO"); return 1;
    }

    int card_index = -1;
    for (int i = 0; i < 32; i++) {
        if (cards[i].valid) {
            printf("  card[%d]: minor=%u gpu_id=0x%08x pci=%04x:%02x:%02x.%u "
                   "dev=0x%04x BAR0=0x%016lx+0x%lx\n",
                   i, cards[i].minor_number, cards[i].gpu_id,
                   cards[i].pci_info.domain, cards[i].pci_info.bus,
                   cards[i].pci_info.slot, cards[i].pci_info.function,
                   cards[i].pci_info.device_id,
                   cards[i].reg_address, cards[i].reg_size);
            if ((int)cards[i].minor_number == target_minor)
                card_index = i;
        }
    }
    if (card_index < 0) {
        fprintf(stderr, "ERROR: target minor %d not found\n", target_minor);
        return 2;
    }
    NvU64 bar0_base = cards[card_index].reg_address;
    NvU64 bar0_size = cards[card_index].reg_size;
    printf("\n");

    /* ---- Allocate RM objects ---- */
    NVOS21_PARAMETERS root;
    memset(&root, 0, sizeof(root));
    root.hClass = NV01_ROOT;
    NvU32 st = rm_alloc21(ctl, &root);
    if (st != 0) { fprintf(stderr, "ERROR: alloc root: 0x%08x\n", st); return 3; }
    NvHandle hClient = root.hObjectNew;
    printf("[OK] RM root client: hClient=0x%08x\n", hClient);

    NV0000_CTRL_GPU_GET_ATTACHED_IDS_PARAMS attached;
    memset(&attached, 0xff, sizeof(attached));
    st = rm_control(ctl, hClient, hClient,
                    NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS,
                    &attached, sizeof(attached));
    if (st != 0) { fprintf(stderr, "ERROR: get attached ids\n"); return 4; }

    NvU32 device_instance = 0xffffffffu;
    for (int i = 0; i < NV0000_CTRL_GPU_MAX_ATTACHED_GPUS; i++) {
        if (attached.gpuIds[i] == NV0000_CTRL_GPU_INVALID_ID) continue;
        NV0000_CTRL_GPU_GET_ID_INFO_PARAMS id;
        memset(&id, 0, sizeof(id));
        id.gpuId = attached.gpuIds[i];
        NvU32 _ist = rm_control(ctl, hClient, hClient,
                                NV0000_CTRL_CMD_GPU_GET_ID_INFO,
                                &id, sizeof(id));
        (void)_ist;
        if (attached.gpuIds[i] == cards[card_index].gpu_id)
            device_instance = id.deviceInstance;
    }
    if (device_instance == 0xffffffffu) {
        fprintf(stderr, "ERROR: cannot map gpu_id to device instance\n");
        return 5;
    }
    printf("[OK] device_instance=%u\n", device_instance);

    char devpath[64];
    snprintf(devpath, sizeof(devpath), "/dev/nvidia%d", target_minor);
    int devfd = open(devpath, O_RDWR | O_CLOEXEC);
    if (devfd < 0) { perror("open /dev/nvidiaN"); return 6; }
    nv_ioctl_register_fd_t reg;
    memset(&reg, 0, sizeof(reg));
    reg.ctl_fd = ctl;
    rm_ioctl(devfd, NV_ESC_REGISTER_FD, &reg, sizeof(reg));
    printf("[OK] Registered ctl fd on %s\n", devpath);

    NvHandle hDevice = 0xd0000000u | ((target_minor & 0xffu) << 8);
    NV0080_ALLOC_PARAMETERS devp;
    memset(&devp, 0, sizeof(devp));
    devp.deviceId = device_instance;
    NVOS64_PARAMETERS devalloc;
    memset(&devalloc, 0, sizeof(devalloc));
    devalloc.hRoot         = hClient;
    devalloc.hObjectParent = hClient;
    devalloc.hObjectNew    = hDevice;
    devalloc.hClass        = NV01_DEVICE_0;
    devalloc.pAllocParms   = (NvU64)(uintptr_t)&devp;
    st = rm_alloc64(ctl, &devalloc);
    if (st == 0xffffffffu) {
        NVOS21_PARAMETERS devalloc21;
        memset(&devalloc21, 0, sizeof(devalloc21));
        devalloc21.hRoot         = hClient;
        devalloc21.hObjectParent = hClient;
        devalloc21.hObjectNew    = hDevice;
        devalloc21.hClass        = NV01_DEVICE_0;
        devalloc21.pAllocParms   = (NvU64)(uintptr_t)&devp;
        st = rm_alloc21(ctl, &devalloc21);
    }
    if (st != 0) { fprintf(stderr, "ERROR: alloc device: 0x%08x\n", st); return 7; }
    printf("[OK] RM device: hDevice=0x%08x\n", hDevice);

    NvHandle hSubdev = hDevice | 1u;
    NV2080_ALLOC_PARAMETERS subp;
    memset(&subp, 0, sizeof(subp));
    subp.subDeviceId = 0;
    NVOS64_PARAMETERS suballoc;
    memset(&suballoc, 0, sizeof(suballoc));
    suballoc.hRoot         = hClient;
    suballoc.hObjectParent = hDevice;
    suballoc.hObjectNew    = hSubdev;
    suballoc.hClass        = NV20_SUBDEVICE_0;
    suballoc.pAllocParms   = (NvU64)(uintptr_t)&subp;
    st = rm_alloc64(ctl, &suballoc);
    if (st == 0xffffffffu) {
        NVOS21_PARAMETERS suballoc21;
        memset(&suballoc21, 0, sizeof(suballoc21));
        suballoc21.hRoot         = hClient;
        suballoc21.hObjectParent = hDevice;
        suballoc21.hObjectNew    = hSubdev;
        suballoc21.hClass        = NV20_SUBDEVICE_0;
        suballoc21.pAllocParms   = (NvU64)(uintptr_t)&subp;
        st = rm_alloc21(ctl, &suballoc21);
    }
    if (st != 0) { fprintf(stderr, "ERROR: alloc subdevice: 0x%08x\n", st); return 8; }
    printf("[OK] RM subdevice: hSubdev=0x%08x\n\n", hSubdev);

    /* ---- Open BAR0 /dev/mem ---- */
    bar0_handle_t bar0;
    if (bar0_open(&bar0, bar0_base, bar0_size) < 0) {
        fprintf(stderr, "WARNING: Cannot open BAR0 directly, will use RM path only\n\n");
        memset(&bar0, 0, sizeof(bar0));
    } else {
        printf("[OK] BAR0 /dev/mem mapped at 0x%016lx+0x%lx\n\n", bar0_base, bar0_size);
    }

    /* ====================================================================
     * STEP 0: FUSE Bus Health Scan
     * ==================================================================== */
    if (bar0.map) {
        printf("=== STEP 0: FUSE Bus Health Scan ===\n");
        probe_fuse_bus(&bar0);
    }

    /* ====================================================================
     * STEP 1: Read FUSE FEATURE_OVERRIDE registers via BAR0 and RM
     * ==================================================================== */
    printf("=== STEP 1: FUSE FEATURE_OVERRIDE Region Dump ===\n");

    /* BAR0 probe of all 4 FUSE registers */
    if (bar0.map) {
        printf("  Via BAR0 /dev/mem:\n");
        NvU32 fuse_plm_b0   = bar0_read32(&bar0, FUSE_FEATURE_OVERRIDE_PLM_OFFSET);
        NvU32 fuse_ss_b0    = bar0_read32(&bar0, FUSE_FEATURE_OVERRIDE_SS_OFFSET);
        NvU32 fuse_ss1_b0   = bar0_read32(&bar0, FUSE_FEATURE_OVERRIDE_SS1_OFFSET);
        NvU32 fuse_remap_b0 = bar0_read32(&bar0, FUSE_FEATURE_OVERRIDE_REMAP_OFFSET);

        printf("  BAR0@0x%08x (PLM):        0x%08x\n",
               FUSE_FEATURE_OVERRIDE_PLM_OFFSET, fuse_plm_b0);
        printf("  BAR0@0x%08x (SM_SPEED):   0x%08x\n",
               FUSE_FEATURE_OVERRIDE_SS_OFFSET, fuse_ss_b0);
        printf("  BAR0@0x%08x (SM_SPEED_1): 0x%08x\n",
               FUSE_FEATURE_OVERRIDE_SS1_OFFSET, fuse_ss1_b0);
        printf("  BAR0@0x%08x (REMAP):      0x%08x\n",
               FUSE_FEATURE_OVERRIDE_REMAP_OFFSET, fuse_remap_b0);

        /* Decode non-dead values */
        if (fuse_plm_b0 != 0xFFFFFFFFu)
            decode_plm("FUSE_PLM(0x23804)", fuse_plm_b0);
        if (fuse_ss_b0 != 0xFFFFFFFFu) {
            printf("  --- FUSE SS decode ---\n");
            decode_fuse_ss(fuse_ss_b0);
        }
        if (fuse_ss1_b0 != 0xFFFFFFFFu)
            decode_fuse_ss1(fuse_ss1_b0);

        /* Diagnose FUSE region aliveness */
        NvU32 dead_mask = 0;
        if (fuse_plm_b0 == 0xFFFFFFFFu)   dead_mask |= 1;
        if (fuse_ss_b0 == 0xFFFFFFFFu)    dead_mask |= 2;
        if (fuse_ss1_b0 == 0xFFFFFFFFu)   dead_mask |= 4;
        if (fuse_remap_b0 == 0xFFFFFFFFu) dead_mask |= 8;

        if (dead_mask == 0) {
            printf("\n  *** FUSE controller region is ALIVE on this GPU! ***\n");
        } else if (dead_mask == 0xF) {
            printf("\n  *** FUSE controller region is DEAD (all 0xFFFFFFFF) on this GPU. ***\n");
            printf("  *** Same failure mode as FECS region on CMP. ***\n");
        } else {
            printf("\n  *** FUSE region partially alive. Dead regs mask: 0x%x ***\n", dead_mask);
        }
        printf("\n");

        /* Also compare with known FECS values for V100 sanity */
        NvU32 fecs_plm_b0 = bar0_read32(&bar0, 0x00409650u);
        NvU32 fecs_ss_b0  = bar0_read32(&bar0, 0x00409664u);
        printf("  FECS reference (for V100 sanity check):\n");
        printf("    FECS_PLM(0x409650)  = 0x%08x\n", fecs_plm_b0);
        printf("    FECS_SS(0x409664)   = 0x%08x\n", fecs_ss_b0);
        printf("\n");
    }

    /* RM regop read of FUSE registers */
    printf("  Via RM regop:\n");
    NvU32 fuse_plm_rm   = rm_reg_read(ctl, hClient, hSubdev,
                                       FUSE_FEATURE_OVERRIDE_PLM_OFFSET);
    NvU32 fuse_ss_rm    = rm_reg_read(ctl, hClient, hSubdev,
                                       FUSE_FEATURE_OVERRIDE_SS_OFFSET);
    NvU32 fuse_ss1_rm   = rm_reg_read(ctl, hClient, hSubdev,
                                       FUSE_FEATURE_OVERRIDE_SS1_OFFSET);
    NvU32 fuse_remap_rm = rm_reg_read(ctl, hClient, hSubdev,
                                       FUSE_FEATURE_OVERRIDE_REMAP_OFFSET);

    printf("  RM@0x%08x (PLM):        0x%08x\n",
           FUSE_FEATURE_OVERRIDE_PLM_OFFSET, fuse_plm_rm);
    printf("  RM@0x%08x (SM_SPEED):   0x%08x\n",
           FUSE_FEATURE_OVERRIDE_SS_OFFSET, fuse_ss_rm);
    printf("  RM@0x%08x (SM_SPEED_1): 0x%08x\n",
           FUSE_FEATURE_OVERRIDE_SS1_OFFSET, fuse_ss1_rm);
    printf("  RM@0x%08x (REMAP):      0x%08x\n",
           FUSE_FEATURE_OVERRIDE_REMAP_OFFSET, fuse_remap_rm);

    if (fuse_plm_rm != 0xFFFFFFFFu)
        decode_plm("FUSE_PLM(RM)", fuse_plm_rm);
    if (fuse_ss_rm != 0xFFFFFFFFu) {
        printf("  --- FUSE SS decode (RM) ---\n");
        decode_fuse_ss(fuse_ss_rm);
    }
    if (fuse_ss1_rm != 0xFFFFFFFFu)
        decode_fuse_ss1(fuse_ss1_rm);

    if (bar0.map) {
        NvU32 fuse_plm_b0 = bar0_read32(&bar0, FUSE_FEATURE_OVERRIDE_PLM_OFFSET);
        printf("\n  BAR0 vs RM match: PLM=%s SS=%s\n",
               (fuse_plm_b0 == fuse_plm_rm) ? "YES" : "DIFFERS",
               (bar0_read32(&bar0, FUSE_FEATURE_OVERRIDE_SS_OFFSET) == fuse_ss_rm)
                   ? "YES" : "DIFFERS");
    }
    printf("\n");

    /* ====================================================================
     * STEP 2: Clear FUSE PRIV_LEVEL_MASK via RM
     * ==================================================================== */
    printf("=== STEP 2: Clear FUSE PRIV_LEVEL_MASK Write Protection ===\n");

    if (fuse_plm_rm == 0xFFFFFFFFu) {
        printf("  SKIP: FUSE PLM returns 0xFFFFFFFF (dead decode).\n\n");
    } else {
        NvU32 new_plm = fuse_plm_rm & ~(PLM_READ_PROTECTION_MASK | PLM_WRITE_PROTECTION_MASK);
        printf("  Current FUSE PLM: 0x%08x\n", fuse_plm_rm);
        printf("  New FUSE PLM:     0x%08x (all protection disabled)\n", new_plm);

        st = rm_reg_write(ctl, hClient, hSubdev,
                          FUSE_FEATURE_OVERRIDE_PLM_OFFSET, new_plm);
        if (st != 0) {
            printf("  RM write FAILED: status=0x%08x\n", st);
        } else {
            printf("  [OK] RM write returned status=0\n");
        }

        NvU32 plm_after = rm_reg_read(ctl, hClient, hSubdev,
                                       FUSE_FEATURE_OVERRIDE_PLM_OFFSET);
        decode_plm("FUSE PLM after write", plm_after);

        if (plm_after == new_plm) {
            printf("\n  *** FUSE PLM WRITE TOOK EFFECT! Protection disabled. ***\n");
        } else if (plm_after != fuse_plm_rm) {
            printf("\n  *** FUSE PLM partially changed: 0x%08x -> 0x%08x ***\n",
                   fuse_plm_rm, plm_after);
        } else {
            printf("\n  *** FUSE PLM UNCHANGED. Write ignored by hardware. ***\n");
        }

        /* Also try via BAR0 if available */
        if (bar0.map) {
            NvU32 plm_b0_before = bar0_read32(&bar0, FUSE_FEATURE_OVERRIDE_PLM_OFFSET);
            printf("\n  Also trying BAR0 direct write to FUSE PLM...\n");
            printf("  Before: 0x%08x\n", plm_b0_before);
            bar0_write32(&bar0, FUSE_FEATURE_OVERRIDE_PLM_OFFSET, PLM_ALL_DISABLED);
            usleep(10000);
            NvU32 plm_b0_after = bar0_read32(&bar0, FUSE_FEATURE_OVERRIDE_PLM_OFFSET);
            printf("  After:  0x%08x (%s)\n", plm_b0_after,
                   (plm_b0_after == PLM_ALL_DISABLED) ? "CLEARED" :
                   (plm_b0_after != plm_b0_before) ? "CHANGED" : "UNCHANGED");
        }
        printf("\n");
    }

    /* ====================================================================
     * STEP 3: Write SM_SPEED_SELECT with full speed value
     * ==================================================================== */
    printf("=== STEP 3: Write FUSE SM_SPEED_SELECT = Full Speed ===\n");

    if (fuse_ss_rm == 0xFFFFFFFFu) {
        printf("  SKIP: FUSE SS returns 0xFFFFFFFF (dead decode).\n\n");
    } else {
        /* Try 1-bit full speed first */
        printf("  Attempt 1: Write FUSE_SS_FULL_1BIT = 0x%08x\n", FUSE_SS_FULL_1BIT);
        st = rm_reg_write(ctl, hClient, hSubdev,
                          FUSE_FEATURE_OVERRIDE_SS_OFFSET, FUSE_SS_FULL_1BIT);
        printf("  RM write status=0x%08x\n", st);
        NvU32 ss_after = rm_reg_read(ctl, hClient, hSubdev,
                                      FUSE_FEATURE_OVERRIDE_SS_OFFSET);
        decode_fuse_ss(ss_after);

        if (ss_after == FUSE_SS_FULL_1BIT) {
            printf("\n  *** SUCCESS: FUSE SS cleared via 1-bit value! ***\n");
        } else if (ss_after != fuse_ss_rm) {
            printf("\n  *** PARTIAL: Changed 0x%08x -> 0x%08x ***\n", fuse_ss_rm, ss_after);
        } else {
            printf("\n  *** UNCHANGED. Trying 3-bit encoding... ***\n\n");

            /* Try 3-bit encoding */
            printf("  Attempt 2: Write FUSE_SS_FULL_3BIT = 0x%08x\n", FUSE_SS_FULL_3BIT);
            st = rm_reg_write(ctl, hClient, hSubdev,
                              FUSE_FEATURE_OVERRIDE_SS_OFFSET, FUSE_SS_FULL_3BIT);
            printf("  RM write status=0x%08x\n", st);
            ss_after = rm_reg_read(ctl, hClient, hSubdev,
                                    FUSE_FEATURE_OVERRIDE_SS_OFFSET);
            decode_fuse_ss(ss_after);

            if (ss_after == FUSE_SS_FULL_3BIT) {
                printf("\n  *** SUCCESS: FUSE SS using 3-bit encoding! ***\n");
            } else if (ss_after != fuse_ss_rm) {
                printf("\n  *** PARTIAL: Changed 0x%08x -> 0x%08x ***\n", fuse_ss_rm, ss_after);
            } else {
                printf("\n  *** FUSE SS UNCHANGED by either encoding. ***\n");
            }
        }

        /* Try BAR0 direct write if available */
        if (bar0.map) {
            NvU32 ss_b0_before = bar0_read32(&bar0, FUSE_FEATURE_OVERRIDE_SS_OFFSET);
            printf("\n  Also trying BAR0 direct write to FUSE SS...\n");
            printf("  Before: 0x%08x\n", ss_b0_before);
            bar0_write32(&bar0, FUSE_FEATURE_OVERRIDE_SS_OFFSET, FUSE_SS_FULL_1BIT);
            usleep(10000);
            NvU32 ss_b0_after = bar0_read32(&bar0, FUSE_FEATURE_OVERRIDE_SS_OFFSET);
            printf("  After (1bit): 0x%08x (%s)\n", ss_b0_after,
                   (ss_b0_after == FUSE_SS_FULL_1BIT) ? "FULL SPEED" :
                   (ss_b0_after != ss_b0_before) ? "CHANGED" : "UNCHANGED");
        }
        printf("\n");
    }

    /* ====================================================================
     * STEP 4: Write SM_SPEED_SELECT_1 (IMLA4)
     * ==================================================================== */
    printf("=== STEP 4: Write FUSE SM_SPEED_SELECT_1 = Full Speed ===\n");

    if (fuse_ss1_rm == 0xFFFFFFFFu) {
        printf("  SKIP: FUSE SS1 returns 0xFFFFFFFF (dead decode).\n\n");
    } else {
        printf("  Writing 0x%08x to 0x%08x\n",
               FUSE_SS1_FULL, FUSE_FEATURE_OVERRIDE_SS1_OFFSET);
        st = rm_reg_write(ctl, hClient, hSubdev,
                          FUSE_FEATURE_OVERRIDE_SS1_OFFSET, FUSE_SS1_FULL);
        printf("  RM write status=0x%08x\n", st);
        NvU32 ss1_after = rm_reg_read(ctl, hClient, hSubdev,
                                       FUSE_FEATURE_OVERRIDE_SS1_OFFSET);
        decode_fuse_ss1(ss1_after);

        if (ss1_after == FUSE_SS1_FULL) {
            printf("\n  *** SUCCESS: FUSE SS1 cleared! ***\n");
        } else if (ss1_after != fuse_ss1_rm) {
            printf("\n  *** PARTIAL: Changed 0x%08x -> 0x%08x ***\n",
                   fuse_ss1_rm, ss1_after);
        } else {
            printf("\n  *** FUSE SS1 UNCHANGED. ***\n");
        }
        printf("\n");
    }

    /* ====================================================================
     * STEP 5: Verify FECS SM_SPEED_SELECT post-FUSE write
     *         (check if FUSE override propagates to FECS)
     * ==================================================================== */
    printf("=== STEP 5: Check FECS SM_SPEED_SELECT After FUSE Write ===\n");
    NvU32 fecs_ss = rm_reg_read(ctl, hClient, hSubdev, 0x00409664u);
    printf("  FECS SS(0x409664) = 0x%08x\n", fecs_ss);
    if (fecs_ss == 0xFFFFFFFFu) {
        printf("  (FECS region still dead on this GPU)\n");
    } else {
        printf("    IMLA=%u IMLA_OVR=%u FMLA=%u FMLA_OVR=%u DP=%u DP_OVR=%u\n",
               (fecs_ss >> 0) & 1u, (fecs_ss >> 3) & 1u,
               (fecs_ss >> 4) & 1u, (fecs_ss >> 7) & 1u,
               (fecs_ss >> 8) & 1u, (fecs_ss >> 11) & 1u);
        if (fecs_ss == 0)
            printf("  *** FECS SS IS FULL SPEED! ***\n");
    }
    printf("\n");

    /* ====================================================================
     * Summary
     * ==================================================================== */
    printf("=== Summary ===\n");
    printf("  FUSE controller path: ");
    if (fuse_plm_rm == 0xFFFFFFFFu && fuse_ss_rm == 0xFFFFFFFFu) {
        printf("DEAD (all 0xFFFFFFFF)\n");
    } else {
        printf("ALIVE\n");
        printf("  FUSE PLM: 0x%08x\n", fuse_plm_rm);
        printf("  FUSE SS:  0x%08x\n", fuse_ss_rm);
        printf("  FUSE SS1: 0x%08x\n", fuse_ss1_rm);
    }
    printf("  FECS SS(0x409664): 0x%08x\n", fecs_ss);
    printf("\n");

    /* Cleanup */
    bar0_close(&bar0);
    close(devfd);
    close(ctl);

    return 0;
}
