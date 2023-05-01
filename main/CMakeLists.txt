#target_compile_definitions(${COMPONENT_TARGET} PUBLIC "-DFWCFG=$ENV{FWCFG}")
idf_component_register(
	SRCS
	adc.cpp
	alarms.cpp
	buttons.cpp
	console.cpp
	dht.cpp
	dimmer.cpp
	displays.cpp
	distance.cpp
	fs.cpp
	ftpd.cpp
	globals.cpp
	gpios.cpp
	httpd.cpp
	hwcfg.cpp
	HttpReq.cpp
	HttpResp.cpp
	HttpServer.cpp
	i2c.cpp
	inetd.cpp
	influx.cpp
	leds.cpp
	ledstrip.cpp
	luaext.cpp
	lwinetd.cpp
	mqtt.cpp
	nightsky.cpp
	ota.cpp
	ow.cpp
	ping.cpp
	relays.cpp
	romfs.cpp
	screen.cpp
	settings.cpp
	shell.cpp
	spi.cpp
	startup.cpp
	statemach.cpp
	support.cpp
	swcfg.cpp
	syslog.cpp
	telnet.cpp
	touchpads.cpp
	uarts.cpp
	udpctrl.cpp
	webcam.cpp
	wfccore.cpp
	wifi.cpp

	REQUIRES
	actions app_update button cyclic event dht display event fatfs
	hc-sr04 hlw8012 i2c logging lua max7219 memfiles onewire relay
	spi timefuse tlc5947 wpa_supplicant ws8212b espcoredump
)
