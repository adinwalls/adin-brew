# Adin Brew

*Vocal plugins for Linux by Adin Walls*

| Plugin | What it does |
|---|---|
| **[TuneBrew Autotune](#tunebrew-autotune)** | Real-time pitch correction that tunes hard and still sounds human. Keeps vibrato at 0 ms retune, detects the key, and has a classic hard-tune mode. |
| **[LevelBrew Vocal Rider](#levelbrew-vocal-rider)** | Rides the level of a recorded vocal. It reads the whole take first, then you watch it ride the fader during playback. |

**Formats:** LV2, VST3 and VST2 (64-bit Linux). TuneBrew also comes as CLAP. They work in Harrison Mixbus, Ardour, REAPER, Bitwig Studio, Carla and other Linux DAWs.

![TuneBrew Autotune](docs/screenshot.png)

---

## Install: step by step

### What you need

- 64-bit Linux with **glibc 2.35 or newer**: Ubuntu / Pop!_OS / Linux Mint 22.04+, Fedora 36+, Debian 12+, Arch, Manjaro.
- A Linux DAW that loads LV2, VST3 or CLAP plugins.

### Option 1: download the ready-made plugins (easiest)

1. Download **[adin-brew-linux-x86_64.tar.gz](https://github.com/adinwalls/adin-brew/raw/main/dist/adin-brew-linux-x86_64.tar.gz)** (about 2 MB). It'll go to your Downloads folder.
   Or download it from a terminal:
   ```bash
   wget -P ~/Downloads https://github.com/adinwalls/adin-brew/raw/main/dist/adin-brew-linux-x86_64.tar.gz
   ```
2. Open a terminal and go to your Downloads folder:
   ```bash
   cd ~/Downloads
   ```
3. Unpack it:
   ```bash
   tar xzf adin-brew-linux-x86_64.tar.gz
   ```
4. Go into the folder and run the installer:
   ```bash
   cd adin-brew-linux-x86_64
   ./install.sh
   ```
   It copies both plugins into your home folder (`~/.lv2`, `~/.vst3`, `~/.clap`, `~/.vst`), so no `sudo` is needed.
5. Open your DAW and **rescan plugins** (see [Finding them in your DAW](#finding-them-in-your-daw)).
6. Add **TuneBrew Autotune** or **LevelBrew Vocal Rider** to your vocal track. Done!

### Option 2: build them from source

1. Install the build tools.

   Ubuntu / Pop!_OS / Mint / Debian:
   ```bash
   sudo apt install git build-essential pkg-config libcairo2-dev libx11-dev libxext-dev
   ```
   Fedora:
   ```bash
   sudo dnf install git gcc-c++ make pkgconf cairo-devel libX11-devel libXext-devel
   ```
   Arch / Manjaro:
   ```bash
   sudo pacman -S --needed git base-devel cairo libx11 libxext
   ```
2. Download the source, including the DPF framework they're built on:
   ```bash
   git clone --recursive https://github.com/adinwalls/adin-brew.git
   cd adin-brew
   ```
3. Build and install:
   ```bash
   ./install.sh
   ```
4. Rescan plugins in your DAW.

To update later: `git pull && git submodule update --init --recursive && ./install.sh --rebuild`

### Finding them in your DAW

Menu names vary a little between versions.

- **Ardour / Harrison Mixbus:** LV2 plugins are found on start-up. If one is missing, open *Window → Plugin Manager* and choose **Re-scan**.
- **REAPER:** *Options → Preferences → Plug-ins → VST* (or *CLAP*), then **Re-scan**. Use the VST3 version (or CLAP for TuneBrew).
- **Bitwig Studio:** *Settings → Locations → Plug-ins*: make sure your user VST3/CLAP folders are enabled, then rescan.
- **Carla:** *Add Plugin → Refresh*.

They show up as **TuneBrew Autotune** and **LevelBrew Vocal Rider** (maker: Adin Walls).

### Uninstall

```bash
./uninstall.sh
```
Or delete `TuneBrew.*` / `LevelBrew.*` from `~/.lv2`, `~/.vst3`, `~/.clap` and `~/.vst`.

---

# TuneBrew Autotune

## Quick start

1. Put TuneBrew **first** on the lead vocal track, before compression, EQ, reverb or delay.
2. Press **DETECT KEY**, play 15–30 seconds of the vocal, then press **APPLY**. Or click the key's note and choose a scale yourself.
3. Pick a style:
   - **Natural pop vocal:** NATURAL, Retune 10–20 ms. The defaults are a good start.
   - **Aggressive but human:** NATURAL, Retune 0 ms, Flex 0, Humanize 0.
   - **T-Pain / robot:** CLASSIC, Retune 0 ms.

Example audio is in [`demo/`](demo). Compare `vibrato_vocal_hardtune_CLASSIC.wav` with `vibrato_vocal_hardtune_NATURAL.wav`.

## What it does

- **Natural mode (default):** tunes hard but still sounds human.
  - **Vibrato** (0–200%): the singer's vibrato is detected and kept even at 0 ms retune, because only the note centre is corrected. 100% keeps it untouched, 0% flattens it and 200% doubles it.
  - **Flex** (%): only corrects notes sung near the target, so deliberate bends and blue notes pass through.
  - **Humanize** (%): short notes snap; long held notes are corrected more gently so they keep their life.
  - **Transition** (ms): note attacks, scoops into notes and slides between notes are let through instead of being stair-stepped.
- **Classic mode:** the plain hard-tune sound, with the Natural knobs dimmed and bypassed.
- **Detect Key:** press it and play 15–30 s of the vocal. It shows the key with a confidence meter, the runner-up key, and the recording's tuning offset. **Apply** sets the Key, the Scale and Reference A4 in one click.
- **Live pitch graph:** what was sung (amber) against what comes out (teal), plus a note readout, a flat/sharp LED meter, a correction LED ladder and vibrato depth.

## The editor

- **Key strip:** click a note to set the key. In-scale notes are lit and the note being corrected to is outlined.
- **Scale menu:** click it or scroll over it.
- **Knobs:** drag, hold **Shift** for fine control, scroll to nudge, **double-click** to reset.
- **Resizing:** the window keeps its aspect ratio and is HiDPI aware.

## Controls

| Control | What it does |
|---|---|
| **Key / Scale** | Notes it snaps to. *Chromatic* = nearest semitone |
| **Classic / Natural** | Mode, see above |
| **Retune** (ms) | **0** = instant. **10–30** = tight pop. **40–80** = soft |
| **Vibrato / Flex / Humanize / Transition** | Natural-mode controls, see above |
| **Amount** (%) | How far toward the target note to pull |
| **Transpose** (st) | Shift ±12 semitones (formants preserved) |
| **Ref A4** (Hz) | Tuning reference (Detect Key can set it) |
| **Mix / Gain** | Dry/wet and output level |

### Starting points

- **"Natural" pop vocal:** Natural, Retune 10–20, Vibrato 100, Flex 20, Humanize 30, Transition 40.
- **Aggressive but human (modern rap/pop):** Natural, Retune 0, Vibrato 100, Flex 0, Humanize 0, Transition 20.
- **T-Pain / robot:** Classic, Retune 0.
- **Transparent touch-up:** Natural, Retune 40, Amount 80, Humanize 60.

## Tips

- It needs a clean lead vocal (no backing vocals or instrument bleed), and works best first in the chain.
- Latency is ~49 ms at 48 kHz. It's reported to the host, so playback is compensated automatically. If you hear lag while tracking, monitor dry and add the plugin afterwards.
- Pick the song's actual key/scale. Chromatic works for anything, but it'll also "correct" deliberate blue notes.
- Tracks down to 70 Hz (deep bass voices) and up to 1.1 kHz.

## How it works

- **Detection:** YIN on a 1 kHz low-passed, decimated signal, with a median filter against octave jumps and a level gate. Pitch is analysed every ~5 ms and interpolated per pitch period.
- **Targeting:** key/scale quantiser with hysteresis, so it won't flip between notes.
- **Vibrato separation:** the pitch's turning points are tracked. A regular 3–9 Hz swing of similar size is classed as vibrato, its centre becomes the note being corrected, and the swing is added back (scaled by the Vibrato knob).
- **Key detection:** time spent on each pitch is counted in 10-cent bins, ignoring slides. A circular mean gives the tuning offset, the bins fold into 12 pitch classes, and those are correlated against Krumhansl-Kessler major/minor profiles. Tested on 24 keys × 2 seed sets with random ±45-cent tuning: 47 of 48 correct, and the one miss reported 6% confidence.
- **Retune:** the correction offset is smoothed with a one-pole filter. Fast settings flatten vibrato; slow settings let it through.
- **Shifting:** TD-PSOLA. Pitch marks are tracked cycle-to-cycle by waveform similarity, so grains always overlap in phase: no phasiness or "underwater" smear. With zero correction the output is a bit-exact copy of the input, and formants are preserved.

Measured on synthetic vocals with ±40-cent detuning and vibrato: mean error after hard-tune is **0.2–0.5 cents** (from ~23 cents). Hard-tuned output sits within 7.7 dB log-spectral distance of a perfect-pitch reference (v1.1: 11.8 dB). Pass-through at zero correction measures 50+ dB SNR. Tested at 44.1, 48, 88.2 and 96 kHz. CPU is around 2% of one core, stereo, at 48 kHz.

# LevelBrew Vocal Rider

![LevelBrew Vocal Rider](docs/levelbrew.png)

A vocal rider for takes you've already recorded. Instead of reacting as the audio goes past, LevelBrew **reads the whole vocal file first**, so it knows every phrase in advance and can ride the level smoothly, the way you'd ride a fader by hand. Then, during playback, you **watch it work**: a scrolling view of the original level, the levelled vocal and the ride curve, plus a fader that moves with the ride.

## Quick start

1. Put LevelBrew **first** on the vocal track, before EQ, compression or effects.
2. Click **LOAD** and choose the recorded vocal file (WAV). In Mixbus/Ardour the recordings are in your session folder under `interchange/<session name>/audiofiles/`.
3. Press **play**. Within a few seconds of singing, LevelBrew recognises the take and locks to it ("Locked: take starts at ..."). From then on it rides the vocal wherever you play, loop or seek.
4. Adjust **Strength**, **Range** and **Speed** to taste. The defaults are a moderate, natural ride.

The sync position is saved with your session, so next time it rides straight away.

## How it behaves

- **Reads the file without playback.** The whole take is analysed in the background in well under a second.
- **Watches the file.** If the WAV changes on disk (re-recorded, edited, bounced over), LevelBrew re-reads it by itself once it stops changing, and keeps riding with the old version until the new one is ready.
- **Finds the take on the timeline by itself.** It compares what it hears during playback with the file, so you don't have to type in where the region starts. If you move the region, it notices within a few seconds and re-syncs. **RE-SYNC** forces a fresh search.
- **Doesn't pump.** Silences, breaths and "S" sounds are ignored when judging the level, gaps hold the neighbouring gain instead of jumping, and long silences relax back to 0 dB.
- **No latency** and no lookahead delay: the ride is worked out in advance.

## Controls

| Control | What it does |
|---|---|
| **Strength** (%) | How much of the level difference to even out. 100% = fully level, 50% = halfway. Default 85% |
| **Range** (dB) | Largest boost or cut it may apply. Default ±9 dB |
| **Speed** (s) | How slowly it moves. Small = follows individual words, large = follows whole phrases. Default 0.4 s |
| **Output** (dB) | Output trim |
| **Bypass** | Compare with the original. The ride fades in and out over 300 ms, so there's no click |
| **LOAD** | Choose the vocal file |
| **RE-SYNC** | Find the take's timeline position again |

## Good to know

- It rides **one take (file)**. If your vocal track is a comp of several takes, load the main one. Regions from other files simply pass through unchanged.
- Keep it first in the chain: it recognises the take by comparing its input with the file, so heavy processing before it makes syncing harder.
- WAV/BWF files of any common bit depth (16/24/32-bit, float) and sample rate are supported, mono or stereo.
- Not available as CLAP: DPF passes CLAP hosts a steady sample counter instead of the song position, which LevelBrew needs. Use LV2 or VST3.

## How it works

- **Level detection:** 40 ms windows every 10 ms, high-passed at 90 Hz so rumble doesn't count. "S" sounds are detected by their energy above 5 kHz and left out of the level estimate.
- **Singing detection:** frames within 24 dB of the take's loud passages count as singing, with a 70 ms majority filter against flicker.
- **Ride curve:** the local loudness is an energy average over nearby singing only (Gaussian, width set by Speed), compared to the take's own median level, scaled by Strength, limited by Range and smoothed again over ~120 ms.
- **Sync:** during playback the plugin logs 10 ms input levels and slides them along the file's own levels; a near-perfect correlation (≥ 0.97, clearly ahead of any repeated chorus) locks the position. While locked it re-checks every 2 s.

Measured on a real 3:50 vocal: the phrase-to-phrase level swing went from 2.3 dB to 0.9 dB at the default settings, using −4 to +6 dB of gain. In an end-to-end test of the plugin binary, it found the take's position to within 1 ms, matched the planned ride to within 0.01 dB, and re-read a changed file within 2.5 s.

---

## Build details

`make` builds every format into `bin/`, and `make install` (or `./install.sh`) copies them to your home plugin folders.

Built on [DPF](https://github.com/DISTRHO/DPF) (ISC licence), included as a git submodule in `dpf/`.

- **TuneBrew:** the DSP lives in `src/TuneEngine.hpp`, a header-only engine you can reuse, and the editor is `src/TuneView.hpp`.
- **LevelBrew:** the analysis, ride curve, sync and player live in `src/LevelEngine.hpp`, and the editor is `src/LevelView.hpp`.

Both editors are plain Cairo with no framework dependency. Test programs are in `test/`. For example, `test/level_vsthost.cpp` drives the LevelBrew binary like a DAW does.

## Changelog

### LevelBrew

- **1.0** First release.

### TuneBrew

- **1.4.3** Stronger lamp and LED glow.
- **1.4.2** Plastic push buttons with glowing side lamps (key, mode, detect, apply/stop).
- **1.4.1** Tuning-fork logo.
- **1.4** New rack-style editor: wooden cheeks, navy module faceplates, coloured knob caps with printed scales, push buttons, LED tuning meter, LED shift ladder and SIG/VIB status LEDs.
- **1.3** Natural mode (vibrato kept at hard tune, Flex, Humanize, Transition), Classic/Natural switch, Detect Key with tuning detection, larger editor.
- **1.2** Fixed the "underwater"/phasey sound. Pitch marks are now waveform-locked, interpolation is cubic and the timing offset is stable. Latency rose from 36 to 49 ms (compensated by the host).
- **1.1** Custom editor: pitch graph, key strip, scale menu, tuning meter.
- **1.0** First release.

## Licence

TuneBrew and LevelBrew are free software under the **GNU General Public License v3.0**; see [LICENSE](LICENSE).
The DPF framework is ISC-licensed. The speech in `demo/` comes from a public-domain LibriVox recording (via the librosa example data); the other demo vocals are synthesised.
