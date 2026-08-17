
# 成品名：输出 usbd.irx（由 OPL 等调用方按约定加载/映射）
IOP_BIN = ../usbd.irx


#bdm
BDM_OBJS = main.o bdm.o part_driver.o part_driver_mbr.o part_driver_gpt.o
IOP_LIBS += ../libbdm/lib/libbdm.a
IOP_CFLAGS += -I../libbdm/include/ -Wno-strict-aliasing

#bdmfs_fatfs
BDMFS_FATFS_OBJS = ff.o ffsystem.o ffunicode.o fs_driver.o diskio.o main.o
IOP_LIBS += -static-libgcc -lgcc

IOP_OBJS = $(addprefix bdm_,$(BDM_OBJS)) $(addprefix bdmfs_,$(BDMFS_FATFS_OBJS)) imports.o exports.o
ifeq ($(DEBUG), 1)
  IOP_CFLAGS += -DDEBUG -DDEBUG_EXTREME
  IOP_BIN = ../usbd_debug.irx
endif
include ../global.make
IOP_SRC_DIR =
vpath %.c src/
vpath %.lst src/
vpath %.tab src/
vpath %.c src/bdm/
vpath %.c src/bdmfs/


$(IOP_OBJS_DIR)%.o: src/bdm/%.c
	$(DIR_GUARD)
	$(IOP_C_COMPILE) -c $< -o $@
$(IOP_OBJS_DIR)%.o: src/bdmfs/%.c
	$(DIR_GUARD)
	$(IOP_C_COMPILE) -c $< -o $@