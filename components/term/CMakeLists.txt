idf_component_register(
	SRCS
	tcp_terminal.cpp mem_term.cpp terminal.cpp uart_terminal.cpp
	jtag_terminal.cpp cdc_terminal.cpp

	INCLUDE_DIRS .

	REQUIRES logging netsvc streams
)
