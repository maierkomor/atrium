idf_component_register(
	SRCS netsvc.c tcpio.c lwtcp.cpp udns.cpp usntp.cpp
	INCLUDE_DIRS . ../../main
	PRIV_REQUIRES actions cyclic event logging
)
