PROJECT_NAME	= firmware

EXTRA_COMPONENT_DIRS	= $(PROJECT_PATH)/drv $(PROJECT_PATH)/esp32

include $(IDF_PATH)/make/project.mk

CFLAGS		+= -DESP32
CXXFLAGS	+= -DESP32
#CXX		= xtensa-esp32-elf-g++

