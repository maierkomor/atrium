About Atrium Firmware:
======================
Atrium Firmware is software stack for the ESP family of controllers from
Espressif. It provides ready to build configuration (e.g. for S20
socket) and supports ESP8285, ESP8266, ESP32, ESP32-C3, ESP32-S2, and
ESP32-S3 in one source code package, providing infrastructure to build,
configure, and use the devices in a consistent manner.

The intended audience of this distribution are enthusiasts and hobbyists
that want a home-automation without cloud access. For remote operation
MQTT support is integrated, which needs an MQTT server running in your
network.

Atrium firmware is designed to be cover as many applications with one
flash image as possible. Therefore, its drivers are initialized while
booting. The relevant configuration can be performed with an off-line
tool or online in the device if the flash image supports it. The
settings can be uploaded to the device via telnet or directly flashed to
its NVS (non-volatile stroage).

After booting the configured drivers provide events, actions, and timers
that can be associated to provide additional functionality.
Additionally, state-machines can be defined to enable certain
event-actions asssociations based on specific edge-conditions. Configuring
actions, events, and timers can be done online, and is stored among the
regular device configuration in the NVS.

Additionally, Lua support is integrated, which enables flexible
application specific behaviors.


Features:
=========
Software services:
- fully costomizable with timers, events, and actions
- Lua scripting support
- runtime-configurable state-machines to define state-bound event-action
  bindings
- over-the-air (OTA) updates via FTP, TFTP, HTTP upload and download
- OTA aware, persistent system configuration
- tool to send commands to all devices on the network
- timezone support for CET with daylight-saving convention
- telnet server and UART&JTAG console with many commands for remote
  operation and configuration (hardware and software)
- asynchronous integration with LWIP for most protocols
- syslog support for sending log messages to a syslog server
- serial monitor to forward UART to syslog
- WPS support

Protocol support:
- InfluxDB data upload (v1 only) via UDP and TCP
- SNTP timed actions with day-of-week and holiday settings
- MQTT integration (directly integrated with async LWIP)
- HTTP server with system configuration support
- Telnet
- FTP server for access to spiffs/fatfs
- HTTP, FTP, TFTP download
- DNS and mDNS hostname lookup
- mDNS for answering to `<nodename>.local` queries

Filesystem support:
- FAT
- SPIFFS
- ROM filesystem support for read-only data
- memfile support
  (in data segment stored files for e.g. help pages)

Hardware drivers:
- button support with debouncing
- relay control support
- ADC with sliding window
- displays: SSD1360 (OLED), HD44780U (LCD), 7-/14-segmend LEDs (e.g. HT16K33, MAX7219)
- support for several temperature, humidity, and pressure sensors (I2C
  and 1-wire)
- SGP30 and CCS811B TVOC and CO2 sensor
- APDS9930 proximity and light sensor
- alive status LED support with different blinking schemes
- TLC5947 and TLC5916 constant current LED driver support
- color LED strip support for WS2812b based strips
- transparent I/O multiplexer support
- touch-channel support


Getting started:
================
To get started, you can use the `flash-atrium.sh` script on a Linux
machine to flash an ESP device. As an argument you must pass one of the
binary distribution's subdirectories that matches the controller type
and its flash size.

Valid options are: `esp8285`, `esp8266_4m`, `esp32_4m`, `esp32-s3_8m`,
`esp32-c3_4m`.

On Windows you might want to use one of the ESP flashing tools with a
GUI. In that case make sure that you erase the whole flash before
flashing the first time, and that the files in the directory are flashed
to the address given in their name.

On Ubuntu, you should uninstall `brltty`, otherwise CH340 based boards
will not be accessible as `/dev/ttyUSB*`.

For ESP32 app images no address is given, as they can be flashed to any
partition. The default ESP32 partition layout has partition `ota_0` at
0x100000. The bootloader address is dependent on the conroller type. The
partition-table (`ptable`) always at 0x8000.

After that configure the device via the offline tool bin/atriumcfg or via the
serial console (on-device hardware configuration is not supported on 1MB
devces).

The configuration can be written to the non-volatile storage (NVS) of
the device (either directly on the device's serial console or using
`bin/atriumcfg`, which supports writing the complete NVS partition). 1MB
devices can use the xxd method described below to update their hardware
config remotely.


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


