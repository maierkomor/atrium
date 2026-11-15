EXTRA_COMPONENT_DIRS	= $(PROJECT_PATH)/drv

include $(IDF_PATH)/make/project.mk

### to shorten absolute filenames to relative ones with gcc>8
# -fmacro-prefix-map=$(PWD)=
#  this is somehow intercepted by the build system and ends up empty
# -DSHORT_BASE_FILE="$(notdir $<)"

CFLAGS		+= -DESP8266
CXXFLAGS	+= $(CPPFLAGS) -DESP8266 -I$(WFCDIR)/include

