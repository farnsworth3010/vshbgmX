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
#include <string.h>

PSP_MODULE_INFO("vshbgm", 0x1000, 1, 0);
PSP_MAIN_THREAD_ATTR(0);

typedef struct {
  SceUID hnd;
  int f_sz, d_st, d_st0, fr_sz, o_num;
  u8 *r_buf, *d_buf, *o_buf[2];
  unsigned long *c_buf;
} DecodeData;

SceUID thid = -1;
int r_flg = 0, chan = -1, stop = 0;
volatile int actv = 0;
volatile u32 l_tim = 0;

int (*_glen)(int);
int (*_acd)(unsigned long *, int);
unsigned long *our_buf = NULL;

int acPat(unsigned long *buf, int type) {
  if (buf != our_buf && (type == 0x00001000 || type == 0x00001001 ||
                         type == 0x00001002 || type == 0x00001003)) {
    actv = 1;
    l_tim = sceKernelGetSystemTimeLow();
  }
  return _acd(buf, type);
}

void Hook(void) {
  if (_acd)
    return;

  u32 g = find_func("sceAudio_Driver", "sceAudio", 0xB011922F);
  if (!g)
    g = find_func("sceAudio", "sceAudio", 0xB011922F);

  if (g)
    _glen = (void *)g;

  u32 a = find_func("sceAudiocodec", "sceAudiocodec", 0x70A703F8);
  if (a) {
    _acd = (void *)a;
    sctrlHENPatchSyscall((void *)a, acPat);
  }

  sceKernelDcacheWritebackAll();
  sceKernelIcacheClearAll();
}

int MP3_Init(const char *file, DecodeData *mp3) {
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

  mp3->d_st0 = mp3->d_st;
  sceIoLseek32(mp3->hnd, mp3->d_st, PSP_SEEK_SET);

  if (!(mp3->c_buf = memory_alloc(sizeof(unsigned long) * 65)) ||
      !(mp3->d_buf = memory_alloc(1152 * 4)) ||
      !(mp3->o_buf[0] = memory_alloc(1152 * 4)) ||
      !(mp3->o_buf[1] = memory_alloc(1152 * 4)) ||
      !(mp3->r_buf = memory_alloc(4096)))
    return -1;

  our_buf = mp3->c_buf;
  memset(mp3->c_buf, 0, sizeof(unsigned long) * 65);
  memset(mp3->d_buf, 0, 1152 * 4);
  memset(mp3->o_buf[0], 0, 1152 * 4);
  memset(mp3->o_buf[1], 0, 1152 * 4);

  if (K_CALL(sceAudiocodecCheckNeedMem, mp3->c_buf, PSP_CODEC_MP3) < 0 ||
      K_CALL(sceAudiocodecGetEDRAM, mp3->c_buf, PSP_CODEC_MP3) < 0 ||
      K_CALL(sceAudiocodecInit, mp3->c_buf, PSP_CODEC_MP3) < 0)
    return -1;
  mp3->fr_sz = 0;
  mp3->o_num = 1;
  return 0;
}

int MP3_End(DecodeData *mp3) {
  K_CALL_VOID(sceAudiocodecReleaseEDRAM, mp3->c_buf);
  free_alloc(mp3->r_buf);
  free_alloc(mp3->c_buf);
  free_alloc(mp3->d_buf);
  free_alloc(mp3->o_buf[0]);
  free_alloc(mp3->o_buf[1]);
  mp3->r_buf = mp3->d_buf = mp3->o_buf[0] = mp3->o_buf[1] = NULL;
  mp3->c_buf = NULL;
  sceIoClose(mp3->hnd);
  mp3->hnd = -1;
  return 0;
}

int MP3_Decode(DecodeData *mp3) {
  u8 buf[4];
  if (sceIoRead(mp3->hnd, buf, 4) != 4)
    return -1;
  if ((mp3->fr_sz = Get_Framesize(buf)) <= 0)
    return -1;

  if (mp3->fr_sz > 4096)
    return -1;

  sceIoLseek32(mp3->hnd, mp3->d_st, PSP_SEEK_SET);
  if (sceIoRead(mp3->hnd, mp3->r_buf, mp3->fr_sz) != mp3->fr_sz)
    return -1;
  mp3->c_buf[6] = (unsigned long)mp3->r_buf;
  mp3->c_buf[8] = (unsigned long)mp3->d_buf;
  mp3->c_buf[7] = mp3->c_buf[10] = mp3->fr_sz;
  mp3->c_buf[9] = (1152 * 4);
  if (K_CALL(sceAudiocodecDecode, mp3->c_buf, PSP_CODEC_MP3) < 0)
    return -1;
  memcpy(mp3->o_buf[mp3->o_num ^= 1], mp3->d_buf, 1152 * 4);
  return ((mp3->d_st += mp3->fr_sz) >= mp3->f_sz) ? 1 : 0;
}

int Suspend_Handler(int id, char *name, void *prm, int *res) {
  if (id == 0x100 || id == 0x400) {
    r_flg = 1;
    sceKernelDelayThread(800000);

    sceKernelDelayThread(800000);
  }
  return 0;
}

PspSysEventHandler events = {0x40, "Suspend_Event", 0x0000FF00,
                             Suspend_Handler};

int vshbgm_thread(SceSize args, void *argp) {
  DecodeData mp3;
  while (!sceKernelFindModuleByName("sceVshCommonUtil_Module"))
    sceKernelDelayThread(100000);
  sceKernelDelayThread(6000000);
  sceUtilityLoadAvModule(PSP_AV_MODULE_AVCODEC);

  Hook();
  sceKernelDelayThread(3000000);
restart:
  while (MP3_Init("ms0:/bgm.mp3", &mp3) < 0)
    sceKernelDelayThread(2000000);
  while (chan < 0 || chan > 7) {
    chan = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1152,
                             PSP_AUDIO_FORMAT_STEREO);
    if (chan < 0)
      sceKernelDelayThread(500000);
  }
  r_flg = 0;
  int in_media = 0;
  while (!stop) {
    sceKernelDelayThread(10000);
    if (check_audio_active(_glen, chan)) {
      actv = 1;
      l_tim = sceKernelGetSystemTimeLow();
    } else if (actv && sceKernelGetSystemTimeLow() - l_tim > 200000) {
      actv = 0;
    }

    if (sceKernelFindModuleByName("music_browser_module") ||
        sceKernelFindModuleByName("msvideo_main_plugin_module") ||
        sceKernelFindModuleByName("video_plugin_module")) {
      actv = 1;
      l_tim = sceKernelGetSystemTimeLow();
      in_media = 1;
    } else if (in_media) {
      actv = 0;
      in_media = 0;
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
      mp3.d_st = mp3.d_st0;
      sceIoLseek32(mp3.hnd, mp3.d_st, PSP_SEEK_SET);
      continue;
    }
    sceAudioOutputBlocking(chan, PSP_AUDIO_VOLUME_MAX, mp3.o_buf[mp3.o_num]);
  }
  return 0;
}

int module_start(SceSize args, void *argp) {
  sceKernelRegisterSysEventHandler(&events);
  if ((thid = sceKernelCreateThread("vshbgm", vshbgm_thread, 0x1, 0x1000, 0,
                                    NULL)) >= 0)
    sceKernelStartThread(thid, 0, 0);
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
  if (thid >= 0) {
    sceKernelWaitThreadEnd(thid, NULL);
    sceKernelDeleteThread(thid);
    thid = -1;
  }
  sceKernelUnregisterSysEventHandler(&events);
  return 0;
}
