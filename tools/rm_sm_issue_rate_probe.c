#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef uint8_t NvU8;
typedef uint16_t NvU16;
typedef uint32_t NvU32;
typedef uint64_t NvU64;
typedef int32_t NvS32;
typedef uint32_t NvHandle;

#define NV_IOCTL_MAGIC 'F'
#define NV_ESC_CARD_INFO 200
#define NV_ESC_REGISTER_FD 201
#define NV_ESC_RM_CONTROL 0x2A
#define NV_ESC_RM_ALLOC 0x2B

#define NV01_NULL_OBJECT 0
#define NV01_ROOT 0x00000000
#define NV01_DEVICE_0 0x00000080
#define NV20_SUBDEVICE_0 0x00002080

#define NV0000_CTRL_GPU_MAX_ATTACHED_GPUS 32
#define NV0000_CTRL_GPU_INVALID_ID 0xffffffffu
#define NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS 0x201u
#define NV0000_CTRL_CMD_GPU_GET_ID_INFO 0x202u
#define NV0000_CTRL_CMD_SYSTEM_GET_PRIVILEGED_STATUS 0x135u
#define NV0000_CTRL_SYSTEM_GET_PRIVILEGED_STATUS_PRIV_USER_FLAG 0x1u
#define NV0000_CTRL_SYSTEM_GET_PRIVILEGED_STATUS_KERNEL_HANDLE_FLAG 0x2u
#define NV0000_CTRL_SYSTEM_GET_PRIVILEGED_STATUS_PRIV_HANDLE_FLAG 0x4u
#define NV2080_CTRL_CMD_GPU_EXEC_REG_OPS 0x20800122u
#define NV2080_CTRL_CMD_GR_REG_ACCESS 0x20801226u
#define NV2080_CTRL_CMD_INTERNAL_GR_REG_ACCESS 0x20800a15u
#define NV2080_CTRL_CMD_INTERNAL_STATIC_KGR_GET_SM_ISSUE_RATE_MODIFIER 0x20800a34u
#define NV2080_CTRL_CMD_INTERNAL_STATIC_GR_GET_SM_ISSUE_RATE_MODIFIER 0x20800a35u
#define NV2080_CTRL_CMD_GR_GET_SM_ISSUE_RATE_MODIFIER 0x20801230u
#define NV2080_CTRL_GR_REG_ACCESS_FLAG_READ 0x1u
#define NV2080_CTRL_GR_REG_ACCESS_FLAG_LEGACY 0x4u
#define NV2080_CTRL_INTERNAL_GR_MAX_ENGINES 8
#define NV2080_CTRL_GPU_REG_OP_READ_32 0x0u
#define NV2080_CTRL_GPU_REG_OP_TYPE_GLOBAL 0x0u
#define NV2080_CTRL_GPU_REG_OP_TYPE_GR_CTX 0x1u
#define NV2080_CTRL_GPU_REG_OP_TYPE_DEVICE 0x80u

typedef struct {
    NvU32 domain;
    NvU8 bus;
    NvU8 slot;
    NvU8 function;
    NvU16 vendor_id;
    NvU16 device_id;
} nv_pci_info_t;

typedef struct {
    NvU32 valid;
    nv_pci_info_t pci_info;
    NvU32 gpu_id;
    NvU16 interrupt_line;
    NvU64 reg_address;
    NvU64 reg_size;
    NvU64 fb_address;
    NvU64 fb_size;
    NvU32 minor_number;
    NvU8 dev_name[10];
} nv_ioctl_card_info_t;

typedef struct {
    int ctl_fd;
} nv_ioctl_register_fd_t;

typedef struct {
    NvHandle hRoot;
    NvHandle hObjectParent;
    NvHandle hObjectNew;
    NvU32 hClass;
    NvU64 pAllocParms;
    NvU32 status;
} NVOS21_PARAMETERS;

typedef struct {
    NvHandle hRoot;
    NvHandle hObjectParent;
    NvHandle hObjectNew;
    NvU32 hClass;
    NvU64 pAllocParms;
    NvU64 pRightsRequested;
    NvU32 status;
} NVOS64_PARAMETERS;

