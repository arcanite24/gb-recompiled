# Android output

`gbrecomp --android` emits a single-ROM Android project alongside the normal desktop project.

Current Android scope:

- single ROM
- landscape and fullscreen
- `arm64-v8a`
- minimum SDK 24 and target SDK 34
- controller-first, with no touch gameplay overlay
- external SDL2 source checkout rather than a vendored Android SDL build
- persistent keyboard/controller remapping through the runtime settings menu

## Requirements

- a built `gbrecomp`
- Gradle available on `PATH` (the generated project does not include a Gradle wrapper)
- Android SDK and NDK
- `adb`
- an SDL2 source checkout

Build the recompiler first:

```bash
cmake -G Ninja -B build .
ninja -C build
```

## Generate a project

```bash
./build/bin/gbrecomp path/to/game.gb \
  --output output/game \
  --android
```

Optional metadata overrides:

```bash
./build/bin/gbrecomp path/to/game.gbc \
  --output output/game \
  --android \
  --android-package io.gbrecompiled.game \
  --android-app-name "My Game"
```

Without overrides, the package is derived from `io.gbrecompiled.<game>` and the app label comes from the ROM title. The Android project is written under `output/game/android`; the desktop project remains at `output/game`.

Multi-ROM directory input cannot be combined with Android output.

## Provide SDL2

Set `SDL2_SOURCE_DIR` to an SDL2 source tree containing its root `CMakeLists.txt`:

```bash
export SDL2_SOURCE_DIR=/path/to/SDL
```

The generated Gradle and native CMake configuration stop with a focused error when this value is absent or invalid.

## Build the APK

From the repository root:

```bash
SDL2_SOURCE_DIR=/path/to/SDL \
gradle -p output/game/android :app:assembleDebug
```

The debug APK is written to:

```text
output/game/android/app/build/outputs/apk/debug/app-debug.apk
```

## Install and launch

```bash
adb devices -l
adb -s <device_id> install -r \
  output/game/android/app/build/outputs/apk/debug/app-debug.apk
adb -s <device_id> shell am start -W \
  io.gbrecompiled.game/.GameActivity
```

Replace the package in the launch command if generation used a custom value. If only one Android target is connected, `-s <device_id>` can be omitted.

## Controls and writable files

The default controller mapping is based on physical position:

- D-pad or left stick: move
- south face button: Game Boy B
- east face button: Game Boy A
- left/right shoulder: Game Boy B/A
- Start/Menu: Start
- Back/View/Share: Select
- Guide/Home, L3, R3, or Android Back: settings menu

The settings menu can remap gameplay actions and runtime shortcuts. SDL controller detection changes displayed button labels for Xbox, Nintendo, and PlayStation-style layouts where possible.

Saves, RTC data, savestates, screenshots, logs, and runtime preferences use app-private writable storage rather than the packaged asset directory.

## Troubleshooting

### Device is unauthorized

Unlock the device, accept its USB debugging prompt, then rerun:

```bash
adb devices -l
```

### SDL2 source is rejected

Confirm the path points to the root of an SDL2 source checkout:

```bash
test -f "$SDL2_SOURCE_DIR/CMakeLists.txt"
```

Then pass it inline if your shell environment is not reaching Gradle:

```bash
SDL2_SOURCE_DIR=/path/to/SDL \
gradle -p output/game/android :app:assembleDebug
```

### More than one target is connected

Use the identifier printed by `adb devices -l` with every install, launch, and log command:

```bash
adb -s <device_id> install -r output/game/android/app/build/outputs/apk/debug/app-debug.apk
```

### Capture startup logs

```bash
adb -s <device_id> logcat -c
adb -s <device_id> shell am start -W io.gbrecompiled.game/.GameActivity
adb -s <device_id> logcat -d
```

Android output has not yet been promoted into the repository's regular cross-platform CI matrix, so validate the APK on a real device or emulator after generator/runtime changes.
