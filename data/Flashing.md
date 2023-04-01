Binary distribution of Atrium Firmware for ESP devices.
Copyright 2021-2023, Thomas Maier-Komor
https://githup.com/maierkomor/atrium
==========================================================

This package contains binary images for flashing on following ESP
controllers in its subdirectories:
- esp8285: i.e. ESP8266 with 1MB internal flash
- esp8266\_4m: ESP8266 with 4MB external flash
- esp32\_4m: ESP32 with 4MB external flash
- esp32-c3\_4m: ESP32-C3 with 4MB external flash
- esp32-s2\_4m: ESP32-S2 with 4MB external flash
- esp32-s3\_8m: ESP32-S3 with 8MB external flash

Flash the binary images at the address they include in their filenames.
The application binary for the ESP32 can be flashed to any partition,
which is large enough, and which is tagged as bootable for the
bootloader.

To flash the images, you can use the included flash-atrium.sh or update-atrium.sh script. The `flash-atrium.sh`will erase the whole device - so use it only for flashing a new device the first time, because it will erase the configuration, which will be kept, if you use the update-atrium.sh script.

After flashing the device, use the serial console to setup the device.
The S20 socket can also be configured via its internal access-point
using a web browser pointed to 192.168.4.1, if the ROMFS image has been
flashed.

Refer to the README.md for detailed instructions on configuration and
use of Atrium.