Interfaces:
===========
Atrium provides timers, events, and actions to configure the behavior of
a devices. Like this a single flash image can be used for different
kinds of hardware and/or applications. Additionally, state-machines can
be used to provide user-interactions with buttons, LEDs, and a display.
This also enables the user to define dynamic behavior based on states
and event processing.

Timers:
-------
Timers can be created on the fly, started and stopped by events, and can
be used to trigger arbitrary actions. For every timer an associated
timeout event is created, and the timer can be started and stopped with
associated actions. Furthermore, they can be configured  to
automatically start on startup and to repeat with the given interval
time.

Actions:
--------
Actions can be triggered by events or executed manually via a shell.
Execute `action -l` for a list and description of available actions.

To trigger actions remotely, MQTT can be used. Therefore, send the name
of the action to be triggered as value to the topic `<nodename>/action`.

Some actions take an optional argument. The argument can be given on the
command-line when invoking it directly, or via the event interface,
described below.

Events:
-------
Timers, state-machines, and drivers create events that can be bound to
actions defined by the system and other drivers. Events are named
according to the driver or its instnace name. Execute `event -l` for a
list of availbe events.

Events can be associated with an action, using the command `event -a
<event> <action>`. Optionally, an argument can be given to the action.

State-Machines:
---------------
To handle differents mode of operation, runtime-configurable
state-machines are provided that enable associated event-action pairs
only if the related state of the state-machine is active. In addition
events are provided that inform about entry and exit of states. These
events can also be associated with actions.

Like this it is easily possible to use a couple of buttons and LEDs to
provide a user-interface that can drive multiple modes of operation.

Use the `sm` command to add new machines and states. States can be
referneced as `<machine>:<state>` pair in actions. To change the state
of a state-machine, use the `sm!set` action and give it a
`<machine>:<state>` pair as an argument. This action will cause a state
transistion, which first triggers the ``<machine>:<state>`exit`` event,
disable all event-action bindings of the old state, enables all
event-action bindings of the new state, and triggers the
``<machine>`enter`` event of the new state.


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

Rebooting may be necessary for some settings to be picked up.


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


