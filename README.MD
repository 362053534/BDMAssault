# BDM Assault

BDM Assault is a homebrew project for PS2 that aims to bring EXFAT USB support to those old closed source homebrew apps that can load external USB drivers that people still use today (POPStarter, FreeMcBoot).

## Usage

To use this project you have to place the two IRX binaries provided at the [release section](https://github.com/israpps/BDMAssault/releases) into the path that your app looks for


### POPStarter
- `usbd.irx` → `mc?:/POPSTARTER/usbd.irx`
- `bdmhdd.irx` → 由 OPL 等调用方按约定加载（内部再映射到对应槽位）

On the ATA_TEST / BDM-HDD build, `bdmhdd.irx` brings up internal HDD (DEV9+ATAD with BDM) instead of USB.
Both files are still required: `usbd.irx` provides BDM + `mass:` (exFAT), `bdmhdd.irx` attaches the ATA disk.
The HDD must be MBR/GPT + FAT/exFAT (not APA-only). USB mass is not available in that build.

### FreeMcBoot
- `usbd.irx` → `mc?:/SYS-CONF/USBD.IRX`
- `bdmhdd.irx` → 由调用方按约定放置/映射

### FreeHdBoot
- `usbd.irx` → `hdd0:__sysconf/FMCB/USBD.IRX`
- `bdmhdd.irx` → 由调用方按约定放置/映射

### old wLaunchELF versions
- `usbd.irx` → the path you configured for `USBD.IRX`
- `bdmhdd.irx` → 由调用方按约定放置/映射

### Any other homebrew
- find out by yourself. I cant remember any other.

## Incompatible programs

- Simple Media System (SMS): incompatible due to usage of custom USB drivers and practices wich are not common amongst the homebrew devs. support not planned (and most likely not even possible)

## CREDITS

the softwares found here were taken and modified from [__PS2SDK__](https://github.com/ps2dev/ps2sdk)

- krHACKen: for letting me know POPStarter loads external USB drivers (wich ended up in the creation of this project)
- juankytutoriales: for making tests in real hardware
- everyone that collaborated to PS2SDK
