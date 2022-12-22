set(COMPONENT_SRCS 
	spidrv.cpp
	sx1276.cpp
	ssd1309.cpp
)
set(COMPONENT_REQUIRES actions display env logging nvm xio)
set(COMPONENT_ADD_INCLUDEDIRS .)
register_component()