MQTT support:
=============
Atrium has been tested with the Mosquitto MQTT server running on an
Raspi. Once Atrium has an MQTT server configured and is enabled, it will
auto connect to the MQTT server. Publishing of the runtime data is
triggered using the `mqtt!pub_rtdata` action. It is recommended to bind
this action to a custom timer with an interval time that matches your
application. E.g. the following command sequence will configure the MQTT
client to connect to the MQTT server named `mqttserver` on port 1883,
create a timer that is fired every 5000ms and triggers the MQTT
publication of the runtime data that can be listed using the `env`
command.
```
mqtt uri mqtt://mqttserver:1883
mqtt enable
timer -c mqtttmr 5000 true true
event -a mqtttmr`timeout mqtt!pub_rtdata
config write
```

MQTT subscribes per default to the `action` topic. Like this you can
trigger remotely any kind of action on your target node. E.g. using the
mosquitto publish command line tool.
```
mosquitto_pub -h mqtt-server -t node/action -m 'led!set status:fast'
```

For relays there is also an addtional dedicated subscription consisting
of the relayname preceded by `set_`. So following command will toggle
the state of a relay called `mainrelay`:
```
mosquitto_pub -h mqtt-server -t node/set_mainrelay -m toggle
```


FTP server:
===========
To enable the FTP server, you must explicitly set its start flag.
Additionally, you can specify its port and root directory, i.e. the
location in the filesystem, which should be served by the FTP server. If
your device has SPIFFS, which does not support directories, you can only
serve the whole filesystem. In that case the implicit default location
`/flash` is the root directory.

```
config set ftpd.start true
```

The ftpd cannot serve files from ROMFS. It currently has no security at
all. If you turn it on, files can be modified, deleted, and copied
without any password. Just login with user `ftp`.

Furthermore, only the active mode is supported. I.e. to upload a file
you have to execute the `ftp` command with option `-A`.


HTTP server:
============
The http server will automatically look for an `index.html` file in the
given root directory, and start if one is found, unless the start flag
is explicitly set to false.


INETD - internet services daemon:
=================================
The inetd service is implicitly started, when any service is enabled
that waits for incoming TCP connections. I.e. telnet, httpd, ftpd.
It does not have a task of its own, but is tightly coupled into the LWIP
stack to consume as little RAM as possible, unless you explicitly
configured the socket interface to be used.

The command `inetadm` can be used to temporarily disable services which
are already running. This will not shut-down daemons that already got
started by the service, but only reject incoming connections and not
create new service daemons.


A/D Converters:
===============
A/D converter can be sampled via an action on ESP8266 and on ESP32 via
an action or continuously by specifying a sampling interval.
Additionally, a threshold configuration can be used to specify upper
and lower threshold that fire events when being crossed in a hysteresis
behavior. Thresholds can be applied to any variable, not only ADC
values.


Relays:
=======
Relays can be attached to any GPIO or extended GPIO that are provided by
supported I/O extenders. Additionally, a minimum switch interval can be
specified in milliseconds to make sure the relay is not being damaged by
fast switching. Furthermore, a interlock mechanism can interlock two
relays against eachother, making sure that only either one of both is
getting turned on.


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
register `ds18b20\_0!sample` to trigger a conversion command with a
subsequent read. Further actions are registered to adjust the sampling
resolution.

The result will be stored in a runtinme variable according to the name
of the device. These variables can be queried with the `env`
command, and can be submitted to an Influx DB with the `influx!rtdata`
action.

Parasite power is supported and is enabled if you configure the
related gpio. 

1-Wire does not work when the CPU runs in a lower frequency mode, as an
internal ROM function does not adjust to that alternate frequency.


Lua support:
============
Atrium includes support for the execution of Lua scripts. This can be
used to implement custom controls without having to work on the Atrium
sources. The integration includes some functions to trigger events or
execute actions directly, and to create and manipulate variables that
are globally accessible via the `env` command.

For compiling Lua files the `luac` command can be used which crates a
function named by the basename of the file, which can be executed using
the `lua` command.

Lua integration provides also the `lua!run` and `lua!file` actions.
`lua!run` directly interprets its argument as a lua script, whlie the
`lua!file` action parses a file and provides the included function in
the Lua environment as callable functions. 

Using the `lua!run` action any of those functions can be triggered and
also be bound to e.g. timer events.

The file `data/lua/ws2812b.lua` shows an example of how to use it with
an WS2812b LED bar. To run it load the Lua file to a ROMFS partition,
and execute `luac ws2812b.lua` to compile it. After that you can run the
script using the `lua!run ws2812b` action. I.e. the file is compiled
to a function with the same name, which is resolved from Lua's global
scope.

Additonally, the file `data/lua/ledstrip.lua` provides another example
using the Lua functions of the WS2812b driver. This example assumes that
there are 100 WS2812b LEDs in the array. Attaching the `ledstrip`
function to a timer provides continuous update of all 100 LEDs. On an
ESP32-C3 this takes about 15ms.


Building Atrium yourself:
=========================
To prepare the build environment, run "./setupenv.sh", which will:
- check some prerequisites that need to be installed manually
- download and patch the IDFs for ESP8266 and ESP32 family
- download compilers and tools specified by the IDF

To build a project, use the supplied build script that will handle all
the prerequisits and the differences between ESP32 and ESP8266:
```
> bash mkatrium.sh <project>
```

This will build Atrium according to the configuration defined in
file `projects/<project>` in a subdirectory called `build.<project>`.

If you want to adjust the build configuration, you can use the menu
based configuration tool that is provided by calling:
```
> bash mkatrium.sh <project> menuconfig
```

To flash a target, run:
```
> bash mkatrium.sh <project-name> flash
```

ESP8266 application images are bound to their partition base address.
Therefore, for every partition a different image must be created. Per
default `mkatrium` only builds an image for the primary application
partitoin. To also build ESP8266 images for the other live over-the-air
update (OTA), execute:
```
> bash mkatrium.sh <project-name> ota
```

To build `atriumcfg` and `mkromfs` tools, execute:
```
make tools
```

The `mkromfs` tool can be used to create a ROMFS partition that contains
read-only files and cannot be modified during runtime. ROMFS is
specially designed to be light-weight regarding ROM and RAM usage, and
therefore is well-suited to be used on ESP8285 devices that provide only
1MB of flash.

Some example ROMFS partitions are supplied in the binary distribution.
You can create ROMFS partitions yourself, by calling:
```
mkromfs -o name.romfs <files>
```

ROMFS does not support a directory structure. Therefore every file-name
must be unique.

Required Tools:
===============
All required tools are installed using the setupenv.sh
- On Linux:
	- IDF for ESP32 or ESP8266 respectively (use setupenv.sh to
	  setup the build-environemtn)
	- esptool for flashing (shipped with IDF or download from
	  https://github.com/espressif/esptool
- On Windows:
	- building is not supported (and setupenv.sh will probably not work)
	- Flash download tool:
		- Download at: https://www.espressif.com/en/support/download/other-tools
		- https://www.espressif.com/sites/default/files/tools/flash_download_tool_v3.8.5.zip


WFC dependency:
===============
WFC - wire format compiler - is used to generate code for serializing
and parsing config data. The version used is a non-public version with
advanced features. Therefore, the generated files are included in the
distribution and the generation is not performed during the build
process.

If you want to expand the configuration and therefore need to generated
the associated .cpp and .h files, please get in touch with the author
(e-mail: thomas at maier-komor dot de).


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
- Atrium is expected to be used in a close home network with WiFi
  security and high level of trust within the network.
- Password based access via telnet and http are prone to eavesdropping.
- Atrium has no flash or update protection right now.


RAM considerations:
===================
Even the ESP8285 can be loaded with telnet, http, mqtt, uart console, and more, when it comes to required ROM. But be aware that the RAM might not be enough in certain situations. So you cannot expect to have multiple http connections active at the same time while MQTT is used, too.

For performing OTA updates it might be necessary to reduce the RAM consumption. For this, you can temporary disable the syslog with `dmesg 0` and stop influx and mqtt.


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

Over-the-Air (OTA) updates:
===========================
To build binaries for an OTA update, you have to build your project with target `ota` for ESP8266 to get binaries for the approrpiate partitions. For ESP32 all binaries can be used for any patition with OTA updates.
```
./mkatrium <projectname>
```
This produces firmware binaries for that are to be used for the relevant partitions for ESP8266.

If the project has a ROMFS partition, you will also need to execute:
```
make PROJECT=<projectname> romfs
```

To update via the integrated webserver, just point your browser to the IP of the device on page `/update.html`. E.g. `http://192.168.1.89/update.html`.  Always update the romfs/data partition first, and then the application, because a successful application update will trigger the reboot which will require an updated romfs partition.

