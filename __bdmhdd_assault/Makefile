# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# BDM HDD Assault for POPStarter/OPL (HDD slot):
# Link DEV9 + ATAD into one IRX and call their start functions directly.
# Do NOT nest LoadModuleBuffer for ATA — that breaks import order / start.
# 成品名由调用方（OPL）约定，此处只输出 bdmhdd.irx

IOP_BIN = ../bdmhdd.irx

# ATAD options
ATA_SCE_AUTH_HDD ?= 0
ATA_MWDMA_MODES ?= 0
ATA_ALL_PIO_MODES ?= 0
ATA_GAMESTAR_WORKAROUND ?= 1
ATA_ENABLE_BDM ?= 1
ATA_ENABLE_MAXUDMA ?= 0
ATA_USE_DEV9 ?= 1
ATA_USE_AIFDEV9 ?= 0

# DEV9 options
DEV9_SKIP_SMAP_INIT ?= 0
DEV9_PSX_SUPPORT ?= 1
DEV9_GAMESTAR_WORKAROUND ?= 1
DEV9_ENABLE_AIF ?= 0

IOP_OBJS = usbd_usbd.o ps2dev9.o ps2atad.o imports.o exports.o

IOP_INCS += -I./include -I../bdm/include

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

# Avoid duplicate _start/_exit; module entry stays in usbd_usbd.c
$(IOP_OBJS_DIR)ps2dev9.o: $(IOP_SRC_DIR)ps2dev9.c
	$(DIR_GUARD)
	$(IOP_C_COMPILE) -D_start=dev9_start -D_exit=dev9_exit -c $< -o $@

$(IOP_OBJS_DIR)ps2atad.o: $(IOP_SRC_DIR)ps2atad.c
	$(DIR_GUARD)
	$(IOP_C_COMPILE) -D_start=atad_start -D_exit=atad_exit -c $< -o $@

$(IOP_OBJS_DIR)usbd_usbd.o: $(IOP_SRC_DIR)usbd/usbd_usbd.c
	$(DIR_GUARD)
	$(IOP_C_COMPILE) -c $< -o $@
