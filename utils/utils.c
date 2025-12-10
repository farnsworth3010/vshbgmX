#include "utils.h"
#include "systemctrl.h"
#include <pspkernel.h>
#include <string.h>

void *memory_alloc(u32 size) {
  u32 alloc_size = size + 64 + sizeof(SceUID);
  SceUID memid = sceKernelAllocPartitionMemory(2, "umem", 0, alloc_size, NULL);
  if (memid < 0)
    return NULL;

  u32 ptr_base = (u32)sceKernelGetBlockHeadAddr(memid);

  u32 ptr_aligned = (ptr_base + sizeof(SceUID) + 63) & ~63;

  memcpy((void *)(ptr_aligned - sizeof(SceUID)), &memid, sizeof(SceUID));

  return (void *)ptr_aligned;
}

int free_alloc(void *ptr) {
  if (!ptr)
    return -1;
  SceUID memid;
  memcpy(&memid, (void *)((u32)ptr - sizeof(SceUID)), sizeof(SceUID));
  return sceKernelFreePartitionMemory(memid);
}

int Get_Framesize(u8 *buf) {
  u32 header = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
  int version = (header & 0x180000) >> 19;
  version = (version == 3) ? 0 : (version == 2 ? 1 : -1);
  if (version < 0)
    return 0;
  const int bitrates[2][15] = {
      {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}};
  const int samplerates[4] = {44100, 48000, 32000, 2};
  return 144000 * bitrates[version][(header & 0xf000) >> 12] /
             (samplerates[(header & 0xc00) >> 10] / (version + 1)) +
         ((header & 0x200) >> 9);
}

u32 find_func(char *lib, char *name, u32 nid) {
  u32 addr = sctrlHENFindFunction(lib, name, nid);
  return addr;
}

int check_audio_active(int (*glen)(int), int our_chan) {
  if (!glen)
    return 0;
  for (int i = 0; i < 8; i++) {
    if (i != our_chan) {
      int len = glen(i);
      if (len > 2000)
        return 1;
    }
  }
  return 0;
}
