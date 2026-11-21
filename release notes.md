## v1.0.0 — Halo Desktop

**Halo Desktop** is a lightweight, native Windows media experience that surrounds your desktop with dynamic artwork-derived lighting, a floating media controller, and a dedicated Always-On Display—built from the ground up in C++20 and Direct3D 11 with zero browser or Electron overhead.

---

### Getting Started & Where to Find Halo After Installation

#### 1. Installation & Launch

- **Installer Package (`HaloDesktop-Setup.exe`):** Run the installer to deploy Halo Desktop to your system. You can launch it immediately from the **Windows Start Menu** (`Halo Desktop`) or via the optional Desktop shortcut.
- **Portable Build (`.zip`):** Extract the archive to any folder and run `LumnOverlay.exe` directly—no installation required.
- **Silent Background Mode:** Launching with `--background` starts Halo quietly in the notification area without immediately popping up the Media Card or Plasma Rim.

#### 2. Where It Lives (System Tray & Window Behaviour)

Halo is designed as a background desktop companion rather than a traditional taskbar app:

- **System Tray (Notification Area):** Look for the **Halo icon** in the bottom-right Windows system tray (click the `^` overflow chevron if Windows hides newly installed tray icons). Right-clicking or interacting with the tray icon lets you toggle the **Media Card**, **Plasma Lighting**, **Always-On Display**, open **Lighting Studio**, or cleanly **Quit** the application.
- **Closing vs. Quitting:** Closing or dismissing the Media Card, Lighting Studio, or Always-On Display only hides that visual interface—Halo continues monitoring media sessions and audio in the background. To exit completely, select **Quit** from the system tray menu.
- **Single-Instance Handoff:** Launching Halo while it is already running in the background automatically signals and reveals the active instance instead of spawning duplicate processes.

---

### How to Use Halo Desktop

#### Media Card

1. Start playing music or video in any Windows media-enabled player (Spotify, YouTube Music, Apple Music, browser tabs, local players).
2. The floating **Media Card** automatically pulls live track metadata, album artwork, and playback state via Windows GSMTC, letting you play, pause, and skip tracks directly from your desktop.
3. Its rounded album artwork and backdrop integrate cleanly with live desktop backgrounds—including **Wallpaper Engine**—without blocking normal window focus or screen captures.

#### Visual Materials & Lighting Studio

Open **Lighting Studio** from the system tray to customize Halo's look in real time:

- **Solid:** Clean opaque card styling with artwork-derived palettes or custom-configured gradients.
- **Liquid Glass:** Glossy, translucent surface rendering with optical distortion and specular glass highlights.
- **Glassmorphism:** Frosted-glass backdrop Gaussian blur paired with subtle colour tinting and soft borders.

#### Screen-Edge Plasma Lighting

- Automatically samples the active album artwork and wraps your screen perimeter in an organic, audio-reactive plasma glow synchronized to your music via WASAPI loopback capture.

#### Always-On Display (AOD)

- Launch **Always-On Display** from the system tray for a distraction-free fullscreen presentation in either **Black Mode** (minimal pure-black canvas) or **Current Wallpaper Mode** (compatible with animated desktop wallpapers).
- Automatically prevents unwanted display sleep while active and can be dismissed at any time without stopping background media tracking.

---

### What's New in This Release

- **Perceptual OKLab Colour Extraction Engine:** Replaced legacy vibrancy-biased and naive volume-splitting extractors with a 12-sector circular hue-binning and OKLab perceptual visibility pipeline—preventing tiny studio logos from hijacking the palette while preserving distinct secondary and accent colours.
- **Multi-Colour Plasma Rim Shader (`plasmaRim.hlsl`):** Re-balanced shader knots (`0.08`, `0.35`, `0.65`, `0.92`), activated `Balance` weighting, and reduced crest white bleaching (`0.40f`) so all four extracted colours remain rich and visible along the screen edge.
- **Media Card Compositing & Rounded Clipping:** Fixed sharp dark corner artifacts on rounded album artwork and resolved backdrop sampling so live Wallpaper Engine layers render properly behind frosted/glass materials.
- **Desktop Window Discipline & Capture Support:** Fixed unintended topmost Z-order hijacking when opening other applications and ensured the Media Card appears cleanly in Windows screenshots (`Win + Shift + S`).

---

### Nerd Stats & Under-the-Hood Architecture

For developers, graphics tinkerers, and curious minds inspecting how Halo runs on the metal:

| Subsystem                     | Implementation & Metrics                                                                                                                                   |
| :---------------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Core Stack**                | Pure **C++20**, **Win32 API**, **Direct3D 11**, **DirectComposition**, **HLSL**, **WASAPI**, **GSMTC**                                                     |
| **Runtime Footprint**         | Zero Chromium / WebView2 / Electron dependencies; native GPU-composited swapchains                                                                         |
| **Colour Space & Binning**    | **OKLab** (`L, a, b`) perceptual space; `12 × 30°` chromatic hue sectors + achromatic neutral bin (`C < 0.035`)                                            |
| **Sample Grid & Throughput**  | `48 × 48` WIC Fant-downsampled buffer (`2,304` pixels) processed per track change in microseconds with packed stack/pre-reserved buffers                   |
| **Hue Merging & Noise Gate**  | Adjacent hue clusters merged at `ΔE < 0.08`; micro-noise/logos rejected below `1.2%` area ratio (`< 28 px`)                                                |
| **Visibility & Distinctness** | Scored via $\text{areaRatio}^{0.35} \cdot (0.05 + 3.0 \cdot \text{meanChroma})$ with greedy `ΔE >= 0.125` filtering and 5-step gamut-safe chroma bisection |
| **Plasma Shader Knots**       | 4-stop perimeter distribution (`HotColor`, `Color2`, `Color3`, `DeepColor`) mapped at `s = 0.08, 0.35, 0.65, 0.92` with `0.40f` peak crest highlight       |
| **Supported Architectures**   | **Windows x64** and **Windows ARM64** (Windows 10 / Windows 11)                                                                                            |
| **Automated Verification**    | Validated by `LumnVerification.exe` (17-test automated suite covering silent IPC startup, lifecycle, and perceptual colour extraction)                     |

---
