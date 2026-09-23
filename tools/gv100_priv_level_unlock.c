/*
 * gv100_priv_level_unlock.c
 *
 * GV100 PRIV_LEVEL_MASK unlock test for FECS SM speed-select override.
 *
 * Background:
 *   The FECS FEATURE_OVERRIDE_SM_SPEED_SELECT register at 0x409664 is
 *   silicon-level read-only on CMP SKUs (0x1d84). Direct writes produce
 *   no bit changes, even through /dev/mem BAR0 MMIO.
 *
 * Hypothesis:
 *   The PRIV_LEVEL_MASK register at 0x409650 controls write protection for
 *   the entire FECS FEATURE_OVERRIDE region (0x409650-0x40967x). If this
 *   register has WRITE_PROTECTION[6:4] enabled (bits 4,5,6 set = 0x70),
 *   writes to sub-registers like SM_SPEED_SELECT at 0x409664 are silently
 *   discarded.
 *
 *   Clearing WRITE_PROTECTION[6:4]=0 and READ_PROTECTION[2:0]=0 should
 *   allow subsequent writes to 0x409664 to take effect.
 *
 * GV100 register definitions (dev_graphics_nobundle.h):
 *
 *   NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK  0x00409650 (RW-4R)
 *     READ_PROTECTION[2:0]  - read access control per privilege level
 *     READ_VIOLATION[3]     - report read violations
 *     WRITE_PROTECTION[6:4] - write access control per privilege level
 *     WRITE_VIOLATION[7]    - report write violations
 *
 *   NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT  0x00409664 (RW-4R)
 *     __PRIV_LEVEL_MASK = 0x00000650  (cross-ref to the register above)
 *     IMLA[0], IMLA_OVERRIDE[3]
 *     FMLA[4], FMLA_OVERRIDE[7]
 *     DP[8],   DP_OVERRIDE[11]
 *
 * Usage:
 *   sudo ./gv100_priv_level_unlock [minor_number]
 *
 * Build:
 *   gcc -O2 -Wall -o gv100_priv_level_unlock gv100_priv_level_unlock.c
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

/* --- Register offsets (BAR0 absolute) --- */
#define FECS_FEATURE_OVERRIDE_PLM_OFFSET     0x00409650u
#define FECS_FEATURE_OVERRIDE_ECC_PLM_OFFSET 0x0040964cu
#define FECS_FEATURE_OVERRIDE_QUADRO_OFFSET  0x00409654u
#define FECS_FEATURE_OVERRIDE_ECC_OFFSET     0x00409658u
#define FECS_FEATURE_READOUT_OFFSET          0x00409660u
#define FECS_SM_SPEED_SELECT_OFFSET          0x00409664u
#define FECS_FEATURE_SECURITY_PLM_OFFSET     0x00409670u
#define FECS_FEATURE_SECURITY_LOCK_OFFSET    0x00409674u

/* PRIV_LEVEL_MASK bitfields */
#define PLM_READ_PROTECTION_MASK    0x00000007u  /* bits 2:0 */
#define PLM_READ_VIOLATION_MASK     0x00000008u  /* bit 3 */
#define PLM_WRITE_PROTECTION_MASK   0x00000070u  /* bits 6:4 */
#define PLM_WRITE_VIOLATION_MASK    0x00000080u  /* bit 7 */
#define PLM_PROTECTION_ALL_DISABLED 0x00000000u

/* SM_SPEED_SELECT bitfields */
#define OVERRIDE_FULL_SPEED         0x00000000u
#define OVERRIDE_REDUCED            0x00000999u

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

/* RM regop write, returns NV status */
static NvU32 rm_reg_write(int ctl, NvHandle hClient, NvHandle hObject,
                           NvU32 offset, NvU32 value, NvU8 *reg_status) {
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
    if (reg_status) *reg_status = op.regStatus;
    return st;
}

