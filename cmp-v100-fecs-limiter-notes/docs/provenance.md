# Provenance And Source Notes

This write-up is a derived research summary. It does not include NVIDIA source
code, proprietary binaries, firmware blobs, ROM images, keys, leaked archives,
or links to leaked material.

## Source Classes Used

The investigation used several kinds of evidence:

```text
local hardware observations
local VBIOS dumps and decoded init scripts
public VBIOS comparator images
public debugdump-style artifacts where available
locally available NVIDIA driver/source-tree material
open-source tool behavior and documentation
```

The NVIDIA source-tree material referenced in the working notes is understood
to come from the 2022 Lapsus$ NVIDIA extortion/ransom leak. That provenance
matters because it explains why some internal names, HAL paths, generated
headers, and verification-only controls are visible in the research notes.

This repository intentionally avoids redistributing that material.

## Why Mention The Leak

The source matters for reproducibility and credibility. If another researcher
finds this summary, they should know that references such as:

```text
drivers/resman/...
drivers/common/inc/hwref/volta/gv100/...
diag/mods/...
uproc/...
```

refer to a locally examined NVIDIA source tree, not to files that NVIDIA
published as part of the normal open GPU kernel modules.

Several public tools and community projects in the NVIDIA firmware/flashing
space also appear to have been informed by the same leaked material. That does
not make redistribution appropriate; it only explains why the vocabulary and
structure may be familiar in related work.

## Citation Style Used Here

This repo cites symbols, file families, register names, offsets, and observed
values. It does not quote substantial source code.

Example citation style:

```text
volta/gv100/dev_graphics_nobundle.h
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00409664
```

For public-facing publication, keep citations at this level unless you have
independent permission to quote or redistribute the underlying files.

