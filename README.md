About:
======
Atrium Firmware is software stack for the ESP family of controllers from Espressif. It provides ready to build configuration (e.g. for S20 socket) and supports ESP8285, ESP8266, and ESP32 in one source code package, providing infrastructure to build for both controller architectures.

The intended audience of this distribution is embedded systems developers that intend to provide open source solutions for ESP based hardware.

Therefore, it employs Espressif's SDKs and wraps the build environment of the SDKs for transparent switching, and all features of the application stack can be configured together with the SDK configuration.


Features:
=========
Software services:
- timed actions based on time, date, day of week, and holiday settings
- timed failover from station to accesspoint WiFi mode
- OTA via http download
- ftp server for access to spiffs/fatfs
- MQTT integration (ESP library only)
- data transmission to InfluxDB via UDP
- inet server
- http server with system configuration support
  (this is not the one from Espressif SDK)
- sntp with timezone support for CET
- mdns support
- name-service lookup support
- telnet server
- console shell with many commands
- persistent system configuration handling
- syslog support for sending log messages to a syslog server
- WPS support on ESP32 (ESP8266 seems to have an SDK bug)
- SmartConfig support (compatible hardware required)

Filesystem support:
- fatfs on ESP32
- SPIFFS
- ROM filesystem support for shared data across OTA partitions
- memfile support
  (in data segment stored data files, accessible as C strings)

Hardware drivers:
- DHTxx temperature/humidity sensor support
- BME280 temperature/humidity/pressure sensor support
- single button support with debouncing
- single relay control support
- alive status LED support with different blinking schemes
- 7 segment LED support via MAX7219
- TLC5947 and TLC5916 constant current LED driver support
- color LED strip support for WS2812b based strips
- webcams supported in https://github.com/espressif/esp32-camera


Getting started:
================
To prepare the build environment, run "./setupenv.sh", which will:
- check some prerequisites that need to be installed manually
- download the xtools for lx106 (esp8266) and for esp32
- download and compile wire-format-compiler

To build a project, run:
```
> make PROJECT=<project-name>
```
This will build the project in a subdirectory called `build.<project>`.

To flash a target, run:
```
> make PROJECT=<project-name> flash
```

To build for live update, always do a clean build, by running:
```
> rm -r build.<project-name>
> make PROJECT=<project-name> ota
```



Project Configuration:
======================
To configure a specific project run:
```
make PROJECT=<project> menuconfig
```

Select _components_, then select _application_. Here you can configure software
and hardware feature and specify which type of file-system should be used.


Accessing and configuring a device:
===================================
After flashing a device, the device defaults to Soft-AP mode. I.e. it opens up its own WiFi without password, and you can access the device at the IP address `192.168.4.1`. Pointing your browser to `http://192.168.4.1/config.html` will bring up the config page. Per default no password is set, so leave it empty, but set a new password by filling the two new password fields with an appropriate password (10-15 characters). Also set the ssid of your WiFi and its password. After saving the configuration, the ESP will access the new WiFi and ask via DHCP for its IP address.

After that you can access the device in your own WiFi. Sometimes switching the WiFi might not work out of the box. In that case it might be necessary to power-cycle the device.

Telnet and the UART console provides a list of command that can be queried via the `help` command. After setting a password, you need to change to the admin privilege level by using `su 1` and entering the password.

Recommended steps to set up the configuration at the beginning are:
```
> station ssid <wifi-ssid>
> station pass <wiif-password>
> station on
> ap off
> set dns\_server <dns>
> set sntp\_server <sntp>
> set timezone <timezone offset or CET>
> passwd -s
> su 1
> config write
> reboot
```

Additionally, you might want to set up MQTT and InfluxDB. Enter `help` to get a list of all available commands. To get a list of available settings, enter `set -l`. E.g.:
```
> mqtt uri mqtt://<servername>:1883
> mqtt enable
> influx <servername>:8089/<database>
```

Rebooting is necessary for certain settings to work properly. E.g. sntp
has problems when changed during runtime. Other services like MQTT do
not suffer such limitations.


Preconfigured Projects:
=======================
s20:           S20 socket with OTA, webserver, MQTT, and everything else
               that fits on the intrnal esp8285, with English text.
termserv:      Terminal server configured for D1 Lite with OTA, telnet, and more.
	       Use the con command after telnetting to the device to get a
               terminal connection.
dht\_term:     terminal server with DHT driver
dht\_tracker:  esp8266 with DHT support for temperature/humidity logging with
               udp casting
htt:           esp32 with DHT support for temperature/humidity logging with udp
               casting
lightctrl:     light sensor based dimmer
dimmer:        LED PWM dimmer
nino1:         esp8266 with ws2812b based ledstrip
nino2:         esp32 with ws2812b based ledstrip
clock:         esp32 with 8 7-segment LEDs


Eval Board Configurations:
==========================
d1\_mini:      esp8266 for the Wemos D1 Mini board test setup
lolin\_d32:    esp32 for the Lolin D32 board test setup


Flashing an S20 Device the first time:
======================================
To flash an S20 device, do the following steps:
- First, disconnect the device from the power socket!
- Then read the whole procedure before starting.
- Open up the device and connect the serial flasher with 3V3, GND, RX, TX.
- Press and hold the button, then power up the device by connecting the serial flash to the USB port of your laptop.
- Release the button and execute `make PROJECT=s20 flash`
- Now the bootloader, partition-table, initial OTA data, app1, and romfs will be flashed.

