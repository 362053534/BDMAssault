# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.

# Enable to perform the SCE HDD check at the end of ATA device initialization.
ATA_SCE_AUTH_HDD ?= 0

# Enable to enable support for Multi-Word DMA (MWDMA) modes, as in early ATAD modules.
ATA_MWDMA_MODES ?= 0

# Enable to enable support for PIO modes 0-4 (as in early ATAD modules), instead of just PIO mode 0.
ATA_ALL_PIO_MODES ?= 0

# Disable to disable the workaround for bad GameStar compatible adaptors.
ATA_GAMESTAR_WORKAROUND ?= 1

# Enable to enable support for the BDM interface.
ATA_ENABLE_BDM ?= 0

# Enable to enable support for the BDM interface.
ATA_ENABLE_MAXUDMA ?= 0

# Enable to use the dev9/SPEED interface
ATA_USE_DEV9 ?= 1

# Enable to use the dev9/AIF interface
ATA_USE_AIFDEV9 ?= 0

IOP_BIN = atad_bd_assault.irx
IOP_OBJS = ps2atad.o ps2dev9 _start.o imports.o exports.o

ifeq ($(ATA_SCE_AUTH_HDD),1)
IOP_CFLAGS += -DATA_SCE_AUTH_HDD=1
endif

ifeq ($(ATA_MWDMA_MODES),1)
IOP_CFLAGS += -DATA_MWDMA_MODES=1
endif

ifeq ($(ATA_ALL_PIO_MODES),1)
IOP_CFLAGS += -DATA_ALL_PIO_MODES=1
endif

ifeq ($(ATA_GAMESTAR_WORKAROUND),1)
IOP_CFLAGS += -DATA_GAMESTAR_WORKAROUND=1
endif

ifeq ($(ATA_ENABLE_BDM),1)
IOP_CFLAGS += -DATA_ENABLE_BDM=1
IOP_INCS += -I$(PS2SDKSRC)/iop/fs/bdm/include
endif

ifeq ($(ATA_ENABLE_MAXUDMA),1)
IOP_CFLAGS += -DATA_ENABLE_MAXUDMA=1
endif

ifeq ($(ATA_USE_DEV9),1)
IOP_CFLAGS += -DATA_USE_DEV9=1
endif

ifeq ($(ATA_USE_AIFDEV9),1)
IOP_CFLAGS += -DATA_USE_AIFDEV9=1
endif

# DEV9 STUFF


# Enable to disable initialization of SMAP within DEV9 (i.e. clone adaptor compatibility).
DEV9_SKIP_SMAP_INIT ?= 0

# Enable to enable support for the PSX's devices
DEV9_PSX_SUPPORT ?= 1

# Disable to disable the workaround (skip SMAP initialization) for bad GameStar compatible adaptors.
DEV9_GAMESTAR_WORKAROUND ?= 1

# Enable to enable support for AIF specific functions
DEV9_ENABLE_AIF ?= 0

ifeq ($(DEV9_SKIP_SMAP_INIT),1)
IOP_CFLAGS += -DDEV9_SKIP_SMAP_INIT=1
endif

ifeq ($(DEV9_PSX_SUPPORT),1)
IOP_CFLAGS += -DDEV9_PSX_SUPPORT=1
endif

ifeq ($(DEV9_GAMESTAR_WORKAROUND),1)
IOP_CFLAGS += -DDEV9_GAMESTAR_WORKAROUND=1
endif

ifeq ($(DEV9_ENABLE_AIF),1)
IOP_CFLAGS += -DDEV9_ENABLE_AIF=1
endif


include ../global.make