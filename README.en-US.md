# BoomBox Stream 🔊

*[Português (Brasil)](README.md) · English*

An **OBS Studio** plugin that makes your whole screen (or any source) **pulse like a
speaker** to the beat: **zoom** + **bloom** + a brightness flash on every hit.

## Option 1: Shader only (for the Shaderfilter plugin) ⭐

Nothing to compile: [`shaders/boombox.shader`](shaders/boombox.shader) runs in the
**[Shaderfilter](https://github.com/exeldro/obs-shaderfilter) plugin (exeldro)**,
version **2.6.0 or newer**. Audio support (`audio_peak`/`audio_magnitude`) arrived in
2.6.0 (Dec 2025); on older versions the shader loads but nothing reacts.

1. Right-click the source (or the **scene**, for the whole screen) → *Filters* → **+** →
   **User-defined shader**.
2. Tick **Load shader text from file** and pick `boombox.shader`
   (or paste its contents into *Shader text* and click *Reload effect*).
3. Under **Audio source**, pick where the sound comes from (Desktop Audio, music, mic…).
4. Turn on **Simular batida** (simulate beat) to tune it without music, then turn it off.

> The shader's settings are labeled in Portuguese. The English names are in parentheses
> below.

### Not reacting? (checklist)

1. **Turn on “Simular batida” (simulate beat).** If the image pulses, the shader works
   and the problem is the audio. If it still doesn't pulse, the shader failed to
   compile: check *Help → Log Files* for `obs-shaderfilter` errors.
2. **Turn on “Mostrar medidor” (show meter).** Four bars appear in the bottom-left
   corner: red = peak, blue = RMS (as they arrive from OBS), yellow = hit and
   green = body (after your settings). The white line is the noise floor.
   - **Red/blue stuck at zero**: the shader isn't receiving audio.
     - Check your Shaderfilter version. It must be **2.6.0+**; without it the
       **Audio source** field doesn't even appear in the filter properties.
     - Under **Audio source**, pick the source that plays the music (e.g.
       *Desktop Audio*). With *None* it uses the filtered source's own audio, and
       a scene does **not** include Desktop Audio.
     - Levels are measured after the mixer volume, so the source must not be muted
       or at zero volume.
   - **Red/blue move but yellow/green don't**: lower **Piso de ruído** (noise floor,
     e.g. -50 dB), raise **Sensibilidade** (sensitivity) or lower **Curva de punch**
     (punch curve).

