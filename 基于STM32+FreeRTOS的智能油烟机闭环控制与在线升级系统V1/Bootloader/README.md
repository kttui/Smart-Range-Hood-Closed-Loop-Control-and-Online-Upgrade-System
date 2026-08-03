# STM32 OTA Bootloader

This target is intentionally independent from FreeRTOS and the application.

## Build

Open `Bootloader.uvprojx` and build the independent `Bootloader` target, or run
`build_bootloader.bat`. The batch file uses the Keil ARMCC toolchain under
`D:\Keil_v5` by default; set `KEIL_ROOT` when Keil is installed elsewhere.
Outputs are placed in `Bootloader\Objects`.

Memory layout is fixed by `Bootloader.sct`: bootloader 32 KiB at `0x08000000`,
application at `0x08008000`, and redundant metadata pages at `0x0803F000` and
`0x0803F800`.

The first factory flash consists of:

1. `Bootloader.hex` at `0x08000000`.
2. The application binary at `0x08008000`.
3. `factory_metadata.bin` from the release tool at `0x0803F000`.

The bootloader never accepts an image until its ECDSA-P256 signature has been
verified against `Common/ota_public_key.h`.
