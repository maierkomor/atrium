Binary distribution of Atrium Firmware for ESP devices.
Copyright 2021, Thomas Maier-Komor
https://githup.com/maierkomor/atrium
==========================================================

This package contains binary images for flashing on following ESP
controllers in its subdirectories:
- esp8285: i.e. ESP8266 with 1MB internal flash
- esp8266\_4m: ESP8266 with 4MB external flash
- esp32\_4m: ESP32 with 4MB external flash

Flash the binary images at the address they include in their filenames.
The application binary for the ESP32 can be flashed at any location, but
valid partitions are at 0x100000 and 0x280000.

The directory of esp8285 also includes a hardware configuration file for
the Sonoff S20 socket. Please setup the software manually.


To flash a device for the first time to Atrium, do the following:
1) erase the flash:
```
esptool.py --port /dev/ttyUSB0 erase_flash
```
2) write the following segments:
- 0x0 boot@0x0000.bin
- 0x8000 ptable-esp8285@0x8000.bin
- 0x9000 nvs@0x90000.bin	(if availabe, can be generated with atriumcfg)
- 0x10000 atrium@0x10000.app1
- 0xf0000 romfs@0xf0000.bin

E.g.:
```
esptool.py --port /dev/ttyUSB0 write_flash 0x0 boot@0x0000.bin 0x8000 ptable-esp8285@0x8000.bin 0x10000 atrium@0x10000.app1 0xf0000 romfs@0xf0000.bin
```

After flashing the device, use the serial console to setup the device.
The S20 socket can also be configured via its internal access-point
using a web browser pointed to 192.168.4.1, if the ROMFS image has been
flashed.

To update a device running Atrium, either do the update via
/update.html webpage of the device, or telnet to the device and update
via server download. I.e.
- first get super user rights by executing `su 1`
- update the data slice: `update -r http://server.ip/path-to-file/romfs.bin`
- next look to which partition the update will go using `boot`
- the trigger the update by executing
`update http:://server.ip/path-to-file/atrium.appx`
Where x is the appropriate file for the relevant partition. If you have
set dns\_server then you can also use a hostname instead of an IP
adress.