typedef struct {
    NvHandle hClient;
    NvHandle hObject;
    NvU32 cmd;
    NvU32 flags;
    NvU64 params;
    NvU32 paramsSize;
    NvU32 status;
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
    NvU8 privStatusFlags;
} NV0000_CTRL_SYSTEM_GET_PRIVILEGED_STATUS_PARAMS;

typedef struct {
    NvU32 flags;
    NvU32 pad;
    NvU64 route;
} NV2080_CTRL_GR_ROUTE_INFO;

typedef struct {
    NV2080_CTRL_GR_ROUTE_INFO grRouteInfo;
    NvU8 imla0;
    NvU8 fmla16;
    NvU8 dp;
    NvU8 fmla32;
    NvU8 ffma;
    NvU8 imla1;
    NvU8 imla2;
    NvU8 imla3;
    NvU8 imla4;
} NV2080_CTRL_GR_GET_SM_ISSUE_RATE_MODIFIER_PARAMS;

typedef struct {
    NvU8 imla0;
    NvU8 fmla16;
    NvU8 dp;
    NvU8 fmla32;
    NvU8 ffma;
    NvU8 imla1;
    NvU8 imla2;
    NvU8 imla3;
    NvU8 imla4;
} NV2080_CTRL_INTERNAL_STATIC_GR_SM_ISSUE_RATE_MODIFIER;

typedef struct {
    NV2080_CTRL_INTERNAL_STATIC_GR_SM_ISSUE_RATE_MODIFIER smIssueRateModifier[NV2080_CTRL_INTERNAL_GR_MAX_ENGINES];
} NV2080_CTRL_INTERNAL_STATIC_GR_GET_SM_ISSUE_RATE_MODIFIER_PARAMS;

typedef struct {
    NvU32 regOffset;
    NvU32 regVal;
    NV2080_CTRL_GR_ROUTE_INFO grRouteInfo;
    NvU32 accessFlag;
} NV2080_CTRL_GR_REG_ACCESS_PARAMS;

typedef struct {
    NvU8 regOp;
    NvU8 regType;
    NvU8 regStatus;
    NvU8 regQuad;
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
    NvU32 bNonTransactional;
    NvU32 reserved00[2];
    NvU32 regOpCount;
    NvU64 regOps;
    NV2080_CTRL_GR_ROUTE_INFO grRouteInfo;
} NV2080_CTRL_GPU_EXEC_REG_OPS_PARAMS;

static int rm_ioctl(int fd, unsigned nr, void *arg, size_t size) {
    unsigned long request = _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, nr, size);
    return ioctl(fd, request, arg);
}

static NvU32 rm_alloc21(int ctl, NVOS21_PARAMETERS *p) {
    if (rm_ioctl(ctl, NV_ESC_RM_ALLOC, p, sizeof(*p)) < 0) {
        fprintf(stderr, "ioctl alloc21 failed: %s\n", strerror(errno));
        return 0xffffffffu;
    }
    return p->status;
}

static NvU32 rm_alloc64(int ctl, NVOS64_PARAMETERS *p) {
    if (rm_ioctl(ctl, NV_ESC_RM_ALLOC, p, sizeof(*p)) < 0) {
        fprintf(stderr, "ioctl alloc64 failed: %s\n", strerror(errno));
        return 0xffffffffu;
    }
    return p->status;
}

static NvU32 rm_control(int ctl, NvHandle client, NvHandle object, NvU32 cmd, void *params, NvU32 params_size) {
    NVOS54_PARAMETERS p;
    memset(&p, 0, sizeof(p));
    p.hClient = client;
    p.hObject = object;
    p.cmd = cmd;
    p.params = (NvU64)(uintptr_t)params;
    p.paramsSize = params_size;
    if (rm_ioctl(ctl, NV_ESC_RM_CONTROL, &p, sizeof(p)) < 0) {
        fprintf(stderr, "ioctl control 0x%x failed: %s\n", cmd, strerror(errno));
        return 0xffffffffu;
    }
    return p.status;
}

