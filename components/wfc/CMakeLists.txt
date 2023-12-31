idf_component_register(
	SRCS hwcfg_$ENV{WFC_TARGET}.cpp swcfg_$ENV{WFC_TARGET}.cpp wfccore_$ENV{WFC_TARGET}.cpp
	INCLUDE_DIRS . ../streams
	REQUIRES streams
)