| Group | What it does |
|---|---|
| Áudio (audio) | Noise floor (dB), sensitivity, punch curve, beat simulation (BPM), meter |
| Zoom (cone do falante / speaker cone) | Max zoom, **cone wobble** (oscillates while there's sound), center X/Y |
| Bloom | Base, amount on hit, threshold, radius, color and brightness flash |
| Aberração cromática (chromatic aberration) | Radial RGB split that opens on hits |
| Tremor de câmera (camera shake) | Smooth noise shake whose strength follows the audio |

Each effect chooses what drives it: **Corpo** (body: `audio_magnitude`, RMS, smooth),
**Batida** (hit: `audio_peak`, peak, fast) or **Mistura** (mix). By default zoom uses the
mix, while bloom, aberration and shake react to hits.

> **Shader mode limits:** Shaderfilter only provides the volume of the whole signal (no
> separate bass or treble), and a shader keeps no state between frames. So there are no
> frequency bands, no attack/release, and no bouncy spring. For those, use the native
> plugin below.

## Option 2: Native plugin (BoomBox filter), each effect on its own frequency band

Analyzes the audio itself. **Each effect listens to its own frequency band**
(4th-order Butterworth band-pass), with its own sensitivity and attack/release. For
example: zoom on the kick drum, bloom on vocals and flash on cymbals.

### Install on Windows

Download from the **[Releases](https://github.com/xFreDxx/boomboxstream/releases)** page:

- **Installer (easiest):** `boomboxstream-X.Y.Z-windows-x64-installer.exe`. Close OBS and
  run it. It finds your OBS folder on its own (from the Windows registry) and checks
  that it's the right one. To remove it: *Settings → Apps → BoomBox Stream*.
- **Manual:** extract `boomboxstream-X.Y.Z-windows-x64.zip` into your OBS folder
  (usually `C:\Program Files\obs-studio`). You'll get
  `obs-plugins\64bit\boomboxstream.dll` and `data\obs-plugins\boomboxstream\…`.

Then: right-click the source or the **scene** → *Filters* → **+** →
**BoomBox (Audio Reactive)**. Built against the OBS 30.2 API; works on OBS 30, 31 and 32.

> **“Make sure you trust…” / “Windows protected your PC”:** the installer isn't digitally
> signed yet, so Edge and SmartScreen warn about it. In Edge, click the arrow next to
> **Delete** → **Keep** → **Keep anyway**. When running it, click **More info → Run
> anyway**. To avoid the warnings, use the `.zip` instead.

> Test builds of every commit are also available under *Actions → build* (artifacts;
> they require a GitHub login and expire after 90 days).

### Publishing a new version (maintainer)

1. Bump `VERSION` in `project(...)` in `CMakeLists.txt` (e.g. `0.3.0`).
2. Commit, then create and push a tag with the same number:
   `git tag v0.3.0 && git push origin v0.3.0`. Alternatively, create the release on
   GitHub (*Releases → Draft a new release*) with a new `v0.3.0` tag.
3. CI builds Linux + Windows, produces the zip and installer, and attaches them to the
   Release. If the tag doesn't match the version in `CMakeLists.txt`, the build fails on
   purpose.

### Usage

1. In **Audio**, pick the **Audio source**: *Desktop Audio* (default), *This source*
   (the filtered source's own audio) or any other source with sound.
2. Turn each effect on or off with its group checkbox and pick its **Frequency band**.
3. Turn on **Show band meter** to see how much each effect is reacting, in the corner of
   the screen: orange = zoom, white = bloom, cyan = flash, magenta = aberration,
   green = shake.

### Band presets

| Band | Hz | Catches |
|---|---|---|
| Kick / sub | 40–120 | kick drum, 808 |
| Bass | 60–250 | bass + kick |
| Low mids | 250–800 | body of vocals and guitars |
| Vocals | 300–3400 | voice |
| Mids | 500–4000 | vocals, snare, guitars |
| Snare / attack | 1500–5000 | snare crack, transients |
| Highs / cymbals | 6000–16000 | cymbals, hi-hats |
| Full range | everything | — |
| **Custom…** | you set the low and high frequency | — |

### Settings

| Group | Parameters |
|---|---|
| Audio (global) | Audio source, noise floor, response curve, meter |
| **Every** effect | Frequency band, low/high (Hz, for custom), sensitivity (dB), attack, release |
| Zoom (speaker cone) | Max zoom, elasticity (bouncy spring), center X/Y. Default: **kick** |
| Bloom | Amount on beat, base, threshold, radius, color. Default: **mids**, +6 dB |
| Brightness flash | Intensity. Default: **highs/cymbals**, +12 dB |
| Chromatic aberration | RGB split (px). Off by default |
| Camera shake | Amount (px), speed. Off by default |

> Tip: in music, mids and highs carry much less energy than bass, which is why bloom
> and flash ship with extra sensitivity. Use the meter to balance them.

### Building

Requires CMake ≥ 3.16 and `libobs` (headers + CMake config), or headers + import
libraries passed in by hand (see the top of `CMakeLists.txt` and the `windows` job in
`.github/workflows/build.yml`).

#### Linux

```sh
sudo apt install libobs-dev cmake build-essential   # Ubuntu/Debian
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

Per-user install (no sudo): copy `build/boomboxstream.so` to
`~/.config/obs-studio/plugins/boomboxstream/bin/64bit/` and the `data/` folder to
`~/.config/obs-studio/plugins/boomboxstream/data/`.

#### Windows / macOS

Point CMake at an OBS Studio build (one that exports `libobsConfig.cmake`):

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/path/to/obs-studio/build/libobs"
cmake --build build --config Release
cmake --install build --prefix "C:/Program Files/obs-studio"
```

On Windows this puts the DLL in `obs-plugins/64bit/` and the data in
`data/obs-plugins/boomboxstream/`.

## How it works

```
audio thread  ─► capture callback: mix to mono → one band-pass per effect
                 (4th-order Butterworth: 2 high-pass + 2 low-pass biquads) → RMS
video thread  ─► tick, per effect: RMS → dB → normalize (floor/sensitivity/curve)
                       → attack/release envelope; zoom also goes through the spring
              ─► render: single-pass shader (data/boombox.effect)
                       zoom + shake on the UV, RGB aberration, 32-tap spiral
                       bloom, flash and meter
```

- `src/boombox-filter.c`: audio analysis, spring physics and properties.
- `data/boombox.effect`: the native plugin's shader (zoom, shake, aberration, bloom, flash, meter).
- `data/locale/`: UI strings in pt-BR and en-US.
- `shaders/boombox.shader`: shader-only version for Shaderfilter.
- `installer/boomboxstream.iss`: Windows installer (Inno Setup).
