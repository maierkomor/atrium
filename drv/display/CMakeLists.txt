idf_component_register(
	SRCS display.cpp hd44780u.cpp ledcluster.cpp ssd130x.cpp fonts_row_major.c fonts_byte_column_major.c font6x8_ssd130x.c glyphs.c
	INCLUDE_DIRS .
	REQUIRES i2c logging
)
