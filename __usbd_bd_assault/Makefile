
IOP_BIN = ../usbd_bd_assault.irx

ATAD_SOURCE = $(PS2SDK)/iop/dev9/atad/src/ps2atad.c
DEV9_BUILD_DIR = $(PS2SDK)/iop/dev9/dev9
DEV9_IRX = $(DEV9_BUILD_DIR)/irx/ps2dev9.irx

IOP_INCS += -I../bdm/include \
	-I$(PS2SDK)/iop/dev9/atad/include \
	-I$(PS2SDK)/iop/dev9/dev9/include
IOP_CFLAGS += -DATA_ENABLE_BDM=1 -DATA_USE_DEV9=1 -DATA_GAMESTAR_WORKAROUND=1

# usbd_usbd.c supplies the HDD wrapper. DEV9 is embedded in the result so
# POPStarter still receives one self-contained driver file.
IOP_OBJS = usbd_usbd.o atad.o dev9_blob.o imports.o exports.o

include ../global.make

$(IOP_OBJS_DIR)atad.o: $(ATAD_SOURCE)
	$(DIR_GUARD)
	$(IOP_C_COMPILE) -D_start=bdm_hdd_atad_start -c $< -o $@

$(DEV9_IRX):
	$(MAKE) -C $(DEV9_BUILD_DIR)

$(IOP_OBJS_DIR)dev9_blob.o: $(DEV9_IRX)
	$(DIR_GUARD)
	$(PS2SDK)/bin/bin2c $< $(IOP_OBJS_DIR)dev9_blob.c bdm_hdd_dev9_irx
	$(IOP_C_COMPILE) -c $(IOP_OBJS_DIR)dev9_blob.c -o $@

$(IOP_OBJS_DIR)usbd_usbd.o: src/usbd/usbd_usbd.c
	$(DIR_GUARD)
	$(IOP_C_COMPILE) -c $< -o $@
