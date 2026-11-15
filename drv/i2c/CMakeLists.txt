idf_component_register(
	SRCS
	ads1x1x.cpp
	ahtxx.cpp
	apds9930.cpp
	bh1750.cpp
	bq25601d.cpp
	bme680.c
	bmp388.cpp
	bmp581.cpp
	bmx.cpp
	ccs811b.cpp
	crc8.cpp
	hdc1000.cpp
	ht16k33.cpp
	husb238.cpp
	i2cdrv.cpp
	ina2xx.cpp
	mcp2300x.cpp
	mcp2301x.cpp
	opt3001.cpp
	pca9685.cpp
	pcf8574.cpp
	sgp30.cpp
	sgp41.cpp
	sh1106.cpp
	sht4x.cpp
	si7021.cpp
	ssd1306.cpp
	tca9555.cpp
	ti.cpp
	INCLUDE_DIRS .
	REQUIRES actions cyclic event display env logging nvm term xio
	PRIV_REQUIRES xio driver
)