/* Decode SM_SPEED_SELECT */
static void decode_speed_select(NvU32 val) {
    unsigned imla          = (val >> 0) & 1u;
    unsigned imla_override = (val >> 3) & 1u;
    unsigned fmla          = (val >> 4) & 1u;
    unsigned fmla_override = (val >> 7) & 1u;
    unsigned dp            = (val >> 8) & 1u;
    unsigned dp_override   = (val >> 11) & 1u;

    printf("  SM_SPEED_SELECT = 0x%08x\n", val);
    printf("    IMLA[0]=%u IMLA_OVERRIDE[3]=%u  -> %s\n",
           imla, imla_override,
           (imla_override && imla) ? "REDUCED (1/16)" : "FULL");
    printf("    FMLA[4]=%u FMLA_OVERRIDE[7]=%u  -> %s\n",
           fmla, fmla_override,
           (fmla_override && fmla) ? "REDUCED (1/16)" : "FULL");
    printf("    DP[8]=%u   DP_OVERRIDE[11]=%u   -> %s\n",
           dp, dp_override,
           (dp_override && dp) ? "REDUCED (1/16)" : "FULL");
}

/* Decode PRIV_LEVEL_MASK */
static void decode_priv_level_mask(const char *name, NvU32 val) {
    NvU32 rp = val & PLM_READ_PROTECTION_MASK;
    NvU32 rv = (val >> 3) & 1u;
    NvU32 wp = (val >> 4) & 0x07u;
    NvU32 wv = (val >> 7) & 1u;
    printf("  %s = 0x%08x\n", name, val);
    printf("    READ_PROTECTION[2:0]=0x%x (L0=%c L1=%c L2=%c)\n",
           rp,
           (rp & 1) ? 'E' : 'D',
           (rp & 2) ? 'E' : 'D',
           (rp & 4) ? 'E' : 'D');
    printf("    READ_VIOLATION[3]=%u (%s)\n", rv,
           rv ? "REPORT_ERROR" : "SOLDIER_ON");
    printf("    WRITE_PROTECTION[6:4]=0x%x (L0=%c L1=%c L2=%c)\n",
           wp,
           (wp & 1) ? 'E' : 'D',
           (wp & 2) ? 'E' : 'D',
           (wp & 4) ? 'E' : 'D');
    printf("    WRITE_VIOLATION[7]=%u (%s)\n", wv,
           wv ? "REPORT_ERROR" : "SOLDIER_ON");
}

/* --- BAR0 /dev/mem direct access --- */
typedef struct {
    int      fd;
    void    *map;
    size_t   map_size;
    NvU64    bar0_base;
} bar0_handle_t;

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

