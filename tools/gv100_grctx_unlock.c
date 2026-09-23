/*
 * gv100_grctx_unlock.c
 *
 * NV2080_CTRL_CMD_GPU_EXEC_REG_OPS write path for GV100
 * GR context SM speed-select override register (ctxsw offset 0x00019900).
 *
 * Code-path analysis conclusions (from nvdrv source):
 *
 *   Path — NV2080_CTRL_CMD_GPU_EXEC_REG_OPS with GR_CTX type (0x01):
 *     1. nv2080CtrlCmdGpuExecRegOps  (gpuctrl.c:5351)
 *     2. gpuCtrlExecRegOps           (gpuctrl.c:5285)
 *        -> hClientTarget==0 -> chid=INVALID_CHID
 *     3. gpuExecRegOpsNonTransaction (gpuctrl.c:2979)
 *        a) gpuExecGlobalRegops      (gpuctrl.c:2423) - handles GLOBAL type ops
 *        b) gpuAddGrCtxOverrides     (gpuctrl.c:2604) - adds GR_CTX ops to
 *           pGr->pContextInitOverrides[] with GRCTXOVERRIDE(offset)
 *           = 0x10000000 | offset (applied during context init by ctxsw fw)
 *        c) gpuExecGrCtxRegops_HAL   (gpugf100.c:1367)
 *           = gpuExecGrCtxRegops_GK104
 *           With INVALID_CHID:
 *             i.   Reads via _gpuExecGrCtxRegopsBar0 (BAR0 PRI addr)
 *                  -> GPU_REG_RD32(pGpu, pRegOp->regOffset)
 *                  -> Since 0x00019900 is not FECS, returns PMC junk
 *             ii.  Writes: iterates ALL channels
 *                  - Resident channels: _gpuExecGrCtxRegopsBar0 BAR0 write
 *                    -> GPU_REG_WR32(pGpu, pRegOp->regOffset, data32)
 *                    -> Writes to PRI 0x00019900 (PMC register, NOT ctxsw!)
 *                  - Non-resident channels: _gpuExecGrCtxRegopsCtxswBuffer
 *                    -> grGetCtxBufferOffsets_HAL maps offset to ctxsw buffer
 *                    -> MEM_WR32(baseAddr + pOffsets[j], data32)
 *                    -> Correctly writes to ctxsw buffer offset 0x00019900
 *
 *   Key insight: The override table path (b) is the most important.
 *   It stores GRCTXOVERRIDE(0x00019900) = 0x10019900 in the per-GR
 *   context init override list. When any CUDA context is initialized,
 *   the ctxsw firmware reads from this override list and applies
 *   the SM_SPEED_SELECT override BEFORE context execution starts.
 *
 *   The BAR0 write for resident channels (ii) goes to PRI 0x00019900
 *   which is NOT the ctxsw buffer — but resident channels already have
 *   their context loaded so this is harmless. The non-resident channel
 *   path (ii) correctly writes to the ctxsw buffer in memory.
 *
 * Build:
 *   gcc -O2 -Wall -o gv100_grctx_unlock gv100_grctx_unlock.c
 *
 * Usage (MUST be root):
 *   sudo ./gv100_grctx_unlock [minor_number]
 *
 * The ctxsw header offset at 0x00019900 is
 * NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT.
 * Current value 0x00000999 means:
 *   IMLA[0]=1, IMLA_OVERRIDE[3]=1  -> 1/16 issue rate for IMLA
 *   FMLA[4]=1, FMLA_OVERRIDE[7]=1  -> 1/16 issue rate for FMLA
 *   DP[8]=1,   DP_OVERRIDE[11]=1   -> 1/16 issue rate for DP
 *
 * Writing 0x00000000 clears ALL overrides -> full issue rate for all ops.
 * The override takes effect on the NEXT context initialization.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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

/* Register operation constants */
#define NV2080_CTRL_GPU_REG_OP_READ_32     0x00000000
#define NV2080_CTRL_GPU_REG_OP_WRITE_32    0x00000001
#define NV2080_CTRL_GPU_REG_OP_TYPE_GLOBAL 0x00000000u
#define NV2080_CTRL_GPU_REG_OP_TYPE_GR_CTX 0x00000001u

/* The FECS SM speed-select override register (PRI offset) */
#define FECS_SM_SPEED_SELECT_OFFSET 0x00409664u

/* The ctxsw firmware header SM speed-select override offset
 * (ctxsw buffer offset, NOT a PRI address) */
