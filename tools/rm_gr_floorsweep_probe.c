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

#define NV01_ROOT 0x00000000
#define NV01_DEVICE_0 0x00000080
#define NV20_SUBDEVICE_0 0x00002080

#define NV0000_CTRL_GPU_MAX_ATTACHED_GPUS 32
#define NV0000_CTRL_GPU_INVALID_ID 0xffffffffu
#define NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS 0x201u
#define NV0000_CTRL_CMD_GPU_GET_ID_INFO 0x202u
#define NV2080_CTRL_CMD_GR_GET_GLOBAL_SM_ORDER 0x2080121bu
#define NV2080_CTRL_CMD_GR_GET_GPC_MASK 0x2080122au
#define NV2080_CTRL_CMD_GR_GET_TPC_MASK 0x2080122bu
#define NV2080_CTRL_CMD_GR_GET_PHYS_GPC_MASK 0x20801232u
#define NV2080_CTRL_CMD_GR_GET_GLOBAL_SM_ORDER_MAX_SM_COUNT 512u

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
    NvU32 flags;
    NvU32 pad;
    NvU64 route;
} NV2080_CTRL_GR_ROUTE_INFO;

typedef struct {
    NV2080_CTRL_GR_ROUTE_INFO grRouteInfo;
    NvU32 gpcMask;
} NV2080_CTRL_GR_GET_GPC_MASK_PARAMS;

typedef struct {
    NV2080_CTRL_GR_ROUTE_INFO grRouteInfo;
    NvU32 gpcId;
    NvU32 tpcMask;
} NV2080_CTRL_GR_GET_TPC_MASK_PARAMS;

typedef struct {
    NvU32 physSyspipeId;
    NvU32 gpcMask;
} NV2080_CTRL_GR_GET_PHYS_GPC_MASK_PARAMS;

typedef struct {
    struct {
        NvU16 gpcId;
        NvU16 localTpcId;
        NvU16 localSmId;
        NvU16 globalTpcId;
        NvU16 virtualGpcId;
        NvU16 migratableTpcId;
    } globalSmId[NV2080_CTRL_CMD_GR_GET_GLOBAL_SM_ORDER_MAX_SM_COUNT];
    NvU16 numSm;
    NvU16 numTpc;
    NV2080_CTRL_GR_ROUTE_INFO grRouteInfo;
} NV2080_CTRL_GR_GET_GLOBAL_SM_ORDER_PARAMS;

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

static unsigned popcount32(NvU32 v) {
    unsigned n = 0;
    while (v) {
        n += v & 1u;
        v >>= 1;
    }
    return n;
}

