# УБЕРВЫСЕКАТЕЛЬ (Ubervysekatel)

A timing meter plugin: it listens to one instrument and shows, cumulatively, how far the hits land from the
DAW's grid — median offset, spread, hit count and a 0–100 score. Audio passes through unchanged.

- Formats: VST3 (Windows, macOS universal), AU (macOS).
- Vendor shown in hosts: **Insinistra**. Plugin name: **УБЕРВЫСЕКАТЕЛЬ**.
  Files: `Ubervysekatel.vst3`, `Ubervysekatel.component`.
- Rules and numbers match the Rehearsal tab of the Insinistra REAPER panel
  (`Reaper projects\Scripts\rehearsal\config.lua`, `tools\rehearsal.lua`), so both tools give the same figures.
  The onset detector is a port of `insinistra_onset.jsfx`.

## How it works
1. The plugin reads tempo, time signature, play position and bar start from the host on every block.
   No click track is needed, but the musician must play to the click of the same DAW project.
2. An envelope onset detector (one-pole filters per role, fast/slow envelopes, threshold, refractory time)
   finds every hit.
3. Each hit is compared with the nearest grid point, counted from the start of the current bar, so changing
   time signatures do not shift the grid.
4. A hit counts if it is within **60 ms** of the grid point **and** within **40 %** of a grid step; otherwise it
   goes to the *outside the grid window* counter.
5. Offsets are in milliseconds: **+ = behind (late), − = ahead (early)**.

### Display
| Area | Meaning |
|---|---|
| **TOTAL** | everything since the last reset |
| **LAST 8 BARS** | hits from the last 8 bars played |
| score | 0–100; green ≥ 80, yellow ≥ 60, red below. No score with fewer than 8 hits |
| `+x.x ms` | median offset |
| spread | standard deviation after trimming 10 % of the hits at each end |
| histogram | −60…+60 ms in 2 ms bars; green band = pocket; thick line = TOTAL median, thin white line = LAST 8 BARS median |

### Score
```
score = 100 − 2 · (w_ahead · ahead + w_behind · behind + 0.5 · spread)
```
`ahead` / `behind` are how far the median lies outside the pocket (ms). The wrong side weighs 2, the other 1.

| Role | Pocket (no penalty) | Wrong side |
|---|---|---|
| Drums (kit), Kick, Overheads | −10 … 0 ms | behind |
| Bass, Guitar | 0 … +15 ms | ahead |

Rationale: drums sit a touch ahead of the click so that bass and guitar playing right on the click never end
up ahead of the drums; bass and guitar a touch behind is fine.

