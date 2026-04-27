# EZStemz

A JUCE 8 standalone desktop app that performs **local** music source separation
and multitrack playback.

You drop in an MP3/WAV, the app runs [demucs.cpp](https://github.com/sevagh/demucs.cpp)
(`htdemucs_6s` or `htdemucs`) on your CPU, and the resulting stems are loaded
into a sample-accurate multitrack player with per-track gain / mute / solo and
a master transport.

No accounts, no API, no network. Everything happens on your machine.

## What's included

- **Local separator** – wraps `demucs.cpp` to decode audio with JUCE,
  resample to 44.1 kHz stereo, run inference, and write WAV stems to disk.
- **Multitrack player** – a single `juce::AudioSource` that mixes N
  `AudioFormatReader`s from one shared sample-accurate playhead, with
  click-free per-track gain ramps. Master gain. Scrubbable transport. Auto-stop
  at the longest stem's end.
- **Track strips** – name, mute (`M`), solo (`S`), gain slider, and a
  `juce::AudioThumbnail` waveform with a moving playhead, colour-coded for
  `vocals`, `drums`, `bass`, `guitar`, `piano`, `other`.
- **Bundled model** – the demucs `.bin` is copied into the app bundle at build
  time (macOS: `EZStemz.app/Contents/Resources/`, Linux/Windows: alongside the
  binary). The user never picks a model.

## Architecture

```
SongProcessorComponent
        │
        ├─▶ LocalSeparator ──▶ demucs.cpp.lib (htdemucs_6s)
        │       (decode → resample → inference → write WAVs)
        │
        ├─▶ MultitrackPlayer ──▶ AudioSourcePlayer ──▶ AudioDeviceManager
        │           │
        │       TrackStrips
        │       TransportBar
        │
        └─▶ AppConfig ─▶ <bundle>/Contents/Resources/<model>.bin
```

Source layout (`Source/`):

| File | Purpose |
|---|---|
| `Main.cpp` | `JUCEApplication` entry point + `DocumentWindow`. |
| `MainComponent.{h,cpp}` | Trivial wrapper around `SongProcessorComponent`. |
| `SongProcessorComponent.{h,cpp}` | File picker, progress UI, multitrack view. |
| `LocalSeparator.{h,cpp}` | demucs.cpp integration (pimpl, no Eigen leakage). |
| `MultitrackPlayer.{h,cpp}` | Audio engine (`juce::AudioSource`). |
| `TrackStripComponent.{h,cpp}` | One row per stem with thumbnail + controls. |
| `TransportBar.{h,cpp}` | Bottom transport: play/pause/stop/scrub/master. |
| `AppConfig.{h,cpp}` | Resolves the bundled model path. |

## Prerequisites

You need:

- C++17 toolchain (Xcode/Clang, GCC ≥ 9, or MSVC 2019+).
- CMake ≥ 3.22.
- A clone of **demucs.cpp** (with submodules) accessible on disk.
- A converted **demucs model `.bin`** file (e.g. `ggml-model-htdemucs-6s-f16.bin`).
- A BLAS implementation. On macOS Apple's **Accelerate** framework is detected
  automatically. On Linux install OpenBLAS (`libopenblas-dev`). On Windows
  install OpenBLAS or pass `-DUSE_OPENBLAS=OFF` to fall back to OpenMP.

### Getting demucs.cpp + model

```bash
# Library (vendored Eigen + libnyquist)
git clone --recurse-submodules https://github.com/sevagh/demucs.cpp ~/projects/demucs.cpp

# Convert the official PyTorch demucs weights (one-time, requires Python).
# See ~/projects/demucs.cpp/scripts/convert-pth-to-ggml.py for details.
# Or grab a pre-converted model and drop it somewhere predictable, e.g.:
#   ~/projects/ggml-demucs/ggml-model-htdemucs-6s-f16.bin
```

At configure time CMake copies the model into the app bundle. The default
source path is `~/projects/ggml-demucs/ggml-model-htdemucs-6s-f16.bin`; pass
`-DEZSTEMZ_MODEL_FILE=/path/to/model.bin` (and optionally
`-DEZSTEMZ_MODEL_NAME=<filename>` if you want to bundle a non-default model
file) to override.

At runtime the app looks for the model exclusively at:

- macOS: `EZStemz.app/Contents/Resources/<EZSTEMZ_MODEL_NAME>`
- Linux/Windows: next to the executable, named `<EZSTEMZ_MODEL_NAME>`

## Building

JUCE 8 is fetched automatically via `FetchContent`. demucs.cpp is **not**
fetched – pass its path with `-DEZSTEMZ_DEMUCS_DIR=...` if it isn't sitting at
`../demucs.cpp` next to this repo.

### macOS / Linux

```bash
cd /Users/adamgroves/projects/ezstemz
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ezstemz -j
```

### Windows

```powershell
cd ezstemz
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --target ezstemz --config Release
```

### Pointing at a different demucs.cpp / JUCE checkout

```bash
cmake -S . -B build \
  -DEZSTEMZ_DEMUCS_DIR=/path/to/demucs.cpp \
  -DJUCE_DIR=/path/to/JUCE
```

The build artifact lands at:

- macOS:   `build/ezstemz_artefacts/Release/EZStemz.app`
- Linux:   `build/ezstemz_artefacts/Release/EZStemz`
- Windows: `build/ezstemz_artefacts/Release/EZStemz.exe`

## Using it

1. Launch `EZStemz.app`.
2. Drag an MP3/WAV onto the window (or click **Choose audio file...**).
3. Watch the progress bar — demucs walks the song in 7.8s segments. Expect a
   few minutes per song depending on length and CPU.
4. When finished, the stems appear as live multitrack rows. Adjust each track,
   solo, scrub, mix.

Stems are cached at `~/Library/Application Support/ezstemz/cache/<session>/`
on macOS (and the equivalent locations on Linux/Windows), so you can dig out
the WAVs later if you want to reuse them outside the app.

## Notes

- Separation is fully synchronous in the underlying library. EZStemz runs it
  on a background thread so the UI stays responsive, and the progress callback
  is forwarded from `demucs.cpp` straight into the progress bar.
- Supported input formats: anything JUCE's basic format manager can decode
  (MP3, WAV, FLAC, AIFF, OGG; M4A on platforms with the OS codec).
- Output is always 16-bit PCM stereo WAV at 44.1 kHz – the rate demucs operates
  at internally.
- The 6-stem model produces `drums, bass, other, vocals, guitar, piano`. The
  4-stem model drops `guitar, piano`.