static void print_global_sm_order(const NV2080_CTRL_GR_GET_GLOBAL_SM_ORDER_PARAMS *p) {
    unsigned per_gpc_tpc_seen[32] = {0};
    unsigned per_gpc_sm_seen[32] = {0};
    unsigned per_global_tpc_sm_seen[512] = {0};

    for (unsigned i = 0; i < p->numSm && i < NV2080_CTRL_CMD_GR_GET_GLOBAL_SM_ORDER_MAX_SM_COUNT; i++) {
        NvU16 gpc = p->globalSmId[i].gpcId;
        NvU16 gtpc = p->globalSmId[i].globalTpcId;
        if (gpc < 32) {
            per_gpc_sm_seen[gpc]++;
            if (p->globalSmId[i].localSmId == 0) per_gpc_tpc_seen[gpc]++;
        }
        if (gtpc < 512) per_global_tpc_sm_seen[gtpc]++;
    }

    printf("global_sm_order numSm=%u numTpc=%u sm_per_tpc_inferred=",
           p->numSm, p->numTpc);
    if (p->numTpc) {
        printf("%.2f", (double)p->numSm / (double)p->numTpc);
    } else {
        printf("n/a");
    }
    printf("\n");

    printf("global_sm_order per_gpc_tpc_counts");
    for (unsigned g = 0; g < 32; g++) {
        if (per_gpc_tpc_seen[g] || per_gpc_sm_seen[g]) {
            printf(" gpc%u:tpc=%u,sm=%u", g, per_gpc_tpc_seen[g], per_gpc_sm_seen[g]);
        }
    }
    printf("\n");

    printf("global_sm_order first_entries");
    for (unsigned i = 0; i < p->numSm && i < 24; i++) {
        printf(" [%u:g%u.t%u.s%u/gtpc%u/vgpc%u]",
               i,
               p->globalSmId[i].gpcId,
               p->globalSmId[i].localTpcId,
               p->globalSmId[i].localSmId,
               p->globalSmId[i].globalTpcId,
               p->globalSmId[i].virtualGpcId);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    int target_minor = 0;
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
        if (!cards[i].valid) continue;
        printf("card[%d] minor=%u gpu_id=0x%08x pci=%04x:%02x:%02x.%u dev=0x%04x\n",
               i, cards[i].minor_number, cards[i].gpu_id,
               cards[i].pci_info.domain, cards[i].pci_info.bus,
               cards[i].pci_info.slot, cards[i].pci_info.function,
               cards[i].pci_info.device_id);
        if ((int)cards[i].minor_number == target_minor) card_index = i;
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

    NV0000_CTRL_GPU_GET_ATTACHED_IDS_PARAMS attached;
    memset(&attached, 0xff, sizeof(attached));
    st = rm_control(ctl, hClient, hClient, NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS, &attached, sizeof(attached));
    printf("get_attached_ids status=0x%08x\n", st);

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

    NvHandle hDevice = 0xe0000000u | ((target_minor & 0xffu) << 8);
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

    NV2080_CTRL_GR_GET_PHYS_GPC_MASK_PARAMS phys;
    memset(&phys, 0, sizeof(phys));
    st = rm_control(ctl, hClient, hSubdev, NV2080_CTRL_CMD_GR_GET_PHYS_GPC_MASK, &phys, sizeof(phys));
    printf("phys_gpc_mask status=0x%08x syspipe=%u mask=0x%08x count=%u\n",
           st, phys.physSyspipeId, phys.gpcMask, popcount32(phys.gpcMask));

    NV2080_CTRL_GR_GET_GPC_MASK_PARAMS gpc;
    memset(&gpc, 0, sizeof(gpc));
    st = rm_control(ctl, hClient, hSubdev, NV2080_CTRL_CMD_GR_GET_GPC_MASK, &gpc, sizeof(gpc));
    printf("gpc_mask status=0x%08x mask=0x%08x count=%u\n", st, gpc.gpcMask, popcount32(gpc.gpcMask));

    unsigned total_tpc_from_masks = 0;
    for (unsigned g = 0; g < 16; g++) {
        NV2080_CTRL_GR_GET_TPC_MASK_PARAMS tpc;
        memset(&tpc, 0, sizeof(tpc));
        tpc.gpcId = g;
        NvU32 tst = rm_control(ctl, hClient, hSubdev, NV2080_CTRL_CMD_GR_GET_TPC_MASK, &tpc, sizeof(tpc));
        unsigned cnt = popcount32(tpc.tpcMask);
        if (tst == 0 && (tpc.tpcMask || (gpc.gpcMask & (1u << g)))) total_tpc_from_masks += cnt;
        printf("tpc_mask gpc=%u status=0x%08x mask=0x%08x count=%u\n", g, tst, tpc.tpcMask, cnt);
    }
    printf("total_tpc_from_masks=%u inferred_sm_count_x2=%u\n",
           total_tpc_from_masks, total_tpc_from_masks * 2u);

    NV2080_CTRL_GR_GET_GLOBAL_SM_ORDER_PARAMS order;
    memset(&order, 0, sizeof(order));
    st = rm_control(ctl, hClient, hSubdev, NV2080_CTRL_CMD_GR_GET_GLOBAL_SM_ORDER, &order, sizeof(order));
    printf("global_sm_order status=0x%08x\n", st);
    if (st == 0) print_global_sm_order(&order);

    return 0;
}
