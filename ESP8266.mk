PROJECT_NAME	= firmware

EXTRA_COMPONENT_DIRS	= $(PROJECT_PATH)/drv

include $(IDF_PATH)/make/project.mk

CFLAGS		+= -DESP8266
CXXFLAGS	+= -DESP8266 -I$(WFCDIR)/include

