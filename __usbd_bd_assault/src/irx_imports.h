/*
 * Defines all IRX imports.
 *
 * Note: DEV9 is linked into this IRX — do not import the "dev9" library.
 * ATAD calls DEV9 symbols by direct linkage. BDM comes from usbd.irx.
 *
 * 不能同时#include ioman.h与iomanX.h（静态内联符号冲突），
 * ioman的import宏在此手写；业务代码里各自只包含需要的头文件。
 */

#ifndef IOP_IRX_IMPORTS_H
#define IOP_IRX_IMPORTS_H

#include <irx.h>

#include <bdm.h>
#include <dmacman.h>
#include <intrman.h>
#define IOMANX_OLD_NAME_ADDDELDRV 0
#define IOMANX_OLD_NAME_COMPATIBILITY 0
#include <iomanX.h>
#include <loadcore.h>
#include <stdio.h>
#include <sysclib.h>
#include <thbase.h>
#include <thevent.h>
#include <thsemap.h>

/* 手写ioman import表，避免与iomanX头文件冲突 */
#define ioman_IMPORTS_start DECLARE_IMPORT_TABLE(ioman, 1, 1)
#define ioman_IMPORTS_end   END_IMPORT_TABLE
#define I_open   DECLARE_IMPORT(4, open)
#define I_close  DECLARE_IMPORT(5, close)
#define I_write  DECLARE_IMPORT(7, write)
#define I_dopen  DECLARE_IMPORT(13, dopen)
#define I_dclose DECLARE_IMPORT(14, dclose)

#endif /* IOP_IRX_IMPORTS_H */
