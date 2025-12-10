#ifndef UTILS_H
#define UTILS_H

#include <pspsdk.h>
#include <pspkernel.h>

#define K_CALL(func, ...) ({ \
    int k1 = pspSdkSetK1(0); \
    int res = func(__VA_ARGS__); \
    pspSdkSetK1(k1); \
    res; \
})

#define K_CALL_VOID(func, ...) ({ \
    int k1 = pspSdkSetK1(0); \
    func(__VA_ARGS__); \
    pspSdkSetK1(k1); \
})


void *memory_alloc(u32 size);
int free_alloc(void *ptr);
int Get_Framesize(u8 *buf);
u32 find_func(char *lib, char *name, u32 nid);
int check_audio_active(int (*glen)(int), int our_chan);

#endif