/* ===== main ===== */
int main(int argc, char **argv) {
    int target_minor = 14;
    if (argc > 1) target_minor = atoi(argv[1]);

    if (geteuid() != 0) {
        fprintf(stderr, "ERROR: must be run as root\n");
        return 1;
    }

    printf("=== GV100 PRIV_LEVEL_MASK Unlock Tool ===\n");
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
            printf("  card[%d]: minor=%u gpu_id=0x%08x pci=%04x:%02x:%02x.%u dev=0x%04x BAR0=0x%016lx+0x%lx\n",
                   i, cards[i].minor_number, cards[i].gpu_id,
                   cards[i].pci_info.domain, cards[i].pci_info.bus,
                   cards[i].pci_info.slot, cards[i].pci_info.function,
                   cards[i].pci_info.device_id,
                   cards[i].reg_address, cards[i].reg_size);
            if ((int)cards[i].minor_number == target_minor) {
                card_index = i;
            }
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

    /* Map gpu_id to deviceInstance */
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

    /* Register dev fd */
    char devpath[64];
    snprintf(devpath, sizeof(devpath), "/dev/nvidia%d", target_minor);
    int devfd = open(devpath, O_RDWR | O_CLOEXEC);
    if (devfd < 0) { perror("open /dev/nvidiaN"); return 6; }
    nv_ioctl_register_fd_t reg;
    memset(&reg, 0, sizeof(reg));
    reg.ctl_fd = ctl;
    rm_ioctl(devfd, NV_ESC_REGISTER_FD, &reg, sizeof(reg));
    printf("[OK] Registered ctl fd on %s\n", devpath);

    /* Alloc device */
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

    /* Alloc subdevice */
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
     * STEP 1: Dump full FECS FEATURE_OVERRIDE region state
     * ==================================================================== */
    printf("=== STEP 1: FECS FEATURE_OVERRIDE Region Dump ===\n");
    NvU8 rs;
    NvU32 plm_rm, eplm_rm, quad_rm, ecc_rm, ro_rm, ss_rm, splm_rm, slk_rm;

    plm_rm  = rm_reg_read(ctl, hClient, hSubdev, FECS_FEATURE_OVERRIDE_PLM_OFFSET, NULL);
    eplm_rm = rm_reg_read(ctl, hClient, hSubdev, FECS_FEATURE_OVERRIDE_ECC_PLM_OFFSET, NULL);
    quad_rm = rm_reg_read(ctl, hClient, hSubdev, FECS_FEATURE_OVERRIDE_QUADRO_OFFSET, NULL);
    ecc_rm  = rm_reg_read(ctl, hClient, hSubdev, FECS_FEATURE_OVERRIDE_ECC_OFFSET, NULL);
    ro_rm   = rm_reg_read(ctl, hClient, hSubdev, FECS_FEATURE_READOUT_OFFSET, NULL);
    ss_rm   = rm_reg_read(ctl, hClient, hSubdev, FECS_SM_SPEED_SELECT_OFFSET, NULL);
    splm_rm = rm_reg_read(ctl, hClient, hSubdev, FECS_FEATURE_SECURITY_PLM_OFFSET, NULL);
    slk_rm  = rm_reg_read(ctl, hClient, hSubdev, FECS_FEATURE_SECURITY_LOCK_OFFSET, NULL);

    printf("  Via RM:\n");
    decode_priv_level_mask("PLM(0x409650)", plm_rm);
    decode_priv_level_mask("ECC_PLM(0x40964c)", eplm_rm);
    printf("  QUADRO(0x409654)     = 0x%08x\n", quad_rm);
    printf("  ECC(0x409658)        = 0x%08x\n", ecc_rm);
    printf("  READOUT(0x409660)    = 0x%08x\n", ro_rm);
    decode_speed_select(ss_rm);
    decode_priv_level_mask("SEC_PLM(0x409670)", splm_rm);
    printf("  SEC_LOCK(0x409674)   = 0x%08x\n", slk_rm);

    if (bar0.map) {
        NvU32 plm_bar0  = bar0_read32(&bar0, FECS_FEATURE_OVERRIDE_PLM_OFFSET);
        NvU32 ss_bar0   = bar0_read32(&bar0, FECS_SM_SPEED_SELECT_OFFSET);
        printf("\n  Via BAR0 /dev/mem:\n");
        printf("  PLM(0x409650)        = 0x%08x\n", plm_bar0);
        printf("  SM_SPEED_SELECT(0x409664) = 0x%08x\n", ss_bar0);
        printf("  (Match: PLM %s, SM_SPEED %s)\n",
               (plm_bar0 == plm_rm) ? "YES" : "DIFFERS",
               (ss_bar0 == ss_rm) ? "YES" : "DIFFERS");
    }
    printf("\n");

    /* ====================================================================
     * STEP 2: Write PRIV_LEVEL_MASK via RM to disable write protection
     * ==================================================================== */
    printf("=== STEP 2: Clear PRIV_LEVEL_MASK Write Protection via RM ===\n");

    /* Compute new PLM value: clear all protection bits, preserve violations */
    NvU32 new_plm = plm_rm & ~(PLM_READ_PROTECTION_MASK | PLM_WRITE_PROTECTION_MASK);
    printf("  Current PLM:  0x%08x\n", plm_rm);
    printf("  New PLM:      0x%08x (all protection disabled)\n", new_plm);

    st = rm_reg_write(ctl, hClient, hSubdev,
                      FECS_FEATURE_OVERRIDE_PLM_OFFSET, new_plm, &rs);
    if (st != 0) {
        printf("  FAILED: RM status=0x%08x regStatus=0x%02x\n", st, rs);
    } else {
        printf("  [OK] Write returned status=0 regStatus=0x%02x\n", rs);
    }

    /* Read back PLM */
    NvU32 plm_after = rm_reg_read(ctl, hClient, hSubdev,
                                   FECS_FEATURE_OVERRIDE_PLM_OFFSET, NULL);
    decode_priv_level_mask("PLM after write", plm_after);

    if (plm_after == new_plm) {
        printf("\n  *** PLM write TOOK EFFECT! Protection disabled. ***\n");
    } else {
        printf("\n  *** PLM write IGNORED by hardware (value unchanged). ***\n");
    }
    printf("\n");

    /* Also try clearing ECC_PLM */
    printf("--- Also clearing ECC_PLM at 0x40964c ---\n");
    NvU32 new_eplm = eplm_rm & ~(PLM_READ_PROTECTION_MASK | PLM_WRITE_PROTECTION_MASK);
    printf("  Current ECC_PLM: 0x%08x -> New: 0x%08x\n", eplm_rm, new_eplm);
    st = rm_reg_write(ctl, hClient, hSubdev,
                      FECS_FEATURE_OVERRIDE_ECC_PLM_OFFSET, new_eplm, &rs);
    if (st != 0) {
        printf("  FAILED: RM status=0x%08x regStatus=0x%02x\n", st, rs);
    } else {
        NvU32 eplm_after = rm_reg_read(ctl, hClient, hSubdev,
                                        FECS_FEATURE_OVERRIDE_ECC_PLM_OFFSET, NULL);
        printf("  After write: 0x%08x (%s)\n", eplm_after,
               (eplm_after == new_eplm) ? "CHANGED" : "UNCHANGED");
    }
    printf("\n");

    /* ====================================================================
     * STEP 3: Write SM_SPEED_SELECT with full speed (0x0) via RM
     * ==================================================================== */
    printf("=== STEP 3: Write SM_SPEED_SELECT = 0x00000000 (full speed) via RM ===\n");
    st = rm_reg_write(ctl, hClient, hSubdev,
                      FECS_SM_SPEED_SELECT_OFFSET, OVERRIDE_FULL_SPEED, &rs);
    printf("  RM write status=0x%08x regStatus=0x%02x\n", st, rs);

    NvU32 ss_after_rm = rm_reg_read(ctl, hClient, hSubdev,
                                     FECS_SM_SPEED_SELECT_OFFSET, NULL);
    decode_speed_select(ss_after_rm);

    if (ss_after_rm == OVERRIDE_FULL_SPEED) {
        printf("\n  *** SUCCESS: SM speed-select overrides cleared via RM! ***\n");
    } else if (ss_after_rm != ss_rm) {
        printf("\n  *** PARTIAL: Value changed from 0x%08x to 0x%08x ***\n", ss_rm, ss_after_rm);
    } else {
        printf("\n  *** UNCHANGED (still 0x%08x). RM path ineffective. ***\n", ss_after_rm);
    }
    printf("\n");

    /* ====================================================================
     * STEP 4: Try via BAR0 /dev/mem direct path
     * ==================================================================== */
    if (bar0.map) {
        printf("=== STEP 4: Direct BAR0 /dev/mem PRIV_LEVEL_MASK write ===\n");

        /* 4a: Read current PLM via BAR0 */
        NvU32 plm_direct = bar0_read32(&bar0, FECS_FEATURE_OVERRIDE_PLM_OFFSET);
        NvU32 ss_direct  = bar0_read32(&bar0, FECS_SM_SPEED_SELECT_OFFSET);
        printf("  Before: PLM=0x%08x SS=0x%08x\n", plm_direct, ss_direct);

        /* 4b: Hammer PLM with 0 to clear all protection */
        printf("  Writing PLM=0x00000000 via /dev/mem...\n");
        bar0_write32(&bar0, FECS_FEATURE_OVERRIDE_PLM_OFFSET, 0x00000000u);
        usleep(10000);

        NvU32 plm_direct_after = bar0_read32(&bar0, FECS_FEATURE_OVERRIDE_PLM_OFFSET);
        printf("  PLM after write: 0x%08x (%s)\n", plm_direct_after,
               (plm_direct_after == 0) ? "CLEARED" :
               (plm_direct_after != plm_direct) ? "CHANGED" : "UNCHANGED");

        /* If PLM write failed, try the ECC_PLM too */
        if (plm_direct_after == plm_direct) {
            printf("\n  Master PLM was read-only. Trying ECC_PLM at 0x40964c...\n");
            NvU32 eplm_direct = bar0_read32(&bar0, FECS_FEATURE_OVERRIDE_ECC_PLM_OFFSET);
            printf("  ECC_PLM before: 0x%08x\n", eplm_direct);
            bar0_write32(&bar0, FECS_FEATURE_OVERRIDE_ECC_PLM_OFFSET, 0x00000000u);
            usleep(10000);
            NvU32 eplm_direct_after2 = bar0_read32(&bar0, FECS_FEATURE_OVERRIDE_ECC_PLM_OFFSET);
            printf("  ECC_PLM after: 0x%08x (%s)\n", eplm_direct_after2,
                   (eplm_direct_after2 == 0) ? "CLEARED" :
                   (eplm_direct_after2 != eplm_direct) ? "CHANGED" : "UNCHANGED");
        }

        /* 4c: Now write SM_SPEED_SELECT */
        printf("\n  Writing SM_SPEED_SELECT=0x00000000 via /dev/mem...\n");
        bar0_write32(&bar0, FECS_SM_SPEED_SELECT_OFFSET, OVERRIDE_FULL_SPEED);
        usleep(10000);

        NvU32 ss_direct_after = bar0_read32(&bar0, FECS_SM_SPEED_SELECT_OFFSET);
        printf("  SM_SPEED_SELECT after: 0x%08x (%s)\n", ss_direct_after,
               (ss_direct_after == OVERRIDE_FULL_SPEED) ? "FULL SPEED!" :
               (ss_direct_after != ss_direct) ? "CHANGED" : "UNCHANGED");
        decode_speed_select(ss_direct_after);

        if (ss_direct_after == OVERRIDE_FULL_SPEED) {
            printf("\n  *** SUCCESS: SM speed-select overrides cleared via /dev/mem! ***\n");
        } else if (ss_direct_after != ss_direct) {
            printf("\n  *** PARTIAL: Value partially changed via /dev/mem ***\n");
        } else {
            printf("\n  *** UNCHANGED. Hardware is blocking writes at silicon level. ***\n");
        }
        printf("\n");
    }

    /* ====================================================================
     * STEP 5: Try a different approach — write to FUSE override registers
     *         These are in the FUSE controller address space (BAR1 typically)
     * ==================================================================== */
    printf("=== STEP 5: Check FUSE-level SM speed-select overrides (if accessible) ===\n");
    printf("  GV100 FUSE definitions (dev_fuse.h, offsets are FUSE-relative):\n");
    printf("    NV_FUSE_OPT_SM_IMLA_SPEED_SELECT  @ 0x00021410\n");
    printf("    NV_FUSE_OPT_SM_FMLA_SPEED_SELECT  @ 0x000214E0\n");
    printf("    NV_FUSE_OPT_DP_SPEED_SELECT       @ 0x00021224\n");
    printf("  These are in FUSE controller space (BAR1-based), not tested here.\n");
    printf("  Need fuse controller base address from PMC or BAR1 mapping.\n\n");

    /* ====================================================================
     * STEP 6: Try writing to FECS_CFG1, FECS_CFG2 to see if they affect behavior
     * ==================================================================== */
    printf("=== STEP 6: Probe adjacent FECS control registers ===\n");
    /* FECS_CFG1 at 0x409614 was previously found partially writable */
    NvU32 cfg1 = rm_reg_read(ctl, hClient, hSubdev, 0x00409614, NULL);
    printf("  FECS_CFG1(0x409614)  = 0x%08x\n", cfg1);
    NvU32 cfg2 = rm_reg_read(ctl, hClient, hSubdev, 0x00409644, NULL);
    printf("  FECS_CFG2(0x409644)  = 0x%08x\n", cfg2);

    printf("\n=== Summary ===\n");
    printf("  PRIV_LEVEL_MASK path: %s\n",
           (plm_after == new_plm) ? "PLM IS WRITABLE" : "PLM IS READ-ONLY");
    printf("  SM_SPEED_SELECT @ 0x409664: 0x%08x\n", ss_after_rm);
    printf("\n");

    /* Cleanup */
    bar0_close(&bar0);
    close(devfd);
    close(ctl);

    return 0;
}
