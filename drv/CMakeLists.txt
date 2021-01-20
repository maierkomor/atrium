set(COMPONENT_SRCDIRS
	tlc5947
	max7219
	dht
	ws8212b
	tlc5916
	hc-sr04
	bme280
)
set(COMPONENT_PRIV_INCLUDEDIRS ../components/logging)
register_component(
	INCLUDE_DIRS "dht"
	)