static const char *speed_name(NvU8 v) {
    switch (v) {
    case 0: return "FULL";
    case 1: return "REDUCED_1_2";
    case 2: return "REDUCED_1_4";
    case 3: return "REDUCED_1_8";
    case 4: return "REDUCED_1_16";
    case 5: return "REDUCED_1_32";
    case 6: return "REDUCED_1_64";
    default: return "UNKNOWN";
    }
}

static void print_issue_rate_row(const char *prefix, const NV2080_CTRL_INTERNAL_STATIC_GR_SM_ISSUE_RATE_MODIFIER *sm) {
    printf("%s imla0=%u(%s) fmla16=%u(%s) dp=%u(%s) fmla32=%u(%s) ffma=%u(%s) imla1=%u(%s) imla2=%u(%s) imla3=%u(%s) imla4=%u(%s)\n",
           prefix,
           sm->imla0, speed_name(sm->imla0),
           sm->fmla16, speed_name(sm->fmla16),
           sm->dp, speed_name(sm->dp),
           sm->fmla32, speed_name(sm->fmla32),
           sm->ffma, speed_name(sm->ffma),
           sm->imla1, speed_name(sm->imla1),
           sm->imla2, speed_name(sm->imla2),
           sm->imla3, speed_name(sm->imla3),
           sm->imla4, speed_name(sm->imla4));
}

static void try_static_issue_rate(int ctl, NvHandle hClient, NvHandle hObject, NvU32 cmd, const char *name) {
    NV2080_CTRL_INTERNAL_STATIC_GR_GET_SM_ISSUE_RATE_MODIFIER_PARAMS p;
    memset(&p, 0, sizeof(p));
    NvU32 st = rm_control(ctl, hClient, hObject, cmd, &p, sizeof(p));
    printf("static_sm_issue_rate cmd=0x%08x %-38s status=0x%08x\n", cmd, name, st);
    if (st == 0) {
        for (unsigned i = 0; i < NV2080_CTRL_INTERNAL_GR_MAX_ENGINES; i++) {
            char prefix[64];
            snprintf(prefix, sizeof(prefix), "%s[%u]", name, i);
            print_issue_rate_row(prefix, &p.smIssueRateModifier[i]);
        }
    }
}

static void try_gr_read(int ctl, NvHandle hClient, NvHandle hObject, NvU32 cmd, NvU32 offset, const char *name) {
    NV2080_CTRL_GR_REG_ACCESS_PARAMS p;
    memset(&p, 0, sizeof(p));
    p.regOffset = offset;
    p.accessFlag = NV2080_CTRL_GR_REG_ACCESS_FLAG_READ | NV2080_CTRL_GR_REG_ACCESS_FLAG_LEGACY;
    NvU32 st = rm_control(ctl, hClient, hObject, cmd, &p, sizeof(p));
    printf("gr_reg_read cmd=0x%08x %-42s offset=0x%08x status=0x%08x val=0x%08x\n",
           cmd, name, offset, st, p.regVal);
}

static const char *reg_type_name(NvU8 type) {
    switch (type) {
    case NV2080_CTRL_GPU_REG_OP_TYPE_GLOBAL: return "GLOBAL";
    case NV2080_CTRL_GPU_REG_OP_TYPE_GR_CTX: return "GR_CTX";
    case NV2080_CTRL_GPU_REG_OP_TYPE_DEVICE: return "DEVICE";
    default: return "UNKNOWN";
    }
}

static void try_exec_reg_ops_read(int ctl, NvHandle hClient, NvHandle hObject, NvU8 regType, NvU32 offset, const char *name) {
    NV2080_CTRL_GPU_REG_OP op;
    NV2080_CTRL_GPU_EXEC_REG_OPS_PARAMS p;
    memset(&op, 0, sizeof(op));
    memset(&p, 0, sizeof(p));
    op.regOp = NV2080_CTRL_GPU_REG_OP_READ_32;
    op.regType = regType;
    op.regOffset = offset;
    p.bNonTransactional = 1;
    p.regOpCount = 1;
    p.regOps = (NvU64)(uintptr_t)&op;
    NvU32 st = rm_control(ctl, hClient, hObject, NV2080_CTRL_CMD_GPU_EXEC_REG_OPS, &p, sizeof(p));
    printf("exec_reg_ops_read %-6s %-42s offset=0x%08x status=0x%08x regStatus=0x%02x val=0x%08x\n",
           reg_type_name(regType), name, offset, st, op.regStatus, op.regValueLo);
}

