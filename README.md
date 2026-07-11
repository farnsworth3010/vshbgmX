# vshbgmX

vshbgmX is an enhanced fork of the original vshbgm Koutsie's PSP plugin.
It plays user MP3 in VSH (XMB) and prioritizes stable playback by auto-pausing in conflict-prone states.

## Demo 🎬

[<video src="https://github.com/farnsworth3010/vshbgmX/raw/refs/heads/main/demo/demo.mp4" width="320" height="240" controls></video>
](https://private-user-images.githubusercontent.com/110492020/620491366-a04d9c05-8ce7-460a-9889-732f5094ceb2.mp4?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3ODM4MDg2NTAsIm5iZiI6MTc4MzgwODM1MCwicGF0aCI6Ii8xMTA0OTIwMjAvNjIwNDkxMzY2LWEwNGQ5YzA1LThjZTctNDYwYS05ODg5LTczMmY1MDk0Y2ViMi5tcDQ_WC1BbXotQWxnb3JpdGhtPUFXUzQtSE1BQy1TSEEyNTYmWC1BbXotQ3JlZGVudGlhbD1BS0lBVkNPRFlMU0E1M1BRSzRaQSUyRjIwMjYwNzExJTJGdXMtZWFzdC0xJTJGczMlMkZhd3M0X3JlcXVlc3QmWC1BbXotRGF0ZT0yMDI2MDcxMVQyMjE5MTBaJlgtQW16LUV4cGlyZXM9MzAwJlgtQW16LVNpZ25hdHVyZT05ZTAyYmRhNTRhNDgwNzJhNjhiYWM3MWU1YzYzNzhhZGVkYTA1Mzg3NjkxYmYwODdhMjY4MmFlOWMyNzMwZWUyJlgtQW16LVNpZ25lZEhlYWRlcnM9aG9zdCZyZXNwb25zZS1jb250ZW50LXR5cGU9dmlkZW8lMkZtcDQifQ.lzAtXfXns9T2pk4dc4lUc7D0fUP5bOGunALhuU63G0k)

## Highlights ✨

- Background music playback in XMB.
- Startup delay to avoid boot-time crackling due to VSH initialization.
- Auto-pause when other audio activity is detected.
- Auto-pause when system mute is enabled or system volume is zero.
- Auto-pause while ARK VSH overlay/menu is active.
- Auto-pause during XMB dim/underclock power-saving states to avoid audio issues and save battery.
- Auto-pause on heavy Memory Stick I/O spikes.
- Unified config support via `ms0:/seplugins/vshbgmX_config.txt`.

## Quick Setup (PSP) 🚀

1. Download `vshbgmX.prx` from the Releases page of this repository.
2. Disable the original `vshbgm` plugin in your plugin manager/config or in ARK Plugins Manager (to avoid conflicts and double audio).
3. Put the plugin at `ms0:/seplugins/vshbgmX.prx`.
4. Add and enable the plugin (in `vsh.txt` or in ARK Plugins Manager):
  - `vsh, ms0:/seplugins/vshbgmX.prx, on`
5. Put your music file at one of these paths:
   - `ms0:/seplugins/cxmb/bgm.mp3`
   - `ms0:/bgm.mp3`
6. Configure behavior:
   - Start from `vshbgmX_config.example.txt`
  - Deploy as `ms0:/seplugins/vshbgmX_config.txt`
   - If this file is missing, the plugin auto-creates it on first start.

## Audio Format 🎵

Recommended MP3 format:

- Sample rate: `44.1 kHz`
- Bitrate: `128 kbps` or `192 kbps`
- Format: `MP3`

FFmpeg conversion example:

```bash
ffmpeg -i input.mp3 -ar 44100 -b:a 192k bgm.mp3
```

Online converter option:

Use the original website's online converter tool:
https://the-sauna.icu/vshbgm/


## Configuration ⚙️

Use template file in repo root as an example:

- `vshbgmX_config.example.txt`

Parameter reference:

- `enable_plugin`
  - Master on/off switch for plugin logic.
  - `1` = enabled, `0` = disabled.
- `volume_percent`
  - BGM output level (0..100).
  - Practical range is usually `30..70`.
- `baseline_cpu`
  - Baseline CPU MHz used by idle-dim pause detection.
  - If current CPU falls below this value and `enable_idle_dim_pause=1`, playback pauses.
- `first_playback_delay_us`
  - Delay before first playback after plugin startup.
  - Helps avoid boot-time crackling while VSH is still stabilizing.
- `loop_sleep_us`
  - Main loop sleep interval.
  - Lower values react faster but increase wakeups/CPU overhead.
- `pause_sleep_us`
  - Sleep interval while playback is paused by conditions.
  - Prevents busy-looping during pause states.
- `check_interval_loops`
  - How often pause conditions are re-evaluated.
  - Lower = faster reaction, higher = less overhead.
- `io_spike_threshold_us`
  - Decode-time threshold that marks an I/O spike.
  - Spikes trigger temporary pause when `enable_io_pause=1`.
- `io_pause_window_us`
  - Pause duration after an I/O spike is detected.
  - Increase this if crackling appears during heavy folder browsing.
- `audio_resume_delay_us`
  - Delay before resuming BGM after external audio ends.
  - Reduces short BGM leaks between system/game audio transitions.
- `enable_idle_dim_pause`
  - Pause BGM during idle dim/underclock state.
- `enable_mute_pause`
  - Pause BGM when mute is active or system volume is zero.
- `enable_overlay_pause`
  - Pause BGM while ARK/VSH overlay is open.
- `enable_io_pause`
  - Enable automatic pause on detected Memory Stick I/O spikes.

Units:

- Any key ending with `_us` uses microseconds.
- Boolean flags use `1` or `0`.

## Troubleshooting 🛠️

- No music plays:
  - Verify plugin line path and that VSH plugin is enabled.
  - Verify MP3 path is exactly one of the supported locations.
  - Re-encode the MP3 with the FFmpeg command above.
- Crackling/stutter:
  - Keep `enable_io_pause=1` and `enable_idle_dim_pause=1`.
  - Increase `io_pause_window_us` slightly.
- Unexpected pauses:
  - Check `enable_mute_pause` and system mute/volume state.
  - Check if ARK overlay is open.

## Known Issues ⚠️

Reports are highly appreciated. 

- Despite the I/O spike mitigation, audio can still occasionally hang/stutter in some scenarios while opening different sections or large folders.

## Build From Source

### Requirements

- GNU Make
- PSPSDK toolchain

### Toolchain Links

- https://pspdev.github.io/installation.html

### Build Commands

```bash
make
./build.sh
make release
```

## Compatibility

Tested environment:

- PSP-3006
- Firmware: 6.61 ARK-5 cIPL

Compatibility with other PSP models, firmware variants, CFW setups, or plugin stacks is not guaranteed.

Battery/performance impact note:

- Battery life and system performance impact were not formally benchmarked/tested yet.

## Credits 🙌

- Original vshbgm author.
- ARK Team.
- PSP-Archive Team.
- PSP Homebrew Community.
- Reddit - r/psp

## References

- Original source: https://github.com/PSP-Archive/vshbgm
- Original website: https://the-sauna.icu/vshbgm/
- ARK-5 source: https://github.com/PSP-Arkfive/ARK-5
- https://github.com/PSP-Archive/MP3PlayerPlugin/
- https://github.com/PSP-Archive/ARK-4/
- https://github.com/PSP-Archive/CXMB

## Contributing

Contributions are welcome.

- Bug fixes and stability improvements are appreciated.
- Reports with tested combinations are appreciated, include:
  - PSP model
  - firmware/CFW version
  - plugin behavior in your setup

## TODO

Completed:

- [x] First-playback startup delay.
- [x] Resume cooldown after external audio.
- [x] Pause on XMB idle dim / underclock.
- [x] Pause on mute or zero system volume.
- [x] Pause while ARK overlay/menu is active.
- [x] Pause on Memory Stick I/O spikes.
- [x] Unified config support via `ms0:/seplugins/vshbgmX_config.txt`.
- [x] Config template file `vshbgmX_config.example.txt`.

Planned:

- [ ] Add a dedicated settings entry in PSP system settings (or equivalent dedicated settings page) for runtime plugin control (enable/disable and future options).
- [ ] Implement this only with a safe, firmware-validated hook path to avoid boot-time instability.
- [ ] Switching tracks.
- [ ] Changing tracks from settings menu.
- [ ] Fade-in/fade-out transitions.

## License

This project is licensed under GPL-2.0:
https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html

## Disclaimer

This software is provided "as is", without warranty of any kind.
The author is not responsible for damage, data loss, malfunction, or other issues caused by use of this plugin.