#define CTXSW_SM_SPEED_SELECT_OFFSET 0x00019900u

/* Value that clears ALL overrides -> full issue rate */
#define OVERRIDE_CLEAR_VALUE 0x00000000u

/* --- data structures (mirror nvidia kernel structs) --- */

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

/* Execute a single regop via EXEC_REG_OPS (can be any regType) */
static NvU32 exec_reg_op(int ctl, NvHandle hClient, NvHandle hObject,
                          NV2080_CTRL_GPU_REG_OP *op) {
    NV2080_CTRL_GPU_EXEC_REG_OPS_PARAMS p;
    memset(&p, 0, sizeof(p));
    p.bNonTransactional = 0;  /* hClientTarget=0 forces non-transactional anyway */
    p.regOpCount = 1;
    p.regOps     = (NvU64)(uintptr_t)op;
    NvU32 st = rm_control(ctl, hClient, hObject,
                          NV2080_CTRL_CMD_GPU_EXEC_REG_OPS, &p, sizeof(p));
    return st;
}

/* Read register via GLOBAL type exec_reg_ops */
static NvU32 exec_global_read32(int ctl, NvHandle hClient, NvHandle hObject,
                                 NvU32 offset) {
    NV2080_CTRL_GPU_REG_OP op;
    memset(&op, 0, sizeof(op));
    op.regOp     = NV2080_CTRL_GPU_REG_OP_READ_32;
    op.regType   = NV2080_CTRL_GPU_REG_OP_TYPE_GLOBAL;
    op.regOffset = offset;
    NvU32 st = exec_reg_op(ctl, hClient, hObject, &op);
    if (st != 0) {
        fprintf(stderr, "  global READ 0x%08x -> status=0x%08x regStatus=0x%02x\n",
                offset, st, op.regStatus);
        return 0xFFFFFFFFu;
    }
    return op.regValueLo;
}

/* Write register via GLOBAL type exec_reg_ops */
static NvU32 exec_global_write32(int ctl, NvHandle hClient, NvHandle hObject,
                                  NvU32 offset, NvU32 value) {
    NV2080_CTRL_GPU_REG_OP op;
    memset(&op, 0, sizeof(op));
    op.regOp       = NV2080_CTRL_GPU_REG_OP_WRITE_32;
    op.regType     = NV2080_CTRL_GPU_REG_OP_TYPE_GLOBAL;
    op.regOffset   = offset;
    op.regValueLo  = value;
    op.regAndNMaskLo = 0xFFFFFFFFu;
    NvU32 st = exec_reg_op(ctl, hClient, hObject, &op);
    if (st != 0) {
        fprintf(stderr, "  global WRITE 0x%08x <- 0x%08x -> status=0x%08x regStatus=0x%02x\n",
                offset, value, st, op.regStatus);
    } else {
        printf("  [OK] global WRITE 0x%08x <- 0x%08x\n", offset, value);
    }
    return st;
}

/* Read register via GR_CTX type exec_reg_ops (reads from BAR0 PRI offset) */
static NvU32 exec_grctx_read32(int ctl, NvHandle hClient, NvHandle hObject,
                                NvU32 offset) {
    NV2080_CTRL_GPU_REG_OP op;
    memset(&op, 0, sizeof(op));
    op.regOp     = NV2080_CTRL_GPU_REG_OP_READ_32;
    op.regType   = NV2080_CTRL_GPU_REG_OP_TYPE_GR_CTX;
    op.regOffset = offset;
    NvU32 st = exec_reg_op(ctl, hClient, hObject, &op);
    if (st != 0) {
        fprintf(stderr, "  grctx READ  0x%08x -> status=0x%08x regStatus=0x%02x\n",
                offset, st, op.regStatus);
        return 0xFFFFFFFFu;
    }
    return op.regValueLo;
}

/* Write register via GR_CTX type exec_reg_ops
 * For INVALID_CHID (hClientTarget=0):
 *   - Adds to override table (GRCTXOVERRIDE(offset) = 0x10000000|offset)
 *   - Writes to BAR0 at PRI offset for resident channels
 *   - Writes to ctxsw buffer for non-resident channels */
