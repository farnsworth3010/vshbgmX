/*
 * vshbgmX - system menu music, a fork of vshbgm
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
#include <pspimpose_driver.h>
#include <psppower.h>
#include <psprtc.h>
#include <pspsdk.h>
#include <pspsysevent.h>
#include <psputility.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("vshbgmX", 0x1000, 1, 0);
PSP_MAIN_THREAD_ATTR(0);

#define VSHBGMX_CONFIG_PATH "ms0:/seplugins/vshbgmX_config.txt"
#define CXMB_MP3_PATH "ms0:/seplugins/cxmb/bgm.mp3"
#define ROOT_MP3_PATH "ms0:/bgm.mp3"

typedef struct {
  SceUID hnd;
  int f_sz, d_st, d_st0, fr_sz, o_num;
  u8 *r_buf, *d_buf, *o_buf[2];
  unsigned long *c_buf;
} DecodeData;

typedef struct {
  int enable_plugin;
  int volume_percent;
  int baseline_cpu;
  int first_playback_delay_us;
  int loop_sleep_us;
  int pause_sleep_us;
  int check_interval_loops;
  int io_spike_threshold_us;
  int io_pause_window_us;
  int audio_resume_delay_us;
  int enable_idle_dim_pause;
  int enable_mute_pause;
  int enable_overlay_pause;
  int enable_io_pause;
} BgmConfig;

static SceUID bgm_thid = -1;
static int r_flg = 0, chan = -1, stop = 0;
static volatile int actv = 0;
static volatile u32 l_tim = 0;
static volatile int g_plugin_enabled = 1;

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

static int is_ark_overlay_active(void) {
  // Different CFW builds expose different module names for VSH/ARK overlay.
  if (sceKernelFindModuleByName("satelite_plugin_module"))
    return 1;
  if (sceKernelFindModuleByName("satelite_plugin"))
    return 1;
  if (sceKernelFindModuleByName("vshmenu_plugin_module"))
    return 1;
  if (sceKernelFindModuleByName("vshmenu_plugin"))
    return 1;

  // Some variants are easier to detect by worker thread name.
  if (get_thread_id("Satelite_Thread") >= 0)
    return 1;
  if (get_thread_id("VshMenu_Thread") >= 0)
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

static int SaveConfig(const BgmConfig *cfg) {
  static char out[4096];
  int len = snprintf(
      out, sizeof(out),
      "# vshbgmX config example\n"
      "# Place this file on PSP as:\n"
      "#   ms0:/seplugins/vshbgmX_config.txt\n"
      "# Compact template: detailed explanations are in README.\n"
      "# All *_us values are in microseconds.\n"
      "# Feature flags: 1 = enabled, 0 = disabled.\n"
      "\n"
      "# Master enable switch\n"
      "enable_plugin=%d\n"
      "\n"
      "# BGM volume in percent (0..100)\n"
      "volume_percent=%d\n"
      "\n"
      "# Baseline CPU clock for idle-dim detection\n"
      "baseline_cpu=%d\n"
      "\n"
      "# Delay before first playback\n"
      "first_playback_delay_us=%d\n"
      "\n"
      "# Main loop sleep\n"
      "loop_sleep_us=%d\n"
      "\n"
      "# Sleep while paused\n"
      "pause_sleep_us=%d\n"
      "\n"
      "# Pause-state check interval\n"
      "check_interval_loops=%d\n"
      "\n"
      "# Decode-time threshold for IO contention\n"
      "io_spike_threshold_us=%d\n"
      "\n"
      "# Pause window after IO spike\n"
      "io_pause_window_us=%d\n"
      "\n"
      "# Delay before resuming after external audio\n"
      "audio_resume_delay_us=%d\n"
      "\n"
      "# Pause on idle dim/underclock\n"
      "enable_idle_dim_pause=%d\n"
      "\n"
      "# Pause on mute or zero volume\n"
      "enable_mute_pause=%d\n"
      "\n"
      "# Pause while overlay/menu is active\n"
      "enable_overlay_pause=%d\n"
      "\n"
      "# Enable auto-pause on IO spikes\n"
      "enable_io_pause=%d\n",
      cfg->enable_plugin ? 1 : 0, cfg->volume_percent, cfg->baseline_cpu,
      cfg->first_playback_delay_us, cfg->loop_sleep_us, cfg->pause_sleep_us,
      cfg->check_interval_loops, cfg->io_spike_threshold_us,
      cfg->io_pause_window_us, cfg->audio_resume_delay_us,
      cfg->enable_idle_dim_pause ? 1 : 0, cfg->enable_mute_pause ? 1 : 0,
      cfg->enable_overlay_pause ? 1 : 0, cfg->enable_io_pause ? 1 : 0);

  if (len <= 0 || len >= (int)sizeof(out))
    return -1;

  sceIoMkdir("ms0:/seplugins", 0777);

  SceUID fd = sceIoOpen(VSHBGMX_CONFIG_PATH,
                        PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd < 0)
    return -1;

  int written = sceIoWrite(fd, out, len);
  sceIoClose(fd);
  return (written == len) ? 0 : -1;
}

static void set_default_config(BgmConfig *cfg) {
  // Master enable switch for the plugin.
  cfg->enable_plugin = 1;
  // BGM output volume in percent (0..100).
  cfg->volume_percent = 50;
  // Base XMB CPU clock used by idle-dim detection fallback.
  cfg->baseline_cpu = 133;
  // Delay before the very first BGM start after plugin load.
  cfg->first_playback_delay_us = 5000000;
  // Main loop tick and pause-loop sleep.
  cfg->loop_sleep_us = 10000;
  cfg->pause_sleep_us = 100000;
  // How often pause state checks run (in loop ticks).
  cfg->check_interval_loops = 3;
  // I/O stutter mitigation thresholds.
  cfg->io_spike_threshold_us = 50000;
  cfg->io_pause_window_us = 1200000;
  // Delay before BGM resumes after external audio activity.
  cfg->audio_resume_delay_us = 3000000;

  // Added pause features are enabled by default.
  cfg->enable_idle_dim_pause = 1;
  cfg->enable_mute_pause = 1;
  cfg->enable_overlay_pause = 1;
  cfg->enable_io_pause = 1;
}

static int read_config_int(const char *buf, const char *key, int def_val) {
  int key_len = strlen(key);
  const char *p = buf;

  while (p && *p) {
    if ((p == buf || p[-1] == '\n' || p[-1] == '\r') &&
        strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
      return simple_atoi(p + key_len + 1);
    }

    const char *next = strchr(p, '\n');
    if (!next)
      break;
    p = next + 1;
  }

  return def_val;
}

static void write_default_config(void) {
  BgmConfig cfg;
  set_default_config(&cfg);
  SaveConfig(&cfg);
}

static void LoadConfig(BgmConfig *cfg) {
  set_default_config(cfg);

  SceUID fd = sceIoOpen(VSHBGMX_CONFIG_PATH, PSP_O_RDONLY, 0777);
  if (fd < 0) {
    write_default_config();
    return;
  }

  static char buf[4096];
  int len = sceIoRead(fd, buf, sizeof(buf) - 1);
  sceIoClose(fd);
  if (len <= 0)
    return;

  buf[len] = '\0';
  cfg->enable_plugin =
      read_config_int(buf, "enable_plugin", cfg->enable_plugin) != 0;
  cfg->volume_percent =
      read_config_int(buf, "volume_percent", cfg->volume_percent);
  cfg->baseline_cpu = read_config_int(buf, "baseline_cpu", cfg->baseline_cpu);
  cfg->first_playback_delay_us =
      read_config_int(buf, "first_playback_delay_us", cfg->first_playback_delay_us);
  cfg->loop_sleep_us = read_config_int(buf, "loop_sleep_us", cfg->loop_sleep_us);
  cfg->pause_sleep_us = read_config_int(buf, "pause_sleep_us", cfg->pause_sleep_us);
  cfg->check_interval_loops =
      read_config_int(buf, "check_interval_loops", cfg->check_interval_loops);
  cfg->io_spike_threshold_us =
      read_config_int(buf, "io_spike_threshold_us", cfg->io_spike_threshold_us);
  cfg->io_pause_window_us =
      read_config_int(buf, "io_pause_window_us", cfg->io_pause_window_us);
  cfg->audio_resume_delay_us =
      read_config_int(buf, "audio_resume_delay_us", cfg->audio_resume_delay_us);
  cfg->enable_idle_dim_pause =
      read_config_int(buf, "enable_idle_dim_pause", cfg->enable_idle_dim_pause) != 0;
  cfg->enable_mute_pause =
      read_config_int(buf, "enable_mute_pause", cfg->enable_mute_pause) != 0;
  cfg->enable_overlay_pause =
      read_config_int(buf, "enable_overlay_pause", cfg->enable_overlay_pause) != 0;
  cfg->enable_io_pause =
      read_config_int(buf, "enable_io_pause", cfg->enable_io_pause) != 0;

  g_plugin_enabled = cfg->enable_plugin ? 1 : 0;

  if (cfg->volume_percent < 0)
    cfg->volume_percent = 0;
  if (cfg->volume_percent > 100)
    cfg->volume_percent = 100;
  if (cfg->baseline_cpu <= 0)
    cfg->baseline_cpu = 133;
  if (cfg->first_playback_delay_us < 0)
    cfg->first_playback_delay_us = 0;
  if (cfg->loop_sleep_us < 1000)
    cfg->loop_sleep_us = 1000;
  if (cfg->pause_sleep_us < 10000)
    cfg->pause_sleep_us = 10000;
  if (cfg->check_interval_loops < 1)
    cfg->check_interval_loops = 1;
  if (cfg->io_spike_threshold_us < 1000)
    cfg->io_spike_threshold_us = 1000;
  if (cfg->io_pause_window_us < 0)
    cfg->io_pause_window_us = 0;
  if (cfg->audio_resume_delay_us < 0)
    cfg->audio_resume_delay_us = 0;
}

static int is_idle_dimmed_clock(int baseline_cpu) {
  int cur = scePowerGetCpuClockFrequencyInt();
  if (cur <= 0 || baseline_cpu <= 0)
    return 0;

  // During XMB idle dimming, CPU frequency is reduced below normal baseline.
  // Any drop below baseline pauses BGM to prevent crackling sound.
  return (cur < baseline_cpu);
}

static int is_audio_muted_or_zero(void) {
  // Global system mute (1 = muted, 0 = unmuted).
  int mute = sceImposeGetParam(PSP_IMPOSE_MUTE);
  if (mute > 0)
    return 1;

  // Global XMB/main volume. If it is 0, skip BGM output.
  int main_volume = sceImposeGetParam(PSP_IMPOSE_MAIN_VOLUME);
  if (main_volume == 0)
    return 1;

  return 0;
}

static int vshbgmX_thread(SceSize args, void *argp) {
  DecodeData mp3;
  BgmConfig cfg;
  int first_playback = 1;
  set_default_config(&cfg);

  // If decode time spikes under heavy Memory Stick I/O, temporarily pause BGM
  // instead of outputting stuttered chunks.
  u32 io_pause_until = 0;

  while (!sceKernelFindModuleByName("sceVshCommonUtil_Module"))
    sceKernelDelayThread(100000);
  while (!sceKernelFindModuleByName("scePaf_Module"))
    sceKernelDelayThread(100000);
  while (!sceKernelFindModuleByName("sceVshCommonGui_Module"))
    sceKernelDelayThread(100000);

  // Load/create config before audio module load and hook setup.
  LoadConfig(&cfg);

  sceKernelDelayThread(1000000);
  sceUtilityLoadAvModule(PSP_AV_MODULE_AVCODEC);

  Hook();
  sceKernelDelayThread(3000000);

restart:;
  while (1) {
    if (MP3_Init(CXMB_MP3_PATH, &mp3) == 0)
      break;
    if (MP3_Init(ROOT_MP3_PATH, &mp3) == 0)
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
  // Set when low-power idle dimming is detected.
  int pause_for_idle_dim = 0;
  // Set when system audio is muted or global volume is zero.
  int pause_for_mute = 0;
  // Set when ARK overlay / VSH menu is open.
  int pause_for_overlay = 0;

  // Baseline CPU for idle-dim detection is taken from config.
  // If that value is invalid, fall back to current CPU clock, then 133 MHz.
  int baseline_cpu = cfg.baseline_cpu;
  if (baseline_cpu <= 0) {
    baseline_cpu = scePowerGetCpuClockFrequencyInt();
    if (baseline_cpu <= 0)
      baseline_cpu = 133;
  }

  // Delay only the very first playback start after module load.
  if (first_playback) {
    first_playback = 0;
    sceKernelDelayThread(cfg.first_playback_delay_us);
  }

  int vol = cfg.volume_percent;
  int max_vol = (0x8000 * vol) / 100;

  while (!stop) {
    u32 now = sceKernelGetSystemTimeLow();
    sceKernelDelayThread(cfg.loop_sleep_us);

    if (!g_plugin_enabled) {
      sceKernelDelayThread(cfg.pause_sleep_us);
      continue;
    }

    if (check_counter++ >= cfg.check_interval_loops) {
      check_counter = 0;
      if (bgm_check_audio_active(_glen, chan)) {
        actv = 1;
        l_tim = sceKernelGetSystemTimeLow();
      } else if (actv &&
                 sceKernelGetSystemTimeLow() - l_tim >
                     (u32)cfg.audio_resume_delay_us) {
        actv = 0;
      }

      if (is_player_active()) {
        actv = 1;
        l_tim = sceKernelGetSystemTimeLow();
        in_media = 1;
      } else if (in_media) {
        // Media playback just ended: keep BGM paused until cooldown elapses.
        actv = 1;
        l_tim = sceKernelGetSystemTimeLow();
        in_media = 0;
      }

      // Re-check power-saving state periodically, not every loop iteration.
        pause_for_idle_dim =
          cfg.enable_idle_dim_pause ? is_idle_dimmed_clock(baseline_cpu) : 0;

      // Re-check system mute/volume state periodically.
        pause_for_mute = cfg.enable_mute_pause ? is_audio_muted_or_zero() : 0;

      // Pause BGM while ARK overlay is active to avoid UI-time stutter.
        pause_for_overlay =
          cfg.enable_overlay_pause ? is_ark_overlay_active() : 0;
    }
    if (r_flg) {
      MP3_End(&mp3);
      r_flg = 0;
      sceKernelDelayThread(2000000);
      goto restart;
    }

    // During active Memory Stick bursts, wait until the temporary pause window
    // elapses so playback resumes cleanly instead of crackling.
    if (io_pause_until != 0 && (s32)(io_pause_until - now) > 0) {
      sceKernelDelayThread(cfg.pause_sleep_us);
      continue;
    }

    // Pause BGM while any other audio is active, XMB is dimmed/underclocked,
    // or system sound is muted/volume=0.
    if (actv || pause_for_idle_dim || pause_for_mute || pause_for_overlay) {
      sceKernelDelayThread(cfg.pause_sleep_us);
      continue;
    }

    u32 decode_start = sceKernelGetSystemTimeLow();
    int res = MP3_Decode(&mp3);
    u32 decode_time = sceKernelGetSystemTimeLow() - decode_start;

    // A large decode-time spike usually means MS I/O contention while browsing
    // game folders. Pause briefly and resume once disk activity settles.
    if (cfg.enable_io_pause) {
      if (decode_time > (u32)cfg.io_spike_threshold_us) {
        io_pause_until = sceKernelGetSystemTimeLow() + (u32)cfg.io_pause_window_us;
        sceKernelDelayThread(cfg.pause_sleep_us);
        continue;
      }
    }

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
  if ((bgm_thid = sceKernelCreateThread("vshbgmX", vshbgmX_thread, 0x12, 0x4000,
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
