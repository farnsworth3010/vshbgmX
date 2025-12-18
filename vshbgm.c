/*
 * vshbgm - system menu music, anyon?
 *
 * This program is licensed under the GPL-v2 license.
 * https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 *
 * heavily inspired by mp3play_lite - code from ARK4 and CXMB:
 * https://github.com/PSP-Archive/MP3PlayerPlugin/
 * https://github.com/PSP-Archive/ARK-4/
 * https://github.com/PSP-Archive/CXMB
 * https://github.com/PSP-Archive/CXMB_Reloaded
 */


#include "systemctrl.h"
#include "utils.h"
#include <pspaudio.h>
#include <pspaudiocodec.h>
#include <pspkernel.h>
#include <psprtc.h>
#include <pspsdk.h>
#include <pspsysevent.h>
#include <psputility.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("vshbgm", 0x1000, 1, 0);
PSP_MAIN_THREAD_ATTR(0);

typedef struct {
  SceUID hnd;
  int f_sz, d_st, d_st0, fr_sz, o_num;
  u8 *r_buf, *d_buf, *o_buf[2];
  unsigned long *c_buf;
} DecodeData;

static SceUID bgm_thid = -1;
static int r_flg = 0, chan = -1, stop = 0;
static volatile int actv = 0;
static volatile u32 l_tim = 0;

static int (*_glen)(int);
static int (*_acd)(unsigned long *, int);
static unsigned long *our_buf = NULL;

void *bgm_memory_alloc(u32 size) {
  u32 alloc_size = size + 64 + sizeof(SceUID);
  SceUID memid = sceKernelAllocPartitionMemory(2, "umem", 0, alloc_size, NULL);
  if (memid < 0)
    return NULL;

  u32 ptr_base = (u32)sceKernelGetBlockHeadAddr(memid);
  u32 ptr_aligned = (ptr_base + sizeof(SceUID) + 63) & ~63;
  memcpy((void *)(ptr_aligned - sizeof(SceUID)), &memid, sizeof(SceUID));

  return (void *)ptr_aligned;
}

int bgm_free_alloc(void *ptr) {
  if (!ptr)
    return -1;
  SceUID memid;
  memcpy(&memid, (void *)((u32)ptr - sizeof(SceUID)), sizeof(SceUID));
  return sceKernelFreePartitionMemory(memid);
}

u32 bgm_find_func(char *lib, char *name, u32 nid) {
  return sctrlHENFindFunction(lib, name, nid);
}

int bgm_check_audio_active(int (*glen)(int), int our_chan) {
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

int bgm_Get_Framesize(u8 *buf) {
  u32 header = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];

  if (((header & 0x180000) >> 19) != 3)
    return 0;

  if (((header & 0x60000) >> 17) != 1)
    return 0;

  int bitrate = (header & 0xF000) >> 12;
  int padding = (header & 0x200) >> 9;

  if (bitrate == 9)
    return 417 + padding;
  if (bitrate == 11)
    return 626 + padding;

  return 0;
}

static int acPat(unsigned long *buf, int type) {
  if (buf != our_buf && (type == 0x00001000 || type == 0x00001001 ||
                         type == 0x00001002 || type == 0x00001003)) {
    actv = 1;
    l_tim = sceKernelGetSystemTimeLow();
  }
  return _acd(buf, type);
}

static void Hook(void) {
  if (_acd)
    return;

  u32 g = bgm_find_func("sceAudio_Driver", "sceAudio", 0xB011922F);
  if (!g)
    g = bgm_find_func("sceAudio", "sceAudio", 0xB011922F);

  if (g)
    _glen = (void *)g;

  u32 a = bgm_find_func("sceAudiocodec", "sceAudiocodec", 0x70A703F8);
  if (a) {
    _acd = (void *)a;
    sctrlHENPatchSyscall((void *)a, acPat);
  }

  sceKernelDcacheWritebackAll();
  sceKernelIcacheClearAll();
}

