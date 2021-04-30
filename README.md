About Atrium Firmware:
======================
Atrium Firmware is software stack for the ESP family of controllers from Espressif. It provides ready to build configuration (e.g. for S20 socket) and supports ESP8285, ESP8266, and ESP32 in one source code package, providing infrastructure to build for both controller architectures.

The intended audience of this distribution are enthusiasts and hobbyists that want a home-automation without cloud access.

Atrium firmware is designed to be cover as many applications with one flash image as possible. Therefore, its drivers are initialized while booting. The relevant configuration can be performed with an off-line tool or online in the device if the flash image supports it. The settings can be uploaded to the device via telnet or directly flashed to its NVS (non-volatile-stroage).

After booting the configured drivers provide events, actions, and timer that can be associated to provide additional functionality. Configuring actions, events, and timers can be done online, and is stored among the regular device configuration in the NVS.


Features:
=========
Software services:
- fully costomizable with timers, events, and actions
- SNTP timed actions with day-of-week and holiday settings
- over-the-air (OTA) updates via http upload and download
- OTA aware, persistent system configuration
- MQTT integration (LWIP library only - buggy on ESP8266)
- data transmission to InfluxDB via UDP
- tool to send commands to all devices on the network
- http server with system configuration support
  (this is not the one from Espressif SDK)
- timezone support for CET with daylight-saving convention
- ftp server for access to spiffs/fatfs
- name-service lookup support
- telnet server and console shell with many commands
- inet server
- syslog support for sending log messages to a syslog server
- serial monitor to forward uart to syslog
- WPS support
- SmartConfig support (compatible hardware required)

Filesystem support:
- fatfs on ESP32
- SPIFFS
- ROM filesystem support for shared data across OTA partitions
- memfile support
  (in data segment stored data files, accessible as C strings)

Hardware drivers:
- button support with debouncing
- relay control support
- BME280 temperature/humidity/pressure sensor support
- DHTxx temperature/humidity sensor support
- DS18B20 temperature sensor support
- alive status LED support with different blinking schemes
- 7 segment LED support via MAX7219
- TLC5947 and TLC5916 constant current LED driver support
- color LED strip support for WS2812b based strips
- webcams supported in https://github.com/espressif/esp32-camera
- 1-wire support (currently only DS18B20)


Getting started:
================
To get started, you need to perform following steps:
1) Erase the device's flash memory: `esptool erase_flash`
2) Flash bootloader, partition table, app-image, and
optionally a romfs-image accoring to the partition table's configured
addresses:
```
esptool write_flash 0x0000 bootloader.bin
esptool write_flash 0x8000 esp8285-ptable.bin
esptool write_flash 0x10000 atrium.app1
esptool write_flash 0xf0000 romfs.bin
```
For ESP32 you need to flash `atrium.bin` instead of `atrium.app1`.

