/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2009, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
#
# Defines all IRX imports.
*/

#ifndef IOP_IRX_IMPORTS_H
#define IOP_IRX_IMPORTS_H

#include <irx.h>

#include <intrman.h>
#include <ioman.h>
#include <ps2ip.h>
#include <sifman.h>
#include <stdio.h>
#include <sysclib.h>
#include <thbase.h>
#include <thsemap.h>

// 使用PS2SDK 2.4兼容接口，同时兼容POPStarter现有的ps2smap驱动。
#undef ps2ip_IMPORTS_start
#undef ps2ip_IMPORTS_end
#define ps2ip_IMPORTS_start DECLARE_IMPORT_TABLE(ps2ip, 2, 4)
#define ps2ip_IMPORTS_end   END_IMPORT_TABLE

int lwip_recvsplit(int s, void *header, int index, void *payload, int plen, unsigned int flags);

#define ps2split_IMPORTS_start DECLARE_IMPORT_TABLE(ps2split, 1, 1)
#define ps2split_IMPORTS_end   END_IMPORT_TABLE
#define I_lwip_recvsplit       DECLARE_IMPORT(4, lwip_recvsplit)

#endif
