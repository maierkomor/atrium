#define MKSTR(s) #s
#define MKHFILE(a,b) MKSTR(a##b.h)
#define HWCFGH(y) MKHFILE(hwcfg_,y)
#include HWCFGH(WFC_TARGET)