3) Configure the device via the offline tool bin/atriumcfg or via the
serial console (hardware only if supported by the app-image)
4) Write the configuration to the NVS of the device (either directly on
the device's serial console or using bin/atriumcfg)


Device Configuration:
=====================
The configuration of an Atrium device is stored in the
non-volatile-storage (NVS), which is a dedicated partition of the flash,
and it consists of software, hardware, and WiFi calibration settings.
The software configuration consists of WiFi, network configuration,
etc.. The hardware configuration defines the GPIOs and settings of the
drivers for attached hardware. The WiFi calibration is opaque to the
user and cannot be modified manually.

All configuration settings can be modified either online on the device
(for hardware settings, if compiled in) or with the `atrimcfg`
configuratoin tool. The atriumcfg can either write binary config files
(usually using the .cfg extension), produce a complete partition image
for the NVS partition, or write the partition image directly to the
device using the esptool.

For creating the NVS partition, atriumcfg needs to know the apropriate
path of either ESP8266 or ESP32 IDF. Unfortunately, the NVS partition
are not compatible with eachother, so you need to select the correct IDF
using the `idf` command of atriumcfg.

Once a device is configured, it will keep the configuration accross
firmware updates, unlees you erase the NVS partition. The Atrium
firmware will read the settings from NVS, and the binary format of the
configuration is designed for forward and backward compatibility. I.e.
up- or down-grading a firmware will not conflict with the device
configuration.

As hardware configuration may not be supported (and needed) on the
target device, there is an additional way of updating the hardware
configuration on a device. For this execute `xxd -p hw_config.cfg` to
print the hex representation of the configuration. After this, access
the device via serial console or telnet and execute `hwconf parsexxd`
and paste the hexcode to the connection, and hit enter. If the parsing
is successful execute `hwconf writebuf` to write the hardware
configuration to the NVS. This way the device hardware coding can be
changed without hwconf support on the device.

To create a hardware or software configuration start `atriumcfg` and
switch to either software configuration with the `sw` command or
hardware configuration using the `hw` command. After that you can
execute `show` to list all valid fields of the configuration. To add an
element to an array of settings, use the `add` command. To set a
parameter use the `set` command. Once you are done configuring, use
`write <filename>` to write the current hw or sw config to a file or use
`updatenvs` to write the configuration to the device. The command
`updatenvs` will read the partition table to write the configuration to
the appropriate address. I.e. this works only after the device has been
initialized with a partition table.


Buliding Atrium yourself:
=========================
To prepare the build environment, run "./setupenv.sh", which will:
- check some prerequisites that need to be installed manually
- download the xtools for lx106 (esp8266) and for esp32
- download and compile wire-format-compiler
- patch the IDF trees to include the LWIP-MQTT library
- satisfy Python requirements of IDF using pip

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

Required Tools:
===============
- On Windows:
	- Flash download tool:
		- Download at: https://www.espressif.com/en/support/download/other-tools
		- https://www.espressif.com/sites/default/files/tools/flash\_download\_tool\_v3.8.5.zip
- On Linux:
	- IDF for ESP32 or ESP8266 respectively


Building a flash image:
=======================
To build the flash image of a specific project run:
```
make PROJECT=<project>
```

Select _components_, then select _application_. Here you can configure software and hardware feature and specify which type of file-system should be used.


Offline configuration:
======================
The recommended way of setting up a new device is to perform an offline configuration with the atriumcfg tool. That way a configuration can be stored in a file and flashed to multiple devices. Hardware configuration on a device is only possible of the relevant option is enabled for the target. Small targets may want to exclude its support to save some flash size (e.g. the d1\_lite project).

The following steps are recommend to perform the software configuration. Replace elements in brackets `<>` with the approriate values.
```
> set station.ssid <wifi ssid>
> set station.pass <wifi pass>
> set dns_server <ip address>
> set sntp_server <sntp-server>
> set timezone <timezone offset or CET>
> set domainname <domainname>
```

For a specific target you may also want to set the nodename (hostname). You can store the configuration in a binary file and load and flash it to multiple targets. Hardware and software configurations are stored in different files by atriumcfg for that purpose.

To configure hardware setting, switch to hardware configuration mode by issueing a `hw` command. See section on antriumcfg below on how to perform hardware configuration.

After the configuration is complete, prepare for flashing the NVS partitioni. As NVS format and handling differs between ESP8266 and ESP32, you need to select the appropriate IDF. Use the `idf` command to set the path of the appropriate IDF. After that use the `updatenvs` command to update the NVS partition.

Atrium Config Tool atriumcfg:
=============================
The Atrium Config Tool atriumcfg is a tool for configuring hardware and
software of an Atrium device and can be used to update the NVS with that
configuration.

Please make sure to always use the correct IDF for the related device.
Using an ESP32 IDF for and ESP8266 device will result in an unusable
NVS, and no configuration will be available.

To build atriumcfg, install libmd-dev and execute `make atriumcfg`.

The atriumcfg tool provides following commands:
- help: to display a list of command with a short description
- add <fieldname>    : add element to array
- clear              : clear config
- exit               : terminate atriumcfg
- file <name>        : set current file to filename
- flashnvs <binfile> : flash binary file <binfile> to NVS partition
- genpart <bindf>    : generate an NVS partition from current config
- hw                 : switch to hardware configuration
- idf <path>         : set directory of IDF to <path>
- nvsaddr [<addr>]   : set address of NVS partition
- json               : output current config as JSON
- passwd {-c|<pass>} : -c to clear password hash, otherwise calc hash from <pass>
- port <p>           : set target communication port to <p> (default: /dev/ttyUSB0)
- print              : print currecnt configuration
- quit               : alias to exit
- read [<file>]      : read config from file <filename>
- set <f> <v>        : set field <f> to value <v>
- show               : print current configuration including all unset values
- size               : print size of current configuration
- sw                 : switch to software configuration
- updatenvs          : update NVS partition on target with currenct configuration
- write [<file>]     : write config to file <filename>

To prepare a configuration for a target follow these steps:
- execute `hw` to start hardware configuration
- execute `show` to list all unset fields and determine which values you need to set
- E.g. for setting up a standard WPS button and the status LED:
```
- add button
- set button[0].name wps
- set button[0].gpio 4
- set button[0].pullmode 3
- add led
- set led[0].name status
- set led[0].gpio 3
- set led[0].config 1
```

Please consult the file binformat.wfc to learn which values need to be
set to configure your devices appropriately.


Accessing and configuring a device online:
==========================================
After flashing a device and if there is no valid WiFi station configuration, WPS will be triggered during boot and booting will only continue after WPS has terminated. This is due to ESP8266 running out of RAM if all services have started before WPS is finished.

If WPS failed, the device defaults to Soft-AP mode. I.e. it opens up its own WiFi without password, and you can access the device at the IP address `192.168.4.1`. Pointing your browser to `http://192.168.4.1/config.html` will bring up the config page. Per default no password is set, so leave it empty, but set a new password by filling the two new password fields with an appropriate password (10-15 characters). Also set the ssid of your WiFi and its password. After saving the configuration, the ESP will access the new WiFi and ask via DHCP for its IP address.

After that you can access the device in your own WiFi. Sometimes switching the WiFi might not work out of the box. In that case it might be necessary to power-cycle the device.

Telnet and the UART console provides a list of command that can be queried via the `help` command. After setting a password, you need to change to the admin privilege level by using `su 1` and entering the password.

Recommended steps to set up the configuration at the beginning are:
```
> station ssid <wifi-ssid>
> station pass <wiif-password>
> station on
> ap off
> set dns_server <dns>
> set sntp_server <sntp>
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


Eval Board Configurations:
==========================
d1\_lite:      Wemos D1 Mini Lite board with ESP8285 and 1MB internal flash
d1\_mini:      Wemos D1 Mini board with ESP8266 and 4MB flash
lolin\_d32:    Lolin D32 board with ESP32 and 4MB flash


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

Onewire/1-wire support:
=======================
Generic 1-wire support is provided by Atrium. Currently, this stack only
has a driver for DS18B20 devices. To configure your onewire bus, just
set the GPIO of the bus with e.g. `hwconf set onewire.gpio 5` to GPIO 5
or whatever fits your needs. Do not forget to execute `hwconf write` to
make that change persistent. After rebooting, you can execute a `ow
scan` and gather its results with `ow list`.

The recognized devices are included in the node configuration. I.e. you
can set a name for every device by executing e.g. `config set
owdevices[0].name my_temperature_sensor1`. 1-wire can be pretty flaky,
if the connections are not good. You can enable debugging of the 1-wire
driver by executing `debug -e owb`. This will give you some debug
messages. The driver is able to support multiple devices per bus. A scan
for devices needs only performed once.

After scanning or booting with pre-configured devices, every device
registers actions according to its name. The first DS18B20 device will
register `ds18b20\_0!convert` to trigger a conversion command for
measuring the temperature, and `ds18b20\_0!read` for reading the result.

The result will be stored in a runtinme variable according to the name
of the device. These variables can be queried with the `webdata`
command, and can be submitted to an Influx DB with the `influx!rtdata`
action.


Security Concepts:
==================
- Security and privileges concept is simple. The current assumption
  is that you run the devices in a private network, where any user may
  use the actions available on the device. This may change in the
  future.
- Access to configuration parameters is restricted by an admin password.
- Admin password is stored as a hash value in NVS.
- If you forget the password, you can trigger a factory reset to reset
  all configuration parameters and erase the password. The factory reset
  can be bound to a button event. On devices without button you can
  reset trigger the factory reset via telnet or clear the NVS with the
  esptool.
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
Even the ESP8285 can be loaded with telnet, http, mqtt, uart console, and more, when it comes to required ROM. But be aware that the RAM might not be enough in certain situations. So you cannot expect to have multiple http connections active at the same time while MQTT is used, too.


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


Hardware configuration:
=======================
For earlier versions of Atrium the hardware and gpio settings were done
with menuconfig. This version of Atrium consolodiates many firmware
variants into few that are specific only to the baseboard variant. The
configuration of the peripherals is done either online (if enabled in
menuconfig) or via the `atriumcfg` tool, which can generate an NVS
partition that must be flashed to the target. The data in the NVS
partition includes hardware and software configuration. The software
configuration can always be done online, but the hardware configuration
can be optimized out to reduce the size of the firmware binary.

To perform a hardware/software configuration follow the steps below:
```
make bin/atriumcfg
bin/atriumcfg
```

Over-the-Air update:
====================
To build binaries for an OTA update, you have to build your project with target `ota` for ESP8266 to get binaries for the approrpiate partitions. For ESP32 all binaries can be used for any patition with OTA updates.
```
make PROJECT=<projectname> ota
```
This produces firmware binaries that are to be used for the relevant partitions for ESP8266.

If the project has a ROMFS partition, you will also need to execute:
```
make PROJECT=<projectname> romfs
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

If your system reports out of memory while flashing, try to turn of some services (e.g. MQTT by `mqtt disable` via telnet or serial console). This should free enough RAM to make the update possible.


Interfaces:
===========

Timers:
-------
Timers can be created on the fly, started and stopped by events, and can
be used to trigger arbitrary actions.

Events:
-------
Drivers create events that can be bound to actions defined by the
system and other drivers. Events are named according to the driver or
its instnace name. Execute 'event -l' for a list of availbe
events.

Actions:
--------
Actions can be triggered by events or executed manually via a shell.
Execute 'action -l' for a list and description of available actions.

Interface Stability:
--------------------
- Currently the interfaces are evolving.
- The action/event concept is planned to become stable - i.e. provided
  with a long term support.
- Interfaces that show a certain level of stability ared documented here
  or in accompaned documentation.


Remarks on terminal server:
===========================
As the ESP8266 has only one UART/rx port, but two UART/tx ports. Therfore, the terminal server will only run, if the UART console is disabled via `hwconf set system.console\_rx -1` and `hwconf set system.console\_tx -1`. Nevertheless, logging to UART0/tx will work (`system.diag`). In consequence access to the UART terminal is only possible via telnet.

The ESP32 does not suffer the same restriction. I.e. dedicated UARTs can be allocated for the terminal server and the console, so that these features won't interfere with eachother.

Enabling UART monitor:
======================
The UART monitor forwards data send to UART0/rx to the syslog with
priority information. To enable the UART monitor, you need to disable
the console for this UART, and define a terminal without a TX port.
I.e.:

```
hwconf set system.console\_rx -1
config add terminal
config set tmerinal[0].uart_rx 0
hwconf write
config write
```


Stating a new flash image configuration:
========================================
To start a new project, choose any of the available project configuration files in the projects subdirectory, and run a `make PROJECT=<projectname> menuconfig`. After exiting the configuration menu, the updated configuration will be saved in the named porject file.


Example - configuring an Itead/Sonoff S20:
==========================================
This example performs a two stage configuration. The first part uses the
`atriumcfg` tool to perform hardware and wifi settings. In a second step
a timer is defined on the device to automatically turn of the relay of
the S20 device 10 minutes after it being turned on.

The nodename is the hostname of the device and will also be used as the
influx node that measurements will be associated with. At any time you
can use the `json` command to view the current configuration in JSON
format. To see what settings are available use the `show` command.
```
bash> make atriumcfg
bash> bin/atriumcfg cfg/s20.cfg
> sw
OK
> set nodename atriumnode1
OK
> set station.ssid myssid
OK
> set station.pass mystationpassword
OK
> set station.activate true
OK
> set domainname myhome.lan
OK
> set dns_server 192.168.1.1
OK
> set syslog_host syslogserver
OK
> set sntp_server sntpserver
OK
> set influx.hostname influxserver
OK
> set influx.port 8090
OK
> set influx.measurement atrium
OK
> write myconfig.cfg
OK
> idf /work/idf-esp8266
OK
> updatenvs
[...]
```

After writing the config to NVS boot the device and wait until it has
obtained an IP address in your WiFi LAN. Lookup that IP address and dial
into the device via telnet. Now you can perform the more advance
configuration easily and interactively. To list the available timers, actions,
and events execute `timer -l` or `event -l` or `action -l`.

After defining a timer with its timeout in milliseconds, you can attach the
timer event to appropriate actions. The example below defines a timer with a
timeout of 10 minutes that gets started when the relay is turned on. When the
timer times out it will turn off the relay again. That way you can make sure
that the relay will not stay turned on forever.

```
> timer -c relaytimer 600000
OK.
> event -a mainrelay`on relaytimer!start
OK
> event -a relaytimer`timeout mainrelay!off
OK
```

Additionally, actions can be triggered at a specific time of day.
Therefor, the `at` command can be used to setup actions depending on
time, day of week, and holidays. This example turns the relay on working
days (Monday to Friday) at 6:30 and on holidays and the weekend on 8:00.
Additionally, in Germany the 3rd of October is a public holiday, which
could also be written in the US slash format - i.e. 10/3 or in dashed
japanese format including the year i.e. 2021-10-3.

```
> at wd 6:30 mainrelay!on
OK
> at we 8:00 mainrelay!on
OK
> at hd 8:00 mainrelay!on
OK
> holiday 3.10.
OK
```

Central Control of Atrium Devices:
==================================
If you have a network of several Atrium devices and you may want to add a
holiday to all of the devices, some kind of central control comes handy.
For that case `bin/atriumctrl.py` can be used to remotely access all or
specified devices. Use the `send` commmand to send a command to all
devices - e.g. `send version`. All devices will execute that command and
send the answer back. Like this you can perform changes of settings
quickly for all devices.


Known Issues:
=============
- The documentation is incomplete and not completely up-to-date
- PWM dimmer of ESP8266 may cause flickering output
- S20/ESP8266 OTA scheme with 1MB ROM works with release/v3.2 but fails
  with release/v3.3
- CMake based builds do not support OTA image generation
- LED-strip/WS2812b driver relies on RMT infrastructure, which
  has timing issues under certain condition. Avoid using channel 0.


Developer hints:
================
- MDNS requires IPv6 to work. The combined footprint is ~50k. If you don't need IPv6 for anything else, disabling MDNS and IPv6 will save you this amount of ROM. This is probably only a point to consider for devices with limited ROM, such as ESP8285 based systems without external flash.  

Bugs:
=====
If you want to report a bug, please do this via the github.com project page.
