idf_component_register(
	SRCS display.cpp hd44780u.cpp ledcluster.cpp ssd130x.cpp fonts_generic.c fonts_ssd1306.c font6x8_ssd130x.c
	INCLUDE_DIRS .
	REQUIRES i2c logging
)
