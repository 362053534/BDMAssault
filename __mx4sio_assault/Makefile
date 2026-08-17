# 从本目录的 src/ 构建 MX4SIO BDM 驱动。
# 分区识别和 FAT32/exFAT 文件系统仍由 usbd.irx 提供。

IOP_BIN = mx4sio.irx
MX4SIO_BIN = ../mx4sio.irx

IOP_INCS += -I../bdm/include

IOP_IMPORT_INCS += \
	fs/bdm \
	sio/sio2man \
	system/dmacman \
	system/intrman \
	system/ioman \
	system/loadcore \
	system/sio2man \
	system/stdio \
	system/sysclib \
	system/threadman

IOP_LIBS += -lgcc

IOP_OBJS += spi_sdcard_crc7.o spi_sdcard_driver.o crc16.o ioplib.o sio2man_hook.o mx4sio.o imports.o

include $(PS2SDKSRC)/Defs.make
include $(PS2SDKSRC)/iop/Rules.bin.make
include $(PS2SDKSRC)/iop/Rules.make
include $(PS2SDKSRC)/iop/Rules.release

all:: $(MX4SIO_BIN)

$(MX4SIO_BIN): $(IOP_BIN)
	cp $< $@

clean::
	rm -f $(MX4SIO_BIN)
