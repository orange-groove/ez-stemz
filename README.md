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

EZStemz uses [clang-cl](https://clang.llvm.org/docs/UsersManual.html#clang-cl)
on Windows because demucs.cpp's CMake script uses gcc-style compiler flags
(`-Ofast -march=native ...`) that MSVC's `cl.exe` rejects. clang-cl ships
with Visual Studio (Individual Components → "C++ Clang tools for Windows").

You also need OpenBLAS. The path of least resistance is
[vcpkg](https://vcpkg.io):

```powershell
# One-time:
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install openblas:x64-windows-static

# Per-build (from a "x64 Native Tools Command Prompt for VS 2022"):
cd ezstemz
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=clang-cl `
  -DCMAKE_CXX_COMPILER=clang-cl `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  -DEZSTEMZ_DEMUCS_DIR=C:/path/to/demucs.cpp `
  -DEZSTEMZ_MODEL_FILE=C:/path/to/ggml-model-htdemucs-6s-f16.bin
cmake --build build --target ezstemz
```

The MSVC runtime is statically linked, so distributed binaries don't need
the Visual C++ Redistributable.

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

## Distribution (macOS)

EZStemz ships as a Developer ID-signed, notarized `.dmg`. No Mac App Store, no
sandboxing constraints, no installer to host beyond an HTTPS download.

### One-time setup

1. **Apple Developer Program** ($99/yr).
2. From Xcode → Settings → Accounts, click *Manage Certificates* and create a
   **Developer ID Application** certificate. Export it as a `.p12` if you want
   to use it from CI.
3. **App-specific password** for `notarytool`: visit
   https://appleid.apple.com → Sign-In and Security → App-Specific Passwords.
4. Install the helper tool used by the release script:

   ```bash
   brew install create-dmg ninja ccache
   ```

5. Copy the env template and fill it in:

   ```bash
   cp .env.release.example .env.release
   # edit .env.release with your identity, Apple ID, team ID, app password
   ```

   `.env.release` is gitignored. Verify your signing identity is visible:

   ```bash
   security find-identity -v -p codesigning
   ```

### Cutting a release locally

```bash
scripts/release
```

That:

1. Configures + builds a universal (arm64 + x86_64) Release.
2. Codesigns every Mach-O inside the bundle with hardened runtime + your
   entitlements (`Resources/EZStemz.entitlements`).
3. Submits the zipped app to Apple's notary service and waits for the ticket.
4. Staples the ticket so the app opens offline.
5. Builds a styled DMG, then signs + notarizes + staples the DMG itself.

Output lands in `dist/EZStemz-<version>.dmg`. That's the file you ship.

For a quick local smoke-test that skips Apple's servers (the resulting DMG
will *not* open on other machines):

```bash
SKIP_NOTARIZE=1 scripts/release
```

### Cutting a release via GitHub Actions

Push a tag and the workflow at `.github/workflows/release.yml` does the
above on a `macos-14` runner and attaches the DMG to a draft GitHub Release.

```bash
git tag v0.3.0
git push origin v0.3.0
```

Required repository secrets:

| Secret | What it is |
|---|---|
| `DEVELOPER_ID_APPLICATION`  | Full identity string, e.g. `Developer ID Application: Your Name (ABCDE12345)` |
| `APPLE_ID`                  | Apple ID email |
| `APPLE_TEAM_ID`             | 10-char team ID |
| `APPLE_APP_PASSWORD`        | App-specific password from appleid.apple.com |
| `MACOS_CERT_P12_BASE64`     | `base64 -i cert.p12` of the exported Developer ID Application certificate |
| `MACOS_CERT_P12_PASSWORD`   | The password you set when exporting the .p12 |
| `MACOS_KEYCHAIN_PASSWORD`   | Any random string; used only to lock the temp keychain on the runner |
| `EZSTEMZ_MODEL_URL`         | Direct download URL for `ggml-model-htdemucs-6s-f16.bin` (e.g. a private S3 link or GitHub release asset) |

### Versioning

The version comes from `project(ezstemz VERSION x.y.z)` in `CMakeLists.txt`.
Bump it before tagging.

## Distribution (Windows)

EZStemz on Windows ships as an **NSIS installer** (`EZStemz-<version>-Setup.exe`).
Unlike macOS, Windows does not require notarization. Code signing is optional:

- **Unsigned**: works, but Microsoft SmartScreen displays a "Windows protected
  your PC" dialog the first time the user runs the installer. They can click
  *More info → Run anyway* to proceed. Many free Windows tools ship like this.
- **Signed with a regular code-signing cert** (~$200/yr from Sectigo, DigiCert,
  Comodo): publisher name appears in the SmartScreen dialog instead of
  "Unknown publisher". The dialog still appears until the cert builds reputation.
- **Signed with an EV (Extended Validation) cert** (~$300/yr): SmartScreen
  prompt is suppressed immediately. Required if you want zero friction.

### Cutting a Windows release via GitHub Actions

The same `release.yml` workflow that builds the macOS DMG also builds the
Windows installer in parallel on a `windows-latest` runner. The job:

1. Sets up MSVC + clang-cl via the `ilammy/msvc-dev-cmd` action.
2. Installs Ninja and NSIS via Chocolatey.
3. Restores `C:\vcpkg\installed` from cache and installs `openblas:x64-windows-static`.
4. Downloads the demucs model from the `EZSTEMZ_MODEL_URL` secret.
5. Configures with clang-cl + the vcpkg toolchain.
6. Builds Release.
7. Runs `makensis windows/installer.nsi` to produce the installer.
8. Attaches the installer to the same draft GitHub Release as the DMG.

No additional secrets are required beyond `EZSTEMZ_MODEL_URL` (already used by
the macOS job). To add code signing later, set `WINDOWS_CERT_PFX_BASE64` and
`WINDOWS_CERT_PASSWORD` secrets and call `signtool` after the
"Build NSIS installer" step (against both `EZStemz.exe` and the installer).

