
IOP_BIN = ../usbd_bd_assault.irx

DEV9_IRX = $(PS2SDK)/iop/irx/ps2dev9.irx
ATAD_IRX = $(PS2SDK)/iop/irx/ata_bd.irx

# usbd_usbd.c supplies a loader that embeds DEV9 and PS2SDK's BDM-enabled
# ATAD. POPStarter still receives one self-contained driver file.
IOP_OBJS = usbd_usbd.o dev9_blob.o atad_blob.o imports.o exports.o

include ../global.make

$(IOP_OBJS_DIR)dev9_blob.o: $(DEV9_IRX)
	$(DIR_GUARD)
	$(PS2SDK)/bin/bin2c $< $(IOP_OBJS_DIR)dev9_blob.c bdm_hdd_dev9_irx
	$(IOP_C_COMPILE) -c $(IOP_OBJS_DIR)dev9_blob.c -o $@

$(IOP_OBJS_DIR)atad_blob.o: $(ATAD_IRX)
	$(DIR_GUARD)
	$(PS2SDK)/bin/bin2c $< $(IOP_OBJS_DIR)atad_blob.c bdm_hdd_atad_irx
	$(IOP_C_COMPILE) -c $(IOP_OBJS_DIR)atad_blob.c -o $@

$(IOP_OBJS_DIR)usbd_usbd.o: src/usbd/usbd_usbd.c
	$(DIR_GUARD)
	$(IOP_C_COMPILE) -c $< -o $@
