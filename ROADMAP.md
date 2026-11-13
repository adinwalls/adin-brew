# Vox Synth Tune: roadmap

A vocal tool in the spirit of Controlla Voice and Audimee: strong pitch correction first, then neural voice-to-voice. Built as an offline render app, with an optional playback plugin later.

The work is split into **bursts**. Each one fits in one short Claude session, has a test that says it's done, and leaves the repo working. To start a burst, open a new chat and say: *"Read ROADMAP.md and do burst 1.2."* Then tick it below.

**Sizes:** **S** = one short session · **M** = one longer session · **L** = split it before starting.
**🖥 = needs Adin's machine.** Adin runs it and pastes the output, or a Claude session runs it there by Remote Control.

## Status

Next up: **0.1** and **0.2** (both Adin, no Claude tokens), then **0.3**.

---

## Decisions to make first

| # | Question | Recommendation | Why |
|---|---|---|---|
| D1 | **GPU:** local or cloud for neural work? | Decide after burst 0.1. If `nvidia-smi` shows an NVIDIA card with **6 GB+ VRAM**, run everything locally. If not, run Phases 1–2 on CPU (they don't need a GPU) and rent a cloud GPU only for Phase 3 training. | The Aero 15XA usually shipped with an RTX 2070/2080 Super Max-Q (8 GB), which would be enough. That is a guess until 0.1. |
| D2 | **Priority:** (a) hard, reliable pitch correction or (b) changing the voice? | **(a) first.** | It is the stated main aim (fewer lead retakes). Phases 1–2 deliver it without any GPU. The neural path in Phase 3 also starts as *pitch correction in your own voice* (burst 3.3) before it does other voices. |
| D3 | **Personal tool or release?** | **Personal for now**, but only pick dependencies that would allow a release later. | adin-brew is GPL-3.0. RMVPE (Apache-2.0), CREPE (MIT), RVC (MIT), DDSP-SVC (MIT) and Seed-VC (GPL-3.0) all fit that. so-vits-svc is AGPL-3.0, so skip it. Pretrained weights have their own terms, so re-check in burst 4.3 before any release. |
| D4 | **App, plugin or both?** | **Standalone app first** (Python backend, web UI on localhost). A thin playback plugin is optional, in burst 4.2. | Mixbus can't let a plugin read ahead, and neural rendering can't run in a real-time callback. A plugin can still play back a pre-rendered file, like LevelBrew's scan. |
| D5 | **Where does the app code live?** | **A new repo** (for example `adinwalls/vox-synth-tune`) that Adin creates on GitHub. Keep this roadmap in adin-brew until then, or move it across. | The app is Python with large model downloads. adin-brew is small C++ plugins with a glibc 2.35 build rule. Keeping them apart keeps both repos simple. |
| D6 | **Voices and consent** | Only your own voice, or voices with written permission. No real-artist cloning. | It's in the brief, and it keeps a later release possible. |

---

## What already exists (reuse, don't rebuild)

- **Phrase Pull is already in TuneBrew 1.5.** Brief item 6 is done, so nothing needs scheduling. The app's Phase 1 is the same idea offline, with a neural pitch tracker and bigger moves.
- `src/TuneEngine.hpp`: header-only YIN detection, key/scale quantiser with hysteresis, vibrato separation, TD-PSOLA shifting. No dependencies.
- `src/PhrasePull.hpp`: `PullRegion` (start, end, pull 0–1, note shift in scale steps). Port this data model as it is.
- `src/KeyDetect.hpp`: key and tuning-offset detection (47/48 on tests).
- `test/harness.cpp`: renders raw float32 audio through TuneEngine offline. So the TuneBrew baseline (burst 0.3) runs in the cloud and **doesn't need Adin's machine**, only the WAV.
- `test/measure.py`, `test/quality.py`: cents-error and quality measurements to reuse for every comparison.

## Models checked (13 Nov 2026)

| Model | Use | Licence | Linux | Notes |
|---|---|---|---|---|
| RMVPE | Pitch tracking | Apache-2.0 (code) | Yes. `rmvpe-onnx` (MIT, v0.2.3) runs on ONNX Runtime CPU | Robust on vocals. The default pitch tracker. |
| CREPE / torchcrepe | Pitch tracking | MIT | Yes (pip) | Fallback and cross-check. |
| RVC (WebUI) | Voice model trained on one voice, rendered at a given pitch | MIT | Yes, official Linux install | Accepts an **F0 curve file**, so an edited pitch curve can be rendered in your own trained voice. That is the neural pitch-correction route. Needs about 10–30 min of dry vocals to train. |
| Seed-VC | Zero-shot conversion from a short reference clip | GPL-3.0 (pip `seed-vc` 0.4.3) | Yes, Python/PyTorch | No training needed. The quickest way to hear neural results. GPU strongly preferred. |
| DDSP-SVC | Lighter trained conversion | MIT | Yes | Faster to train and lighter on the GPU, with lower quality. Keep as the fallback if VRAM is tight. |
| so-vits-svc | Conversion | AGPL-3.0 | Yes | Skip it: licence, and RVC covers the same ground. |

The licences come from each project's LICENSE file and PyPI. Install steps and VRAM needs get confirmed in the bursts that use them.

---

## Phase 0: setup and baseline

- [ ] **0.1 System check** 🖥 · S · *no Claude tokens*
  - Goal: know the GPU, RAM, Python and disk space.
  - Adin runs: `nvidia-smi; python3 --version; free -h; df -h ~` and pastes the output in the thread.
  - Done when: D1 is settled and written into this file.
- [ ] **0.2 Test audio** · S · *no Claude tokens*
  - Goal: one take to measure everything against.
  - From Adin: upload `over_again_raw_vox.wav` (dry, unprocessed), plus the song's key if you know it.
  - Done when: the file is in the project files.
- [ ] **0.3 TuneBrew baseline** · S
  - Goal: a number to beat.
  - Build `test/harness`, render the take at the "firmly in key" settings (Natural, Retune 0, Amount 100, Flex 0), and measure each note's cents error before and after, using RMVPE as the referee.
  - Done when: `baseline/over_again.md` lists every note's error and the worst five, and includes the rendered WAV.
- [ ] **0.4 App repo scaffold** · S
  - Needs from Adin: D5 answered. If it's a new repo, Adin creates it empty on GitHub.
  - Goal: Python project (pyproject, pytest, `vox` CLI with `--help`, README).
  - Done when: `pytest` passes in CI and `vox --help` runs.

## Phase 1: Phrase Pull offline (pitch editor MVP, DSP render)

- [ ] **1.1 Neural pitch track** · S
  - `vox pitch in.wav` uses RMVPE (ONNX, CPU) and writes f0 CSV at 10 ms steps. torchcrepe is the fallback.
  - Done when: a synthetic sine sweep is tracked within ±5 cents, and the take's curve is plotted against TuneBrew's YIN to show where they disagree.
- [ ] **1.2 Target curve** · S
  - Port the key/scale quantiser and `PullRegion` logic to Python: original f0 + key + phrases → target f0. Keep vibrato on the note centre, as TuneBrew does.
  - Done when: unit tests show 100% pull lands on scale notes, 0% leaves the curve untouched, and a note shift moves one scale step.
- [ ] **1.3 DSP render** · M
  - Render the target curve. Try WORLD (`pyworld`) re-synthesis first, because it handles notes sung nearer the wrong pitch. Keep a TD-PSOLA path as the "transparent" option.
  - Done when: the rendered take's measured f0 is within 10 cents of target on voiced frames, and there are no clicks at phrase edges (test with a synthetic vocal).
- [ ] **1.4 Beat the baseline** · S
  - Run 1.1–1.3 on `over_again` with every phrase at 100% pull.
  - Needs from Adin: a listen, and which version sounds better.
  - Done when: the worst-five note error is lower than in 0.3, and Adin approves the sound (or says what's wrong).
- [ ] **1.5a Web UI: view** · M
  - `vox ui` serves a localhost page: load a WAV, see the pitch curve over a key grid, scroll and zoom, play audio.
  - Done when: the `over_again` curve shows with its notes on the grid, checked by a headless browser screenshot.
- [ ] **1.5b Web UI: pull and export** · M
  - Drag to mark a phrase, pull slider, note ▲▼, Render, A/B play, Export WAV.
  - Done when: an exported WAV matches the CLI render bit for bit for the same phrases, and Adin can fix one phrase end to end.

## Phase 2: notes and dragging

- [ ] **2.1 Note segmentation** · M
  - Split f0 into notes (pitch steadiness plus onsets).
  - Done when: a synthetic melody gives the right note count and boundaries within 20 ms, and `over_again` notes look right to Adin.
- [ ] **2.2 Piano roll and drag** · M
  - Notes as blobs in the UI; drag one up or down to re-pitch it, then render.
  - Done when: a dragged note renders at the new pitch with its vibrato kept.
- [ ] **2.3 Formants and timing** · M
  - Formant control for big shifts, and stretching a note's length.
  - Done when: a ±5 semitone move keeps formants within a set spectral-distance limit (reuse the `lsd.py` measure).

## Phase 3: neural render

- [ ] **3.0 GPU environment** 🖥 · S
  - Set up the PyTorch/CUDA environment on Adin's machine (Remote Control session), or a cloud GPU, depending on D1.
  - Done when: `python -c "import torch; print(torch.cuda.is_available())"` prints `True` where the models will run.
- [ ] **3.1 Seed-VC smoke test** 🖥 · S
  - Convert `over_again` using a clean phrase of Adin's own as the reference. Record the time and peak VRAM.
  - Done when: a WAV is rendered and the time and VRAM are noted here.
- [ ] **3.2 Training data** · S · *mostly Adin*
  - From Adin: 10–30 minutes of dry solo vocals of your own (WAV). Claude slices and cleans them with a script.
  - Done when: a dataset folder passes the checks (no clipping, mono, consistent sample rate).
- [ ] **3.3 Your own RVC voice, rendered at the edited pitch** 🖥 · M
  - Train a small RVC model on 3.2, then render using the **target f0 curve from Phase 1** as RVC's F0 file. This is the "neural Phrase Pull": a note sung far off is re-synthesised at the right pitch in your voice.
  - Done when: on notes TuneBrew couldn't fix in 0.3, the error is below 10 cents and Adin says it sounds like them.
- [ ] **3.4 Shoot-out** · S
  - Render the same edits with DSP (1.3), RVC (3.3) and Seed-VC (3.1). Adin picks the default "HQ" engine.
  - Done when: the choice is recorded here.
- [ ] **3.5 HQ render in the app** · M
  - An "HQ render" button that runs the chosen engine on the marked phrases only and crossfades them in.
  - Done when: an HQ export from the UI matches the CLI render.
- [ ] **3.6 Voice to voice** · M
  - Re-render a take in another voice (consented only, see D6) via a Seed-VC reference or a second RVC model.
  - Done when: a converted take exports from the UI.

## Phase 4: packaging

- [ ] **4.1 Installer for Pop!_OS** · M
  - One script: venv, model download with checksums, desktop entry.
  - Done when: a clean Pop!_OS 22.04 container installs and runs `vox ui`.
- [ ] **4.2 Playback plugin (optional)** · L, split before starting
  - A DPF plugin that plays the app's rendered WAV aligned to the DAW timeline, like LevelBrew's scan. Same glibc 2.35 rule and `glibc_compat.c` shim, and no `sscanf`.
- [ ] **4.3 Release decision** · S
  - Licence audit of code and weights, consent notes, then decide on release.

---

## Token-saving rules for every burst

- Start from this file. Don't re-read the whole repo; read only the files the burst names.
- One burst per session. Tick it, update **Status**, commit and stop.
- Check in before anything that runs longer than a few minutes (training, big downloads).
- Ask Adin for audio rather than generating long synthetic test sets.