static int MP3_Init(const char *file, DecodeData *mp3) {
  if ((mp3->hnd = sceIoOpen(file, PSP_O_RDONLY, 0777)) < 0)
    return -1;
  mp3->f_sz = sceIoLseek32(mp3->hnd, 0, PSP_SEEK_END);
  sceIoLseek32(mp3->hnd, 0, PSP_SEEK_SET);
  u8 hdr[10];
  mp3->d_st =
      (sceIoRead(mp3->hnd, hdr, 10) == 10 && !strncmp((char *)hdr, "ID3", 3))
          ? ((hdr[6] << 21) | (hdr[7] << 14) | (hdr[8] << 7) | hdr[9]) + 10
          : 0;
  mp3->d_st0 = mp3->d_st;
  sceIoLseek32(mp3->hnd, mp3->d_st, PSP_SEEK_SET);

  if (!(mp3->c_buf = bgm_memory_alloc(sizeof(unsigned long) * 65)) ||
      !(mp3->d_buf = bgm_memory_alloc(1152 * 4)) ||
      !(mp3->o_buf[0] = bgm_memory_alloc(1152 * 4)) ||
      !(mp3->o_buf[1] = bgm_memory_alloc(1152 * 4)) ||
      !(mp3->r_buf = bgm_memory_alloc(4096)))
    return -1;

  our_buf = mp3->c_buf;
  memset(mp3->c_buf, 0, sizeof(unsigned long) * 65);
  memset(mp3->d_buf, 0, 1152 * 4);
  memset(mp3->o_buf[0], 0, 1152 * 4);
  memset(mp3->o_buf[1], 0, 1152 * 4);

  if (sceAudiocodecCheckNeedMem(mp3->c_buf, PSP_CODEC_MP3) < 0 ||
      sceAudiocodecGetEDRAM(mp3->c_buf, PSP_CODEC_MP3) < 0 ||
      sceAudiocodecInit(mp3->c_buf, PSP_CODEC_MP3) < 0)
    return -1;
  mp3->fr_sz = 0;
  mp3->o_num = 1;
  return 0;
}

static int MP3_End(DecodeData *mp3) {
  sceAudiocodecReleaseEDRAM(mp3->c_buf);
  bgm_free_alloc(mp3->r_buf);
  bgm_free_alloc(mp3->c_buf);
  bgm_free_alloc(mp3->d_buf);
  bgm_free_alloc(mp3->o_buf[0]);
  bgm_free_alloc(mp3->o_buf[1]);
  mp3->r_buf = mp3->d_buf = mp3->o_buf[0] = mp3->o_buf[1] = NULL;
  mp3->c_buf = NULL;
  sceIoClose(mp3->hnd);
  mp3->hnd = -1;
  return 0;
}

static int MP3_Decode(DecodeData *mp3) {
  sceIoLseek32(mp3->hnd, mp3->d_st, PSP_SEEK_SET);

  int read_len = sceIoRead(mp3->hnd, mp3->r_buf, 1024);
  if (read_len < 4)
    return -1;

  if ((mp3->fr_sz = bgm_Get_Framesize(mp3->r_buf)) <= 0)
    return -1;

  if (mp3->fr_sz > read_len)
    return -1;
  mp3->c_buf[6] = (unsigned long)mp3->r_buf;
  mp3->c_buf[8] = (unsigned long)mp3->d_buf;
  mp3->c_buf[7] = mp3->c_buf[10] = mp3->fr_sz;
  mp3->c_buf[9] = (1152 * 4);
  if (sceAudiocodecDecode(mp3->c_buf, PSP_CODEC_MP3) < 0)
    return -1;
  memcpy(mp3->o_buf[mp3->o_num ^= 1], mp3->d_buf, 1152 * 4);
  return ((mp3->d_st += mp3->fr_sz) >= mp3->f_sz) ? 1 : 0;
}

static int Suspend_Handler(int id, char *name, void *prm, int *res) {
  if (id == 0x100 || id == 0x400) {
    if (id == 0x400)
      sceKernelDelayThread(1000000);
    r_flg = 1;
  }
  return 0;
}

PspSysEventHandler bgm_events = {0x40, "Suspend_Event", 0x0000FF00,
                                 Suspend_Handler};

static SceUID get_thread_id(const char *name) {
  int ret, count, i;
  SceUID ids[128];

  ret = sceKernelGetThreadmanIdList(SCE_KERNEL_TMID_Thread, ids, sizeof(ids),
                                    &count);
  if (ret < 0)
    return -1;

  for (i = 0; i < count; ++i) {
    SceKernelThreadInfo info;
    info.size = sizeof(info);
    ret = sceKernelReferThreadStatus(ids[i], &info);
    if (ret < 0)
      continue;
    if (strcmp(info.name, name) == 0)
      return ids[i];
  }
  return -2;
}

static int is_player_active(void) {
  if (get_thread_id("VshCacheIoPrefetchThread") >= 0)
    return 1;

  if (get_thread_id("VideoDecoder") >= 0 || get_thread_id("AudioDecoder") >= 0)
    return 1;

  if (sceKernelFindModuleByName("sceUSB_Stor_Driver"))
    return 1;

  if (sceKernelFindModuleByName("camera_plugin_module"))
    return 2;
  if (sceKernelFindModuleByName("tdb_plugin_module"))
    return 2;
  if (sceKernelFindModuleByName("radioshack_plugin_module"))
    return 1;
  if (sceKernelFindModuleByName("skype_main_plugin_module"))
    return 1;

  return 0;
}

