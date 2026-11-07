# LevelBrew Vocal Rider

*by Adin Walls*

![LevelBrew Vocal Rider](../../docs/levelbrew.png)

**Formats:** LV2, VST3 and VST2.

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

## Changelog

- **1.0** First release.

---

Part of **[Adin Brew](../../README.md)**. Installing, finding the plugin in your DAW and uninstalling are covered on the [main page](../../README.md#install-step-by-step).
