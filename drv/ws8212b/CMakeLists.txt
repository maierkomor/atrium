idf_component_register(
	SRCS ws2812b.cpp
	INCLUDE_DIRS .
	REQUIRES logging
	PRIV_REQUIRES driver xio
)