To update via telnet you need to perform the following steps:
1. upload all necessary files for download to a HTTP, FTP or TFTP
   server.

2. telnet into your device.

3. If you have configured a password, then you must execute `su 1` to get the appropriate privileges.

4. By executing `boot` on the console you can see on which partition the device is running currently and which partition will be used for an update. 

5. By executing `update <uri>` you can trigger a download from the
   server that will update the currently inactive partition.  Depending
   on the configuration of the running Atrium firmware FTP and TFTP URIs
   may also be suppored for downloading updates.  Please activate the
   partition only with `boot <partition-name>` (e.g. `boot ota0` or
   `boot app`), after the update verification was successful.

   `<uri>` can refer to a `http://`, `ftp://` or `tftp://` URI. Firmware
   images for 1MB devices only support HTTP URIs.

   If you have set the `otasrv` config setting, then you can trigger the
   update with just the version number and update will look for an
   appropriate image at the location of `otasrv` in a subdirectory
   called according to the firmware configuration/project name. For
   this, execute `update -v <version>`.

   If update returns with an error, the update failed, and booting from
   the relevant partition will most likely not be successful and end in
   a reset-loop. To get more information about the reason for failure,
   you can execute `dmesg` to see the last message that were generated
   during the OTA procedure. If the update ran out of memory, you might
   want to try to reboot before restarting the update again.

6. If the relevant project (e.g. `esp8285`) has configured a ROMFS
   partition, then you may also need to update this partition as well.
   For this execute: `update -r http://server/path/romfs.bin`

7. When flash updates were successful, switch the boot partition to the
   updated partition. E.g.: `boot app2`

8. Finally, trigger a reboot, by executing `reboot`.

Never switch boot partition if updating returned an error.  If your system doesn't boot anymore, you will need to flash via serial boot loader.

If your system reports out of memory while flashing, try to turn of some services (e.g. use `dmesg 0`, `mqtt disable`, and `influx stop` via telnet or serial console). This should free enough RAM to make the update possible. Following procedure might help:
```
config backup
config clear timefuses
config clear triggers
config clear influx
config clear mqtt
dmesg 0
config write
reboot
```

