# 源码取自本地Open-PS2-Loader的SMSTCPIP模块，并包含与smbman配套的分离接收接口。
# 此处固定使用游戏运行期配置，确保生成的ps2ip.irx与已验证版本保持一致。

INGAME_DRIVER ?= 1
INTERRUPT_CTX_INPKT ?= 1
FULL_LWIP ?= 0

IOP_BIN = ../ps2ip.irx
IOP_OBJS = ps2ip.o inet.o ip.o ip_addr.o ip_frag.o etharp.o tcp_in.o tcp_out.o \
	tcp.o tcpip.o mem.o api_lib.o api_msg.o sockets.o netif.o udp.o memp.o \
	icmp.o pbuf.o timers.o exports.o imports.o
IOP_LIBS =

IOP_CFLAGS += -DLWIP_NOASSERT
IOP_CFLAGS += -DPS2IP_ENABLE_SELECT

ifeq ($(INGAME_DRIVER),1)
IOP_CFLAGS += -DINGAME_DRIVER
endif

ifeq ($(INTERRUPT_CTX_INPKT),1)
IOP_CFLAGS += -DINTERRUPT_CTX_INPKT
endif

ifeq ($(FULL_LWIP),1)
IOP_CFLAGS += -DFULL_LWIP
endif

include ../../global.make

ifneq ($(IOP_CC_VERSION),3.2.2)
ifneq ($(IOP_CC_VERSION),3.2.3)
IOP_CFLAGS += -mno-memcpy
# 该旧版lwIP在新编译器中会产生已知的非功能性警告。
IOP_CFLAGS += -Wno-attributes -Wno-array-bounds -Wno-unused-but-set-variable
endif
endif
