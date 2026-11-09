# Halo Desktop

A native Windows media card with a polished, glass-inspired interface.

Halo Desktop is a lightweight Windows application built around Windows media-session integration and a custom-rendered interface, featuring album artwork, playback controls, audio-reactive effects, and multiple visual materials.

> **Status:** Active development. Features and visuals continue to evolve.

## Features

- **Media session integration** — Connects with Windows media sessions through GSMTC.
- **Playback controls** — Control media playback directly from the card.
- **Album artwork** — Display artwork associated with the current media session.
- **Multiple visual materials** — Solid, Liquid Glass, and frosted Glassmorphism.
- **Lighting Studio** — Configure visual appearance and lighting effects.
- **Audio-reactive visuals** — Effects that respond to audio.
- **Always-On Display** — Dedicated display modes.
- **Native rendering** — Built with Windows graphics APIs rather than a web-based UI.

## Technology

Halo Desktop is written in **C++20** and uses:

- Win32 API
- Direct3D 11
- DirectComposition
- HLSL shaders
- WASAPI
- GSMTC
- CMake

## Building

Halo Desktop is a Windows-native project.

### Requirements

- Windows 10 or Windows 11
- Visual Studio with the MSVC C++ toolchain
- Windows SDK
- CMake

### Build from source

Clone the repository:

```powershell
git clone https://github.com/Nox-Wizard-py/Halo-Desktop.git
cd Halo-Desktop
```

Configure the project:

```powershell
cmake -S . -B build
```

Build the application:

```powershell
cmake --build build --config Release
```

The generated build output will be located in the `build` directory.

## Project Status

Halo Desktop is actively being developed. Features, rendering quality, and Windows integration may change as development continues.

## License

Halo Desktop's original code is licensed under the [MIT License](LICENSE).

Some components or referenced material retain their own licences. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the accompanying licence texts for details. The root MIT licence does not relicense third-party material or externally sourced artwork.