After that there should be enough RAM available for the update to
complete successfully. You will need about 10k of free RAM. Be sure that
`config backup` returns `OK`, otherwise you will have to restore your
configuration manually. After updating and before rebooting, you can
restore the old configuration using:
```
config restore
reboot
```

OTA server:
-----------
An OTA server should provide firmware images via HTTP at the specified
location. Additionally, the OTA server can be set via in the `otasrv`
config setting, which simplifies the ota update command. If the `otasrv`
variable is set the update command only needs the version, you intend to
update to, and the update command will the pick the coorect file for the
relevant partition ($ext) and firmware configuration (project name,
$fwcfg) as follow:

```
$otasrv/$fwcfg/atrium-$version.$ext
```

The `otasrv` setting should be of the format: `http://server/path`.
Below should be a subdirectory for the relevant firmware/project
configuration, which in turn includes the firmware file named
`atrium-<version>.<ext>`. The extension part is for ESP32 devices
always `.bin`, as the firmware can be stored on any partition. For
ESP8266 family, the filename extension must match the partition name, as
it needs to be linked with the correct addresses.

To use this, the update command should be executed with the option `-v`
and the relevant version name:
```
update -v <version>
```


Interface Stability:
--------------------
- Currently the interfaces are evolving.
- The action/event concept is planned to become stable - i.e. provided
  with a long term support.
- Interfaces that show a certain level of stability ared documented here
  or in accompaned documentation.
- Configuration binary format is designed for forward and backward
  compatibility, and has been kept compatible and evolving since the
  start of the project.
- Shell commands are aligned to UNIX commands and have been evolving
  over time - especially concerning their options.


Remarks on terminal server:
===========================
As the ESP8266 has only one UART/rx port, but two UART/tx ports. Therfore, the terminal server will only run, if the UART console is disabled via `hwconf set system.console_rx -1` and `hwconf set system.console_tx -1`. Nevertheless, logging to UART0/tx will work (`system.diag`). In consequence access to the UART terminal is only possible via telnet.

The ESP32 does not suffer the same restriction. I.e. dedicated UARTs can be allocated for the terminal server and the console, so that these features won't interfere with eachother.

Enabling UART monitor:
======================
The UART monitor forwards data send to UART0/rx to the syslog with
priority information. To enable the UART monitor, you need to disable
the console for this UART, and define a terminal without a TX port.
I.e.:

