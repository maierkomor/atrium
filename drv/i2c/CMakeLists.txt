idf_component_register(
	SRCS
	apds9930.cpp
	bh1750.cpp
	bme680.c
	bmx.cpp
	ccs811b.cpp
	hdc1000.cpp
	ht16k33.cpp
	i2cdrv.cpp
	ina2xx.cpp
	mcp2300x.cpp
	mcp2301x.cpp
	opt3001.cpp
	pca9685.cpp
	pcf8574.cpp
	sgp30.cpp
	si7021.cpp
	ssd1306.cpp
	tca9555.cpp
	ti.cpp
	INCLUDE_DIRS .
	REQUIRES actions cyclic event display env logging nvm term xio
)
