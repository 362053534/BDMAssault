/*
 * Defines all IRX imports.
 *
 * Note: DEV9 is linked into this IRX — do not import the "dev9" library.
 * ATAD calls DEV9 symbols by direct linkage. BDM comes from usbd.irx.
 *
 * POPStarter重启IOP后通常无iomanX，本模块只依赖rom0:ioman。
 */

#ifndef IOP_IRX_IMPORTS_H
#define IOP_IRX_IMPORTS_H

#include <irx.h>

#include <bdm.h>
#include <dmacman.h>
#include <intrman.h>
#include <ioman.h>
#include <loadcore.h>
#include <stdio.h>
#include <sysclib.h>
#include <thbase.h>
#include <thevent.h>
#include <thsemap.h>

#endif /* IOP_IRX_IMPORTS_H */
