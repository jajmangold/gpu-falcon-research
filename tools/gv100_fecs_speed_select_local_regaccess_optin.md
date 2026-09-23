# GV100 FECS Speed-Select Local Regaccess Opt-In

This repository update is a local diagnostic opt-in for register access generation only.

What changed:

- Added to local regaccess config:
  - `/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/config/common.yml`
  - `NV_PGRAPH_PRI_FECS_FEATURE_READOUT: [volta]`
  - `NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT: [volta]`

Resulting target registers:

- `0x00409660` (`NV_PGRAPH_PRI_FECS_FEATURE_READOUT`)
- `0x00409664` (`NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT`)

What this does:

- Makes these GV100 FECS entries eligible for user-access-map generation.
- It is intended for read-path probing (e.g., `NV2080_CTRL_CMD_GPU_EXEC_REG_OPS`) after a local RM rebuild.
- It does **not** perform or enable any write-side uncap behavior by itself.

How to use:

- Rebuild/regenerate the regaccess artifacts for the `volta` family and the RM in your local tree.
- Use a debug probe path that only performs read ops and checks `regStatus` transitions for these addresses.

## Host run status (2026-05-20)

I ran the regaccess generator on this host and it succeeds after a minimal Python 3 compatibility pass in the local tree tooling:

1. Added Python 3 compatibility shims to:
   - `regaccess/gen_access_map.py` (argparse/FileType etc. already fixed for binary output)
   - `regparse/regparse.py`
   - `accessmap` + `verify`/`rmconfig` helper modules
2. Installed missing local runtime packages (`python3-mako`, `python3-docopt`) to avoid script import failures.

Generated files:

- `/tmp/gv100_regaccess/user_access_map.bin`
- `/tmp/gv100_regaccess/user_access_map_reglist.h`
- `/tmp/gv100_regaccess/user_access_map.h`
- `/tmp/gv100_regaccess/user_access_map_gzip.h`

The generated reglist includes both needed GV100 entries:

- `ACCESS_MAP_REGISTER(NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT)`
- `ACCESS_MAP_REGISTER(NV_PGRAPH_PRI_FECS_FEATURE_READOUT)`

Command used:

```bash
cd /home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess
PYTHONPATH=accessmap python3 gen_access_map.py build \
  --manual_dir /home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100 \
  --binary /tmp/gv100_regaccess/user_access_map.bin \
  --template templates/accessmap.h /tmp/gv100_regaccess/user_access_map.h \
  --template templates/accessmap_gzip.h /tmp/gv100_regaccess/user_access_map_gzip.h \
  --template templates/reglist.h /tmp/gv100_regaccess/user_access_map_reglist.h \
  config/common.yml config/cuda_tools.yml
```