## Parameters
| Parameter | Range | What it does |
|---|---|---|
| Role | Drums (kit), Kick, Overheads, Bass, Guitar | Detector filters and the default pocket. *Drums (kit)* is for a whole drum bus |
| Grid | 1/16, 1/16T, 1/8T, 1/32 | Grid to compare against; can be automated per song section |
| Input offset | 0–200 ms | For live input only: enter the interface round trip (Input + Output latency from the DAW's audio settings; Cubase: *Studio → Studio Setup → Audio System*). Keep 0 on recorded tracks, where the DAW has already compensated |
| Sensitivity | −12…+12 dB | + catches more hits, − fewer |
| Reset on play | Off / On | Clear the statistics on every transport start |
| Custom pocket, Pocket from / to | −50…+50 ms | Use your own pocket instead of the role default |
| **Reset** button | — | Clear the statistics now |

Detector settings per role (from `insinistra_onset.jsfx`):

| Role | Filter | Ratio | Threshold | Refractory |
|---|---|---|---|---|
| Drums (kit) | none | 1.8 | −50 dB | 35 ms |
| Kick | low-pass 3 kHz | 2.0 | −50 dB | 50 ms |
| Overheads | high-pass 200 Hz | 1.8 | −45 dB | 40 ms |
| Bass | high-pass 30 Hz | 1.6 | −45 dB | 50 ms |
| Guitar | high-pass 80 Hz | 1.6 | −45 dB | 50 ms |

## Usage notes
- Put the plugin **first** in the chain: plugins with latency before it shift the hits against the grid.
- Recorded tracks: Input offset = 0. Live monitoring: Input offset = round-trip latency.
- Statistics are not saved with the project; parameters are.
- *Drums (kit)* is new (the Rehearsal tab merges kick and overheads instead); its detector settings still
  need checking on real recordings.

## Installation
**Windows (VST3)** — copy the `Ubervysekatel.vst3` folder to either
- `C:\Program Files\Common Files\VST3\` (needs admin rights), or
- `%LOCALAPPDATA%\Programs\Common\VST3\` (per-user, no admin; scanned by REAPER and other VST 3.7+ hosts).

**macOS**
- VST3: `Ubervysekatel.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
- AU (Logic, GarageBand): `Ubervysekatel.component` → `~/Library/Audio/Plug-Ins/Components/`
- The bundles are signed ad hoc (no paid Apple Developer ID). After downloading, clear the quarantine flag once:
  ```
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Ubervysekatel.component
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Ubervysekatel.vst3
  ```
  Then restart the host.

**REAPER** may not scan for new plugins at startup: *Options → Preferences → Plug-ins → VST → Re-scan*.
The plugin appears as `VST3: УБЕРВЫСЕКАТЕЛЬ (Insinistra)`.

## Building
Builds run on GitHub Actions (`.github/workflows/build.yml`) on every push, so no local compiler is needed:

| Job | Output | Checks |
|---|---|---|
| `windows-latest` | artifact `Ubervysekatel-windows-VST3` | pluginval, strictness 5 |
| `macos-latest` (arm64 + x86_64, macOS 11+) | artifact `Ubervysekatel-macos` (VST3 and AU zips, ad hoc signed) | pluginval, strictness 5; `auval -v aufx Ubvk Insi` |

Validation logs are uploaded as `validation-<os>`.

Local build (needs CMake ≥ 3.22 and a C++20 compiler: Visual Studio on Windows, Xcode tools on macOS):
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Output: `build/Ubervysekatel_artefacts/Release/{VST3,AU}/`.

JUCE 9.0.5 is fetched by CMake (`FetchContent`).

### The Cyrillic name
JUCE passes the plugin name to VST3 hosts as 8-bit text and widens it byte by byte for the Unicode class info,
so a Cyrillic name would show up garbled, and JUCE-based hosts (pluginval and others) would fail to load the
plugin, because they find the class by comparing the 8-bit name with the scanned one.
`cmake/patch_juce.cmake` patches the fetched JUCE at configure time so that the 8-bit class name, the Unicode
class name and `moduleinfo.json` all carry the UTF-8 name from `Source/UbvName.h` (stored as escaped bytes, so
no compiler can mangle the encoding). On Windows `PLUGIN_NAME` itself stays ASCII (`UBERVYSEKATEL`); on macOS it
is Cyrillic, which also names the AU. The patch fails the configure step loudly if a JUCE update moves its
anchors.

## Source layout
| File | What |
|---|---|
| `Source/Timing.h` | grid steps, pockets, score, median/spread (from a list or a histogram), shared state between audio thread and editor, onset detector |
| `Source/PluginProcessor.*` | parameters, host transport and grid, matching hits to the grid; lock-free, no allocation on the audio thread |
| `Source/PluginEditor.*` | UI: controls, TOTAL / LAST 8 BARS blocks, histogram (refresh 15 Hz) |
| `Source/UbvName.h` | display name in UTF-8 |
| `cmake/patch_juce.cmake` | VST3 name patch (see above) |

Plugin codes: manufacturer `Insi`, plugin `Ubvk`, bundle ID `com.insinistra.ubervysekatel`.
Changing them makes hosts treat the plugin as a different one.
