# ESP-01S STM32 OTA gateway

1. Copy `include/local_config.h.example` to `include/local_config.h` and fill
   Wi-Fi, HTTPS manifest URL, and the server CA certificate.
2. Install PlatformIO and run `pio run`.
3. Flash the ESP-01S over UART0. Disconnect or isolate the STM32 UART while
   flashing the ESP module.

The environment is fixed to a 1 MiB ESP-01S layout with 256 KiB LittleFS and no
ESP self-OTA slot. The STM32 image and signed manifest stay cached until the
new STM32 application sends `APP_BOOT_OK`.

Never replace CA validation with `setInsecure()`. SNTP must succeed before the
gateway opens an HTTPS connection.