static void decode_gv100_fecs_speed_select_reference(NvU32 readout, NvU32 override) {
    printf("reference_decode_gv100_fecs_feature_readout=0x%08x dp=%u imla=%u fmla=%u\n",
           readout,
           (readout >> 20) & 1u,
           (readout >> 21) & 1u,
           (readout >> 22) & 1u);
    printf("reference_decode_gv100_fecs_speed_override=0x%08x imla_reduced=%u imla_override=%u fmla_reduced=%u fmla_override=%u dp_reduced=%u dp_override=%u\n",
           override,
           (override >> 0) & 1u,
           (override >> 3) & 1u,
           (override >> 4) & 1u,
           (override >> 7) & 1u,
           (override >> 8) & 1u,
           (override >> 11) & 1u);
}

int main(int argc, char **argv) {
    int target_minor = 14;
    if (argc > 1) target_minor = atoi(argv[1]);

    int ctl = open("/dev/nvidiactl", O_RDWR | O_CLOEXEC);
    if (ctl < 0) {
        perror("open /dev/nvidiactl");
        return 1;
    }

    nv_ioctl_card_info_t cards[32];
    memset(cards, 0, sizeof(cards));
    if (rm_ioctl(ctl, NV_ESC_CARD_INFO, cards, sizeof(cards)) < 0) {
        perror("ioctl CARD_INFO");
        return 1;
    }

    int card_index = -1;
    for (int i = 0; i < 32; i++) {
        if (cards[i].valid) {
            printf("card[%d] minor=%u gpu_id=0x%08x pci=%04x:%02x:%02x.%u dev=0x%04x\n",
                   i, cards[i].minor_number, cards[i].gpu_id,
                   cards[i].pci_info.domain, cards[i].pci_info.bus,
                   cards[i].pci_info.slot, cards[i].pci_info.function,
                   cards[i].pci_info.device_id);
            if ((int)cards[i].minor_number == target_minor) card_index = i;
        }
    }
    if (card_index < 0) {
        fprintf(stderr, "target minor %d not found\n", target_minor);
        return 2;
    }

    NVOS21_PARAMETERS root;
    memset(&root, 0, sizeof(root));
    root.hClass = NV01_ROOT;
    NvU32 st = rm_alloc21(ctl, &root);
    printf("alloc root status=0x%08x hClient=0x%08x\n", st, root.hObjectNew);
    if (st != 0) return 3;
    NvHandle hClient = root.hObjectNew;

    NV0000_CTRL_SYSTEM_GET_PRIVILEGED_STATUS_PARAMS priv;
    memset(&priv, 0, sizeof(priv));
    st = rm_control(ctl, hClient, hClient, NV0000_CTRL_CMD_SYSTEM_GET_PRIVILEGED_STATUS,
                    &priv, sizeof(priv));
    printf("privileged_status status=0x%08x flags=0x%02x priv_user=%u kernel_handle=%u priv_handle=%u\n",
           st, priv.privStatusFlags,
           (priv.privStatusFlags & NV0000_CTRL_SYSTEM_GET_PRIVILEGED_STATUS_PRIV_USER_FLAG) ? 1u : 0u,
           (priv.privStatusFlags & NV0000_CTRL_SYSTEM_GET_PRIVILEGED_STATUS_KERNEL_HANDLE_FLAG) ? 1u : 0u,
           (priv.privStatusFlags & NV0000_CTRL_SYSTEM_GET_PRIVILEGED_STATUS_PRIV_HANDLE_FLAG) ? 1u : 0u);

    NV0000_CTRL_GPU_GET_ATTACHED_IDS_PARAMS attached;
    memset(&attached, 0xff, sizeof(attached));
    st = rm_control(ctl, hClient, hClient, NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS, &attached, sizeof(attached));
    printf("get attached ids status=0x%08x\n", st);

    NvU32 device_instance = 0xffffffffu;
    for (int i = 0; i < NV0000_CTRL_GPU_MAX_ATTACHED_GPUS; i++) {
        if (attached.gpuIds[i] == NV0000_CTRL_GPU_INVALID_ID) continue;
        NV0000_CTRL_GPU_GET_ID_INFO_PARAMS id;
        memset(&id, 0, sizeof(id));
        id.gpuId = attached.gpuIds[i];
        NvU32 ist = rm_control(ctl, hClient, hClient, NV0000_CTRL_CMD_GPU_GET_ID_INFO, &id, sizeof(id));
        printf("gpuId=0x%08x id_info_status=0x%08x devInst=%u subdevInst=%u gpuInst=%u boardId=0x%x\n",
               attached.gpuIds[i], ist, id.deviceInstance, id.subDeviceInstance, id.gpuInstance, id.boardId);
        if (attached.gpuIds[i] == cards[card_index].gpu_id) device_instance = id.deviceInstance;
    }
    if (device_instance == 0xffffffffu) {
        fprintf(stderr, "could not map target gpu_id to device instance\n");
        return 4;
    }

    char devpath[64];
    snprintf(devpath, sizeof(devpath), "/dev/nvidia%d", target_minor);
    int devfd = open(devpath, O_RDWR | O_CLOEXEC);
    if (devfd < 0) {
        perror("open /dev/nvidiaN");
        return 5;
    }
    nv_ioctl_register_fd_t reg;
    memset(&reg, 0, sizeof(reg));
    reg.ctl_fd = ctl;
    if (rm_ioctl(devfd, NV_ESC_REGISTER_FD, &reg, sizeof(reg)) < 0) {
        fprintf(stderr, "register ctl fd failed: %s\n", strerror(errno));
    } else {
        printf("registered ctl fd on %s\n", devpath);
    }

    NvHandle hDevice = 0xd0000000u | ((target_minor & 0xffu) << 8);
    NV0080_ALLOC_PARAMETERS devp;
    memset(&devp, 0, sizeof(devp));
    devp.deviceId = device_instance;
    NVOS64_PARAMETERS devalloc;
    memset(&devalloc, 0, sizeof(devalloc));
    devalloc.hRoot = hClient;
    devalloc.hObjectParent = hClient;
    devalloc.hObjectNew = hDevice;
    devalloc.hClass = NV01_DEVICE_0;
    devalloc.pAllocParms = (NvU64)(uintptr_t)&devp;
    st = rm_alloc64(ctl, &devalloc);
    if (st == 0xffffffffu) {
        NVOS21_PARAMETERS devalloc21;
        memset(&devalloc21, 0, sizeof(devalloc21));
        devalloc21.hRoot = hClient;
        devalloc21.hObjectParent = hClient;
        devalloc21.hObjectNew = hDevice;
        devalloc21.hClass = NV01_DEVICE_0;
        devalloc21.pAllocParms = (NvU64)(uintptr_t)&devp;
        st = rm_alloc21(ctl, &devalloc21);
    }
    printf("alloc device status=0x%08x hDevice=0x%08x deviceInstance=%u\n", st, hDevice, device_instance);
    if (st != 0) return 6;

    NvHandle hSubdev = hDevice | 1u;
    NV2080_ALLOC_PARAMETERS subp;
    memset(&subp, 0, sizeof(subp));
    subp.subDeviceId = 0;
    NVOS64_PARAMETERS suballoc;
    memset(&suballoc, 0, sizeof(suballoc));
    suballoc.hRoot = hClient;
    suballoc.hObjectParent = hDevice;
    suballoc.hObjectNew = hSubdev;
    suballoc.hClass = NV20_SUBDEVICE_0;
    suballoc.pAllocParms = (NvU64)(uintptr_t)&subp;
    st = rm_alloc64(ctl, &suballoc);
    if (st == 0xffffffffu) {
        NVOS21_PARAMETERS suballoc21;
        memset(&suballoc21, 0, sizeof(suballoc21));
        suballoc21.hRoot = hClient;
        suballoc21.hObjectParent = hDevice;
        suballoc21.hObjectNew = hSubdev;
        suballoc21.hClass = NV20_SUBDEVICE_0;
        suballoc21.pAllocParms = (NvU64)(uintptr_t)&subp;
        st = rm_alloc21(ctl, &suballoc21);
    }
    printf("alloc subdevice status=0x%08x hSubdev=0x%08x\n", st, hSubdev);
    if (st != 0) return 7;

    NV2080_CTRL_GR_GET_SM_ISSUE_RATE_MODIFIER_PARAMS sm;
    memset(&sm, 0, sizeof(sm));
    st = rm_control(ctl, hClient, hSubdev, NV2080_CTRL_CMD_GR_GET_SM_ISSUE_RATE_MODIFIER, &sm, sizeof(sm));
    printf("get_sm_issue_rate_modifier status=0x%08x\n", st);
    if (st == 0) {
        printf("imla0=%u(%s) fmla16=%u(%s) dp=%u(%s) fmla32=%u(%s) ffma=%u(%s) imla1=%u(%s) imla2=%u(%s) imla3=%u(%s) imla4=%u(%s)\n",
               sm.imla0, speed_name(sm.imla0),
               sm.fmla16, speed_name(sm.fmla16),
               sm.dp, speed_name(sm.dp),
               sm.fmla32, speed_name(sm.fmla32),
               sm.ffma, speed_name(sm.ffma),
               sm.imla1, speed_name(sm.imla1),
               sm.imla2, speed_name(sm.imla2),
               sm.imla3, speed_name(sm.imla3),
               sm.imla4, speed_name(sm.imla4));
    }

    try_static_issue_rate(ctl, hClient, hSubdev, NV2080_CTRL_CMD_INTERNAL_STATIC_KGR_GET_SM_ISSUE_RATE_MODIFIER,
                          "INTERNAL_STATIC_KGR");
    try_static_issue_rate(ctl, hClient, hSubdev, NV2080_CTRL_CMD_INTERNAL_STATIC_GR_GET_SM_ISSUE_RATE_MODIFIER,
                          "INTERNAL_STATIC_GR");

    const NvU8 reg_types[] = {
        NV2080_CTRL_GPU_REG_OP_TYPE_GLOBAL,
        NV2080_CTRL_GPU_REG_OP_TYPE_GR_CTX,
        NV2080_CTRL_GPU_REG_OP_TYPE_DEVICE,
    };
    for (unsigned ti = 0; ti < sizeof(reg_types) / sizeof(reg_types[0]); ti++) {
        NvU8 regType = reg_types[ti];
        try_exec_reg_ops_read(ctl, hClient, hSubdev, regType, 0x00409660u, "GV100 FECS FEATURE_READOUT");
        try_exec_reg_ops_read(ctl, hClient, hSubdev, regType, 0x00409664u, "GV100 FECS SM_SPEED_SELECT override");
    }

    const NvU32 gr_cmds[] = {
        NV2080_CTRL_CMD_GR_REG_ACCESS,
        NV2080_CTRL_CMD_INTERNAL_GR_REG_ACCESS,
    };
    for (unsigned ci = 0; ci < sizeof(gr_cmds) / sizeof(gr_cmds[0]); ci++) {
        NvU32 cmd = gr_cmds[ci];
        try_gr_read(ctl, hClient, hSubdev, cmd, 0x00409660u, "GV100 FECS FEATURE_READOUT");
        try_gr_read(ctl, hClient, hSubdev, cmd, 0x00409664u, "GV100 FECS SM_SPEED_SELECT override");
        try_gr_read(ctl, hClient, hSubdev, cmd, 0x00409800u, "known-debugdump PGRAPH 0x409800");
        try_gr_read(ctl, hClient, hSubdev, cmd, 0x00409900u, "possible FECS speed-select override");
        try_gr_read(ctl, hClient, hSubdev, cmd, 0x00409904u, "possible FECS speed-select override_1");
        try_gr_read(ctl, hClient, hSubdev, cmd, 0x004098fcu, "near FECS speed-select");
        try_gr_read(ctl, hClient, hSubdev, cmd, 0x00019900u, "relative 0x19900 candidate");
        try_gr_read(ctl, hClient, hSubdev, cmd, 0x00019904u, "relative 0x19904 candidate");
    }
    decode_gv100_fecs_speed_select_reference(0, 0x999);
    return st == 0 ? 0 : 8;
}
