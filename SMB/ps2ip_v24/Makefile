# 使用与POPStarter现有ps2smap兼容的PS2SDK 2.4网络栈。

IOP_BIN = ../ps2ip.irx
IOP_OBJS = ps2ip.o sys.o sys_arch.o init.o exports.o imports.o mem.o netif.o pbuf.o \
	stats.o tcp_in.o tcp_out.o udp.o memp.o tcp.o etharp.o raw.o def.o timers.o \
	dhcp.o dns.o netdb.o api_lib.o api_msg.o err.o sockets.o tcpip.o netbuf.o \
	icmp.o ip.o ip_addr.o ip_frag.o inet_chksum.o inet.o
IOP_LIBS =

IOP_INCS += -Isrc/include -Isrc/include/ipv4
IOP_CFLAGS += -DLWIP_NOASSERT -DLWIP_COMPAT_MUTEX -DPS2IP_DHCP -DPS2IP_DNS

include ../../global.make

ifneq ($(IOP_CC_VERSION),3.2.2)
ifneq ($(IOP_CC_VERSION),3.2.3)
IOP_CFLAGS += -mno-memcpy
IOP_CFLAGS += -Wno-attributes -Wno-array-bounds -Wno-unused-but-set-variable
endif
endif
