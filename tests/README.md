# LUMN-REA: Test Suite & Verification Harnesses

This directory contains automated verification suites, historical phase regression tests, and integration test scripts for the LUMN Windows application.

---

## 1. Directory Structure

### A. Primary Automated Test Suite (`LumnVerification`)
* **[test_verification.cpp](file:///c:/Users/hossa/Dev-Project/LUMN-REA/tests/test_verification.cpp)**:
  The canonical automated test suite integrated into CMake and CTest.
  - **Backdrop Sampling & Window Exclusion**: Verifies `WDA_EXCLUDEFROMCAPTURE` prevents recursive rendering feedback loops.
  - **Shader & Material Mode Switching**: Verifies runtime switching between full Liquid Glass and Phase 4B procedural fallback.
  - **60 FPS Performance Benchmark**: Measures multi-frame swapchain presentation overhead (<0.01 ms average).

### B. Phase Regression Suites (`test_phase*.cpp`)
Verification harnesses created during each milestone of the project:
* `test_phase3_1_verification.cpp`: OKLab color math, dual-tone extraction, single-hue derivation, and generation race safety.
* `test_phase4a_verification.cpp`: Media card window creation, hit testing, and command routing.
* `test_phase4a1_verification.cpp`: GSMTC timeline interpolation and pause freeze logic.
* `test_phase4a2_verification.cpp`: GSMTC artwork-to-track association and decode queue validation.
* `test_phase4a3_verification.cpp`: Monotonic playback timeline interpolation and Chromium GSMTC stabilization.
* `test_phase4b_verification.cpp`: Glass material rendering, DirectComposition visual tree integration, and tint propagation.
* `test_phase4b1_verification.cpp`: Liquid glass refraction shader and DWM desktop capture integration.

### C. Algorithmic & Subsystem Unit Tests
* `test_oklab_full_algo.cpp` / `test_oklab_palette.cpp` / `test_palette_algo.cpp`: Perceptual color palette extraction algorithms.
* `test_artwork_change_flow.cpp` / `test_artwork_extract.cpp`: WIC artwork decoding and downsampling.
* `test_timeline_behavior.cpp` / `test_ytm_timeline_monitor.cpp`: Timeline interpolation state machines.
* `test_capture_exclude.cpp` / `test_capture_under.cpp` / `test_bitblt_perf.cpp`: Windows GDI/DWM screen capture performance.
* `test_d2d.cpp` / `test_d2d_window.cpp` / `test_d3d_d2d_hybrid.cpp`: Direct2D / Direct3D 11 interop and swapchain presentation.

### D. Audio Reactivity Integration Scripts (`*.ps1`)
PowerShell test scripts that generate audio and validate WASAPI loopback reactivity in `LumnOverlay.exe`:
* `test_audio.ps1`: Plays 120 BPM click track (`click120.wav`) and tests transient pulse response.
* `test_dynamic_passages.ps1`: Tests dynamic volume envelope progression (`dynamic_track.wav`).
* `test_pause_transition.ps1`: Tests smooth decay during playback pause.
* `test_silence.ps1`: Tests noise floor and silence detection.

---

## 2. Running Tests

### Running Automated CTest Suite
```cmd
cmake --build build
ctest --test-dir build --output-on-failure
```

### Running the Verification Binary Directly
```cmd
build\LumnVerification.exe
```

### Running Audio Reactivity Tests (PowerShell)
```powershell
.\tests\test_audio.ps1
```

