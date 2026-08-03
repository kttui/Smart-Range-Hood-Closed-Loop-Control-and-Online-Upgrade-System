# ESP8266 → STM32 OTA deployment

## Hardware

- ESP-01S GPIO1/TX connects to STM32 PA3/USART2_RX.
- ESP-01S GPIO3/RX connects to STM32 PA2/USART2_TX.
- Grounds must be common. Both UARTs are 3.3 V; do not connect 5 V logic.
- Power the ESP-01S from a stable 3.3 V supply rated for at least 500 mA. Place
  10 μF and 0.1 μF decoupling close to the module.
- Keep EN and GPIO0 pulled high for normal boot. Add a jumper or series
  isolation so the STM32 does not drive the UART while the ESP is being flashed.

The ESP and STM32 share the switched appliance power domain. A fully powered
off appliance cannot receive an update; it checks immediately on the next boot.

## One-time provisioning

1. Build `Bootloader/Bootloader.uvprojx` and the application `Project.uvprojx`.
2. Run the release command documented in `Release/README.md`.
3. Program `Release/firmware/factory_bundle.hex` with ST-Link. It contains the
   Bootloader at `0x08000000`, application at `0x08008000`, and signed metadata
   at `0x0803F000`.
4. Copy `ESP8266_Gateway/include/local_config.h.example` to `local_config.h`,
   enter Wi-Fi/HTTPS/CA values, then build and flash the PlatformIO project.

Changing `Common/ota_public_key.h` requires rebuilding and physically reflashing
the Bootloader. Never upload `Release/private_key.pem` to the web server.

## Publishing an update

1. Increase `OTA_APP_VERSION_CODE` in `Common/ota_config.h` and rebuild the app.
2. Run `tools/release_firmware.py release` with matching semantic and numeric
   versions and the final HTTPS base URL.
3. Upload the versioned STM32 `.bin` first, then atomically replace
   `manifest.json`. Configure `manifest.json` as `Cache-Control: no-cache`.
4. When the hood is on, ESP checks at boot and every 30 minutes. It downloads
   and validates the image, while STM32 installs only in OFF mode with a stopped
   motor. The cached image is deleted only after `APP_BOOT_OK`.

## Recovery behaviour

- Packet CRC, offsets, per-packet ACK and five retries make DATA retransmission
  idempotent.
- A PENDING metadata generation prevents a partially overwritten app from
  booting. After power returns, ESP retransmits the cached complete image.
- After three application boots without `APP_BOOT_OK`, Bootloader stays in
  recovery mode for a newer signed image. There is no old-image rollback.
