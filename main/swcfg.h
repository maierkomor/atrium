#define MKSTR(s) #s
#define MKHFILE(a,b) MKSTR(a##b.h)
#define SWCFGH(y) MKHFILE(swcfg_,y)
#include SWCFGH(WFC_TARGET)