static int simple_atoi(const char *s) {
  int res = 0;
  while (*s >= '0' && *s <= '9') {
    res = res * 10 + (*s - '0');
    s++;
  }
  return res;
}

static int GetVolume(void) {
  SceUID fd = sceIoOpen("ms0:/seplugins/vshbgm_volume.txt", PSP_O_RDONLY, 0777);
  if (fd >= 0) {
    char buf[8];
    int len = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (len > 0) {
      buf[len] = '\0';
      int val = simple_atoi(buf);
      if (val >= 0 && val <= 100)
        return val;
    }
  }

  fd = sceIoOpen("ms0:/seplugins/vshbgm_volume.txt",
                 PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd >= 0) {
    sceIoWrite(fd, "50", 2);
    sceIoClose(fd);
  }
  return 50;
}

static int vshbgm_thread(SceSize args, void *argp) {
  DecodeData mp3;
  while (!sceKernelFindModuleByName("sceVshCommonUtil_Module"))
    sceKernelDelayThread(100000);
  while (!sceKernelFindModuleByName("scePaf_Module"))
    sceKernelDelayThread(100000);
  while (!sceKernelFindModuleByName("sceVshCommonGui_Module"))
    sceKernelDelayThread(100000);
  sceKernelDelayThread(1000000);
  sceUtilityLoadAvModule(PSP_AV_MODULE_AVCODEC);

  Hook();
  sceKernelDelayThread(3000000);

restart:;
  while (1) {
    if (MP3_Init("ms0:/seplugins/cxmb/bgm.mp3", &mp3) == 0)
      break;
    if (MP3_Init("ms0:/bgm.mp3", &mp3) == 0)
      break;
    sceKernelDelayThread(2000000);
  }

  while (chan < 0 || chan > 7) {
    chan = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1152,
                             PSP_AUDIO_FORMAT_STEREO);
    if (chan < 0)
      sceKernelDelayThread(500000);
  }
  r_flg = 0;
  int in_media = 0;
  int check_counter = 0;

  int vol = GetVolume();
  int max_vol = (0x8000 * vol) / 100;

  while (!stop) {
    sceKernelDelayThread(10000);

    if (check_counter++ >= 3) {
      check_counter = 0;
      if (bgm_check_audio_active(_glen, chan)) {
        actv = 1;
        l_tim = sceKernelGetSystemTimeLow();
      } else if (actv && sceKernelGetSystemTimeLow() - l_tim > 200000) {
        actv = 0;
      }

      if (is_player_active()) {
        actv = 1;
        l_tim = sceKernelGetSystemTimeLow();
        in_media = 1;
      } else if (in_media) {
        actv = 0;
        in_media = 0;
      }
    }
    if (r_flg) {
      MP3_End(&mp3);
      r_flg = 0;
      sceKernelDelayThread(2000000);
      goto restart;
    }

    if (actv) {
      sceKernelDelayThread(100000);
      continue;
    }

    int res = MP3_Decode(&mp3);
    if (res < 0) {
      MP3_End(&mp3);
      sceKernelDelayThread(2000000);
      goto restart;
    }
    if (res == 1) {
      MP3_End(&mp3);
      goto restart;
    }
    sceAudioOutputBlocking(chan, max_vol, mp3.o_buf[mp3.o_num]);
  }
  return 0;
}

int module_start(SceSize args, void *argp) {
  sceKernelRegisterSysEventHandler(&bgm_events);
  if ((bgm_thid = sceKernelCreateThread("vshbgm", vshbgm_thread, 0x12, 0x1000,
                                        0, NULL)) >= 0)
    sceKernelStartThread(bgm_thid, 0, 0);
  return 0;
}

int module_stop(SceSize args, void *argp) {
  stop = 1;
  sceKernelDelayThread(100000);
  if (chan >= 0) {
    sceAudioChRelease(chan);
    chan = -1;
  }
  if (_acd) {
    sctrlHENPatchSyscall((void *)_acd, (void *)_acd);
    _acd = NULL;
  }
  if (bgm_thid >= 0) {
    sceKernelWaitThreadEnd(bgm_thid, NULL);
    sceKernelDeleteThread(bgm_thid);
    bgm_thid = -1;
  }
  sceKernelUnregisterSysEventHandler(&bgm_events);
  return 0;
}