static NvU32 exec_grctx_write32(int ctl, NvHandle hClient, NvHandle hObject,
                                 NvU32 offset, NvU32 value) {
    NV2080_CTRL_GPU_REG_OP op;
    memset(&op, 0, sizeof(op));
    op.regOp       = NV2080_CTRL_GPU_REG_OP_WRITE_32;
    op.regType     = NV2080_CTRL_GPU_REG_OP_TYPE_GR_CTX;
    op.regOffset   = offset;
    op.regValueLo  = value;
    op.regAndNMaskLo = 0xFFFFFFFFu;
    NvU32 st = exec_reg_op(ctl, hClient, hObject, &op);
    if (st != 0) {
        fprintf(stderr, "  grctx WRITE 0x%08x <- 0x%08x -> status=0x%08x regStatus=0x%02x\n",
                offset, value, st, op.regStatus);
    } else {
        printf("  [OK] grctx WRITE 0x%08x <- 0x%08x\n", offset, value);
    }
    return st;
}

/* Decode the SM_SPEED_SELECT ctxsw firmware header register (same layout as FECS) */
static void decode_speed_select(NvU32 val, const char *label) {
    unsigned imla          = (val >> 0) & 1u;
    unsigned imla_override = (val >> 3) & 1u;
    unsigned fmla          = (val >> 4) & 1u;
    unsigned fmla_override = (val >> 7) & 1u;
    unsigned dp            = (val >> 8) & 1u;
    unsigned dp_override   = (val >> 11) & 1u;

    printf("  %s = 0x%08x\n", label, val);
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

int main(int argc, char **argv) {
    int target_minor = 14;
    if (argc > 1) target_minor = atoi(argv[1]);

    if (geteuid() != 0) {
        fprintf(stderr, "ERROR: must be run as root\n");
        return 1;
    }

    printf("=== GV100 GR Context SM Speed-Select Override Unlock Tool ===\n");
    printf("Target GPU minor: %d\n\n", target_minor);

    /* Open nvidia control device */
    int ctl = open("/dev/nvidiactl", O_RDWR | O_CLOEXEC);
    if (ctl < 0) {
        perror("open /dev/nvidiactl");
        return 1;
    }

    /* Enumerate cards */
    nv_ioctl_card_info_t cards[32];
    memset(cards, 0, sizeof(cards));
    if (rm_ioctl(ctl, NV_ESC_CARD_INFO, cards, sizeof(cards)) < 0) {
        perror("ioctl CARD_INFO");
        return 1;
    }

    int card_index = -1;
    for (int i = 0; i < 32; i++) {
        if (cards[i].valid) {
            printf("Found card[%d]: minor=%u gpu_id=0x%08x pci=%04x:%02x:%02x.%u device=0x%04x\n",
                   i, cards[i].minor_number, cards[i].gpu_id,
                   cards[i].pci_info.domain, cards[i].pci_info.bus,
                   cards[i].pci_info.slot, cards[i].pci_info.function,
                   cards[i].pci_info.device_id);
            if ((int)cards[i].minor_number == target_minor)
                card_index = i;
        }
    }
    if (card_index < 0) {
        fprintf(stderr, "ERROR: target minor %d not found\n", target_minor);
        return 2;
    }
    printf("\n");

    /* Allocate root client */
    NVOS21_PARAMETERS root;
    memset(&root, 0, sizeof(root));
    root.hClass = NV01_ROOT;
    NvU32 st = rm_alloc21(ctl, &root);
    if (st != 0) {
        fprintf(stderr, "ERROR: alloc root failed: 0x%08x\n", st);
        return 3;
    }
    NvHandle hClient = root.hObjectNew;
    printf("[OK] Allocated root client: hClient=0x%08x\n", hClient);

    /* Get attached GPU IDs */
    NV0000_CTRL_GPU_GET_ATTACHED_IDS_PARAMS attached;
    memset(&attached, 0xff, sizeof(attached));
    st = rm_control(ctl, hClient, hClient,
                    NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS,
                    &attached, sizeof(attached));
    if (st != 0) {
        fprintf(stderr, "ERROR: get attached ids: 0x%08x\n", st);
        return 4;
    }

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
        printf("  gpuId=0x%08x devInst=%u subdev=%u\n",
               attached.gpuIds[i], id.deviceInstance, id.subDeviceInstance);
        if (attached.gpuIds[i] == cards[card_index].gpu_id)
            device_instance = id.deviceInstance;
    }
    if (device_instance == 0xffffffffu) {
        fprintf(stderr, "ERROR: cannot map gpu_id to device instance\n");
        return 5;
    }
    printf("[OK] Target device_instance=%u\n", device_instance);

    /* Register the dev fd with ctl */
    char devpath[64];
    snprintf(devpath, sizeof(devpath), "/dev/nvidia%d", target_minor);
    int devfd = open(devpath, O_RDWR | O_CLOEXEC);
    if (devfd < 0) {
        perror("open /dev/nvidiaN");
        return 6;
    }
    nv_ioctl_register_fd_t reg;
    memset(&reg, 0, sizeof(reg));
    reg.ctl_fd = ctl;
    if (rm_ioctl(devfd, NV_ESC_REGISTER_FD, &reg, sizeof(reg)) < 0) {
        fprintf(stderr, "WARNING: register ctl fd: %s\n", strerror(errno));
    } else {
        printf("[OK] Registered ctl fd on %s\n", devpath);
    }

    /* Allocate device object */
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
    if (st != 0) {
        fprintf(stderr, "ERROR: alloc device: 0x%08x\n", st);
        return 7;
    }
    printf("[OK] Allocated device: hDevice=0x%08x\n", hDevice);

    /* Allocate subdevice object */
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
    if (st != 0) {
        fprintf(stderr, "ERROR: alloc subdevice: 0x%08x\n", st);
        return 8;
    }
    printf("[OK] Allocated subdevice: hSubdev=0x%08x\n\n", hSubdev);

    /* ==================================================================== */
    /* STEP 1: Read current FECS SM_SPEED_SELECT register (GLOBAL type)    */
    /* ==================================================================== */
    printf("=== Step 1: Read current FECS SM_SPEED_SELECT (GLOBAL type) ===\n");
    NvU32 fecs_before = exec_global_read32(ctl, hClient, hSubdev,
                                            FECS_SM_SPEED_SELECT_OFFSET);
    if (fecs_before == 0xFFFFFFFFu) {
        printf("  WARNING: FECS read returned 0xFFFFFFFF (dead decode on CMP?)\n");
    } else {
        decode_speed_select(fecs_before, "FECS SM_SPEED_SELECT");
    }
    printf("\n");

    /* ==================================================================== */
    /* STEP 2: Attempt GR_CTX READ of ctxsw header offset (reads BAR0 PRI) */
    /* ==================================================================== */
    printf("=== Step 2: GR_CTX READ at 0x%08x (reads BAR0 PRI offset) ===\n",
           CTXSW_SM_SPEED_SELECT_OFFSET);
    NvU32 grctx_bar0_val = exec_grctx_read32(ctl, hClient, hSubdev,
                                              CTXSW_SM_SPEED_SELECT_OFFSET);
    if (grctx_bar0_val == 0xFFFFFFFFu) {
        printf("  NOTE: GR_CTX read returned 0xFFFFFFFF (BAR0 PRI at 0x%08x may not be mapped)\n",
               CTXSW_SM_SPEED_SELECT_OFFSET);
    } else {
        printf("  BAR0 PRI read at 0x%08x = 0x%08x (this is NOT ctxsw, just PRI bus)\n",
               CTXSW_SM_SPEED_SELECT_OFFSET, grctx_bar0_val);
    }
    printf("\n");

    /* ==================================================================== */
    /* STEP 3: GR_CTX WRITE to ctxsw offset 0x00019900                      */
    /*         This writes to override table AND ctxsw buffers for           */
    /*         non-resident channels. Resident channels get BAR0 write.      */
    /* ==================================================================== */
    printf("=== Step 3: GR_CTX WRITE at 0x%08x <- 0x%08x ===\n",
           CTXSW_SM_SPEED_SELECT_OFFSET, OVERRIDE_CLEAR_VALUE);
    printf("  Mechanism:\n");
    printf("    a) gpuAddGrCtxOverrides stores GRCTXOVERRIDE(0x%08x)\n",
           CTXSW_SM_SPEED_SELECT_OFFSET);
    printf("       = 0x10000000|0x%08x = 0x100%04x in override list\n",
           CTXSW_SM_SPEED_SELECT_OFFSET, CTXSW_SM_SPEED_SELECT_OFFSET);
    printf("       -> Applied during NEXT context init by ctxsw firmware\n");
    printf("    b) Non-resident channels: writes to ctxsw buffer at offset 0x%08x\n",
           CTXSW_SM_SPEED_SELECT_OFFSET);
    printf("    c) Resident channels: writes to BAR0 PRI 0x%08x (harmless)\n\n",
           CTXSW_SM_SPEED_SELECT_OFFSET);

    st = exec_grctx_write32(ctl, hClient, hSubdev,
                             CTXSW_SM_SPEED_SELECT_OFFSET,
                             OVERRIDE_CLEAR_VALUE);
    if (st != 0) {
        printf("\n  *** GR_CTX write FAILED with status=0x%08x ***\n", st);
        printf("  Possible reasons:\n");
        printf("    - gpuValidateRegOffset rejects offset 0x%08x for GR_CTX type\n",
               CTXSW_SM_SPEED_SELECT_OFFSET);
        printf("    - The ctxsw firmware header offset is not in the valid range\n");
        printf("    - GPU is in reset or not fully powered\n");
    } else {
        printf("  [OK] GR_CTX write succeeded.\n");
        printf("  -> Override registered for future context inits.\n");
        printf("  -> Non-resident channel ctxsw buffers patched.\n");
    }
    printf("\n");

    /* ==================================================================== */
    /* STEP 4: GLOBAL WRITE to FECS register (direct BAR0 write)           */
    /* ==================================================================== */
    printf("=== Step 4: GLOBAL WRITE FECS 0x%08x <- 0x%08x ===\n",
           FECS_SM_SPEED_SELECT_OFFSET, OVERRIDE_CLEAR_VALUE);
    st = exec_global_write32(ctl, hClient, hSubdev,
                              FECS_SM_SPEED_SELECT_OFFSET,
                              OVERRIDE_CLEAR_VALUE);
    if (st != 0) {
        printf("  FECS write not supported on this GPU (expected for CMP)\n");
    }
    printf("\n");

    /* ==================================================================== */
    /* STEP 5: Read-back verification (FECS only — GR_CTX read is BAR0)   */
    /* ==================================================================== */
    printf("=== Step 5: Read-back verification ===\n");
    NvU32 fecs_after = exec_global_read32(ctl, hClient, hSubdev,
                                           FECS_SM_SPEED_SELECT_OFFSET);
    if (fecs_after == 0xFFFFFFFFu) {
        printf("  FECS read-back: 0xFFFFFFFF (dead decode on CMP)\n");
    } else {
        decode_speed_select(fecs_after, "FECS SM_SPEED_SELECT (after)");
        if (fecs_after == 0) {
            printf("\n  *** FECS override cleared! ***\n");
        } else if (fecs_after == fecs_before) {
            printf("\n  FECS unchanged (write may be masked or read-only)\n");
        } else {
            printf("\n  FECS changed from 0x%08x to 0x%08x\n",
                   fecs_before, fecs_after);
        }
    }

    /* ==================================================================== */
    /* STEP 6: GR_CTX read-back (BAR0 PRI)                                 */
    /* ==================================================================== */
    printf("\n=== Step 6: GR_CTX read-back at 0x%08x (BAR0 PRI) ===\n",
           CTXSW_SM_SPEED_SELECT_OFFSET);
    NvU32 grctx_bar0_after = exec_grctx_read32(ctl, hClient, hSubdev,
                                                CTXSW_SM_SPEED_SELECT_OFFSET);
    if (grctx_bar0_after != 0xFFFFFFFFu) {
        printf("  BAR0 PRI at 0x%08x = 0x%08x (previous: 0x%08x)\n",
               CTXSW_SM_SPEED_SELECT_OFFSET, grctx_bar0_after, grctx_bar0_val);
        if (grctx_bar0_after != grctx_bar0_val) {
            printf("  Note: BAR0 PRI value changed — this is NOT the ctxsw buffer.\n");
        }
    }

    /* ==================================================================== */
    /* RESULTS                                                              */
    /* ==================================================================== */
    printf("\n=== Results ===\n");
    if (st == 0) {
        printf("  GR_CTX override write: SUCCEEDED\n");
        printf("  Effect: SM_SPEED_SELECT override cleared in context init\n");
        printf("  override table. Run a CUDA workload to verify:\n");
        printf("    - Launch a new CUDA context (e.g., ./vector_add)\n");
        printf("    - Performance should increase if override takes effect\n");
        printf("  Note: GR_CTX write affects:\n");
        printf("    1. Override table (all future context inits)\n");
        printf("    2. Non-resident channel ctxsw buffers (immediate)\n");
        printf("  Not affected: resident channel contexts (need context\n");
        printf("  reinitialization for override to apply)\n");
    } else {
        printf("  GR_CTX override write: FAILED (status=0x%08x)\n", st);
        printf("  This confirms the RM blocks GR_CTX writes to offset\n");
        printf("  0x%08x on this configuration.\n", CTXSW_SM_SPEED_SELECT_OFFSET);
    }

    printf("\n=== Done ===\n");
    return 0;
}