Now the device is ready to go. So reassemble it before connecting it to a power socket. Future updates can be done with the OTA procedure.


Security Concepts:
==================
- Access to configuration parameters is restricted by admin password.
- Admin password is stored as hash value in NVS.
- If you forget the password, you can trigger a factory reset to reset
  all conifguration parameters and erase the password. For S20 this can
  be achieved by pressing the button and releasing it after more than 10s.
  For devices without button you must implement a factory reset trigger
  yourself or clear the NVS with the esptool.
- WIFI softap/station passwords are stored as clear text in NVS.
- WIFI passwords are blanked out when config is made available without
  admin password (e.g. for config web page).
- User-level can modify all timed actions and holidays. It is assumed
  that all users that can access the WIFI network should be able to use
  the device. This may change in the future.


Security Recommendations:
=========================
- enable station mode
- disable access-point mode
- set password
- enable station to access-point failover mode only if the device acts
  as a pure sensor


Security Considerations:
========================
- password based access via telnet and http are prone to eavesdropping
- Atrium has no flash or update protection right now


RAM considerations:
===================
Even the ESP8285 can be loaded with telnet, http, mqtt, uart console, and more, when it comes to required ROM. But be aware that the RAM might not be enough in certain situations. So you cannot expect to have multiple http connections active at the same time while MQTT is used, too. MQTT alone will need more than 10k of RAM.


Smart Config:
=============
SmartConfig can be triggered from the home-page of the S20. Just point your browser to 192.168.4.1. Keep in mind that you have to switch the WiFi back to the one you want to configure on the ESP device in order for SmartConfig to work.

In my experience SmartConfig is seldomly successful on ESP8266. It works better on ESP32 devices. So the recommended way of setting up a device is to go to `http://192.168.4.1/config.html` instead or to use telnet.


Flashing a device:
==================
To initially flash your device, you can trigger the flash procedure with:
```
make PROJECT=<projectname> flash
```

This will flash the bootloader, partition table, primary application, and data/romfs partition if applicable. After flashing the first time, you can update the applicaton with:
```
make PROJECT=<projectname> app-flash
make PROJECT=<projectname> romfs-flash
```


Over-the-Air update:
====================
To perform an OTA update, you have to build your project with:
```
make PROJECT=<projectname> ota
```
This produces firmware binaries that are to be used for the relevant partitions.

If the project has a ROMFS partition, you will also need to execute: make
```
PROJECT=<projectname> romfs
```

To update via the integrated webserver, just point your browser to the IP of the device on page `/update.html`. E.g. `http://192.168.1.89/update.html`.  Always update the romfs/data partition first, and then the application, because a successful application update will trigger the reboot which will require an updated romfs partition.

To update via telnet you need to perform the following steps:
1. upload all necessary files for download to a webserver.

2. telnet into your device.

3. If you have configured a password, then you must execute `su 1` to get the appropriate privileges.

4. By executing `boot` on the console you can see on which partition the device is running currently and which partition will be used for an update. 

5. By executing `update http://server/path/firmware.app1.bin` you can trigger a download that will update the currently inactive partition.  Please activate the partition only with `boot app1` or so, after the update was successful. If update returns with an error, the update failed, and booting from the relevant partition will most likely not be successful. To get more information about the reason for failure, you can execute `dmesg` to see the last message that were generated during the OTA procedure. If the update ran out of memory, you might want to try to reboot before restarting the update again.

6. If the relevant project (e.g. s20) has configured a ROMFS partition, then you may also need to update this partition as well. For this execute: `update -r http://server/path/romfs.bin`

7. When flash updates were successful, switch the boot partition to the updated partition. E.g.: `boot app2`

8. Finally, trigger a reboot, by executing `reboot`.

Never switch boot partition if updating returned an error.  If your system doesn't boot anymore, you will need to flash via serial boot loader.


Known Issues:
=============
- WPS may crash, and its IDF support seems to be broken in several ways
- PWM dimmer of ESP8266 may cause flickering output
- S20/ESP8266 OTA scheme with 1MB ROM works with release/v3.2 but fails
  with release/v3.3
- OTA generation seems to have issues with 4MB flash size for ESP8266.
  The root cause seems to be in the IDF. The issue does not occur on
  ESP32 or with 1MB flash size.
- CMake based builds do not support OTA image generation


Interface Stability:
====================
- Currently all interfaces are evolving.
- Interfaces that show a certain level of stability may be documented in future.


Remarks on terminal server:
===========================
As the ESP8266 has only one UART/rx port, but two UART/tx ports, the terminal server will only run, if the UART console is disabled. This is done automatically in the code at compile-time. Nevertheless, logging to UART0/tx will work. In consequence access to the UART terminal is only possible via telnet.

The ESP32 does not suffer the same restriction. I.e. dedicated UARTs can be allocated for the terminal server and the console, so that these features won't interfere with eachother.


Stating a new project:
======================
To start a new project, choose any of the available project configuration files in the projects subdirectory, and run a `make PROJECT=<projectname> menuconfig`. After exiting the configuration menu, the updated configuration will be saved in the named porject file.


Developer hints:
================
- MDNS requires IPv6 to work. The combined footprint is ~50k. If you don't need IPv6 for anything else, disabling MDNS and IPv6 will save you this amount of ROM. This is probably only a point to consider for devices with limited ROM, such as ESP8285 based systems without external flash.  


Bugs:
=====
If you want to report a bug, please do this via the github.com project page.