```
hwconf set system.console_rx -1
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
into the device via telnet or use the nodename of the device suffixed
with `.local` - e.g. `telnet sensor1.local`. Now you can perform the
more advance configuration easily and interactively. To list the
available timers, actions, and events execute `timer -l` or `event -l`
or `action -l`.

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

Central Control of Atrium Devices (AtriumCtrl):
==================================
If you have a network of several Atrium devices and you may want to add a
holiday to all of the devices, some kind of central control comes handy.
For that case `bin/atriumctrl.py` can be used to remotely access all or
specified devices. Use the `send` commmand to send a command to all
devices - e.g. `send version`. All devices will execute that command and
send the answer back. Like this you can perform changes of settings
quickly for all devices, or set the holidays centrally, without having
to access every device individually.

AtrimCtrl can also be used for loading a hardware configuration that was
created with the `atrimcfg` utility.


Displays:
=========
Atrium supports out of the box some displays including 7- and
14-segment LED displays, multi-line text LCDs, and OLEDs. All displays
are integrated in the clock application if configured appropriately. The
clock application has the ability to display all kinds of information
gathered by the attached sensors.

To configure a display use hwconf (online) or atriumcfg (offline) to set
the display type in the display config. Additionally, you should set the
maximum X and Y resolution of the diplays (digits for LED and text LCDs,
and dots for OLEDs). OLEDs based on SSD1306 may also need appropriate
options for the individual hardware configuration as specified in the
ssd1306.h file. Documentation for this is on the TODO list right now.

Fonts:
======
Atrium firmware images include some basic fonts, if display support is
included, for both row-major and byte-colum-major (for OLEDs) formats.
As fonts require a lot of ROM, Atrium supports to load additional fonts
at run-time. For run-time loading the font file must be either in the ROMFS
or on a partition with FATFS/SPIFFS. Fonts on ROMFS partitions can be
loaded on all ESP32 devices. Fonts stored on FATFS or SPIFFS parttitions
can only be used if the device has an attached SPI RAM. ESP8266 devices
do not support dynamic font loading, due to limited RAM and restrictions
regarding the memory mapping of ROMFS files into the address space.

Ready to use font files are distributed as part of the source tree in
the `data/fonts` subdirectory, and in the binary distribution in the
`fonts` subdirectory. Regular row-major fonts for TFTs such as the
ILI9341 have the `.af1` extension, while fonts for OLEDs in
byte-column-major format have the `.af2` extension. Fonts that include
both formats have the `.afn` extension.

The `font-tool` utitlity can be used to create font files for Atrium
devices from regular TrueType fonts. To create font files from a `.ttf`
file, just pass the sizes as a comma separated list and the TrueType
font to the `font-tool`, and it will create the `.af1`, `.af2`, and
`.afn` files for you.

E.g.:
```
$ cd $ATRIUM_ROOT
$ make font-tool
$ bin/mkfonts.sh -s 8,12,18,24 courier.ttf
```

Fonts added to the ROMFS or root directory of FATFS/SPIFFS will be
loaded when intializing a display. They can be used either when
rendering the screen with Lua scripts or by replacing the default fonts
using the font settings in the `screen` section of the `config` command.

I/O-Extenders:
==============
Support for several I/O extenders is included. These can be used like
regular controller internal GPIOs for most drivers. Of course operation
of these GPIOs come with a latency impact over core GPIOs. In
consequence timing sensitive devices may not operate correctly with I/O
extenders. But for drivers like button and LEDs will work without a
problem.

GPIO ports of I/O extenders are listed like core GPIOs when using the
`gpio` command. To ensure the GPIOs' ids don't change after rebooting,
newly detected I/O extenders are added to the hardware configuration. To
make this change persistent, you must use `hwconf write` to write the
new configuration to NVM.


Advanced I2C commands:
======================
The `i2c` command provides access to device specific commands. To issue
such a command use the `i2c` command followed by the device name and the
command and its arguments to execute. Similarly these commands may be
invoked via the action interface (e.g. binding them to the ``init done`` event).


Advanced I2C configuration:
===========================
Some I2C devices such as PCA9685 may need application specific settings
that also may change with the software or use-case. For these devices
the startup event ``init`done`` can be used to trigger an action that
configures the devices appropriately. E.g. for setting the prescale
value of PCA9685 to adjust the base frequency of the PWM.

Like this the configuration can also be adjusted at run-time.
Therefore, these parameters are also not seen as part of the hardware
configuration and not part of the `hwconf` settings.


USB semihosted filesystem:
==========================
Support for USB semihosted filesystem via openocd is available on
devices that have native USB support (i.e. ESP32-C3, ESP32-S3). If
compiled in and openocd is running, the filesystem is automatically
mounted during boot at `/usb`, but its cannot be listed with `ls`.

Nevertheless, you can use it to copy a file from the host to your target
storage filesystem, which may be quite handy for e.g. Lua files. To copy
a file, just executed e.g. `cp /usb/myfile.lua .`. This will copy the
file to the current directory, which is per default `/flash`.


Crash dumps:
============
Atrium projects for 4MB flash size are configured with a core dump
partition and include infrastructure to save and download crash dumps.
For this, the `dumpadm` command is available that allows copying the
current crash dump to a writable storeage filesystem. Additionally, if
the internal www server is started, a download of `http://nodename/core`
will download the core binary from the crash partition.


Known Issues:
=============
- The documentation is incomplete and not completely up-to-date
- LED-strip/WS2812b driver relies on RMT infrastructure on ESP32, which
  has timing issues under certain condition. Avoid using channel 0.
- Update via http download from apache may hang on specific file sizes
  on ESP8266. This does not happen when downloading from a Python
  http.server session or when uploading via the update.html web page of
  the httpd server of Atrium.  The reason for this is unclear.
  Please use one of the work-arounds. On 4MB devices FTP and TFTP can be
  used in addittion to HTTP for updating. On a stable WiFi TFTP is the
  fastest update method.
- ESP32-S2 does not have enough IRAM to support the WiFi IRAM options.
  Therefore IRAM optimizations for WiFi are turned off.
- CDC terminal on ESP32-S2 hangs on flush shortly after startup. The
  reason for this is currently unclear. Use the UART terminal on
  ESP32-S2.

Bugs:
=====
If you want to report a bug, please do this via the github.com project page.
