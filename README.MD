# BDM Assault

BDM Assault is a homebrew project for PS2 that aims to bring EXFAT USB (and optional ATA HDD) support to those old closed source homebrew apps that can load external USB drivers that people still use today (POPStarter, FreeMcBoot).

## Build products

| File | Source tree | Role |
|------|-------------|------|
| `usbd.irx` | `__bdm_assault` | BDM + `mass:` (exFAT/FAT) filesystem |
| `usbhdfsd.irx` | `__usbd_bd_assault` | USB host + USB mass storage block device |
| `bdmhdd.irx` | `__bdmhdd_assault` | Internal HDD (DEV9 + ATAD with BDM); optional |

USB path needs `usbd.irx` + `usbhdfsd.irx`.
HDD path needs `usbd.irx` + `bdmhdd.irx` (HDD must be MBR/GPT + FAT/exFAT, not APA-only).

## Usage

Place the IRX binaries from the [release section](https://github.com/israpps/BDMAssault/releases) into the paths your app looks for.

### POPStarter
- `usbd.irx` → `mc?:/POPSTARTER/usbd.irx`
- `usbhdfsd.irx` → `mc?:/POPSTARTER/usbhdfsd.irx`
- `bdmhdd.irx` → by caller convention (e.g. OPL HDD slot mapping)

### FreeMcBoot
- `usbd.irx` → `mc?:/SYS-CONF/USBD.IRX`
- `usbhdfsd.irx` → `mc?:/SYS-CONF/USBHDFSD.IRX`
- `bdmhdd.irx` → by caller convention

### FreeHdBoot
- `usbd.irx` → `hdd0:__sysconf/FMCB/USBD.IRX`
- `usbhdfsd.irx` → `hdd0:__sysconf/FMCB/USBHDFSD.IRX`
- `bdmhdd.irx` → by caller convention

### old wLaunchELF versions
- `usbd.irx` / `usbhdfsd.irx` / `bdmhdd.irx` → the paths you configured

### Any other homebrew
- find out by yourself. I cant remember any other.

## Incompatible programs

- Simple Media System (SMS): incompatible due to usage of custom USB drivers and practices wich are not common amongst the homebrew devs. support not planned (and most likely not even possible)

## CREDITS

the softwares found here were taken and modified from [__PS2SDK__](https://github.com/ps2dev/ps2sdk)

- krHACKen: for letting me know POPStarter loads external USB drivers (wich ended up in the creation of this project)
- juankytutoriales: for making tests in real hardware
- everyone that collaborated to PS2SDK
