# Third-Party Notices

This file records third-party material identified during the repository review. It is not a substitute for reviewing the upstream projects and all bundled assets before each release.

## Kyant0 / AndroidLiquidGlass

- **Project:** [Kyant0/AndroidLiquidGlass](https://github.com/Kyant0/AndroidLiquidGlass)
- **Author notice in Halo:** Copyright 2025 Kyant
- **Upstream licence:** Apache License 2.0
- **Halo file:** `src/render/shaders/liquidGlass.hlsl`
- **Scope:** The shader header explicitly identifies Kyant0's work as the mathematical reference and labels the geometry/SDF portions as Kyant0-derived/adapted. The existing copyright and attribution comments in that shader must be retained.
- **Licence text:** See [licenses/Apache-2.0.txt](licenses/Apache-2.0.txt).

The MIT licence in the repository root applies to Halo's original work only. It does not replace or relicense the Apache-licensed third-party material identified above.

## DWMBlurGlass

- **Project:** [Maplespe/DWMBlurGlass](https://github.com/Maplespe/DWMBlurGlass)
- **Licence:** LGPL-3.0 (upstream project)
- **Halo file referencing the investigation:** `src/render/shaders/glassmorphism.hlsl`
- **Scope found in this review:** Halo's shader header describes the implementation as derived from an investigation of DWMBlurGlass CustomBlur. No copied DWMBlurGlass source file or explicit copied-code attribution was identified in the inspected Halo shader. An algorithmic reference or independently implemented technique does not, by itself, establish that DWMBlurGlass code is included in Halo.
- **Release check:** Re-check provenance if any DWMBlurGlass source, code snippets, or substantial code-derived expression is introduced. If that occurs, assess the applicable LGPL obligations before distribution.

## Windows SDK and system libraries

Halo links Windows platform libraries supplied by the Windows SDK (including Direct3D, DirectComposition, WASAPI/Windows Runtime and Win32 system libraries). These are platform dependencies, not third-party source files vendored in this repository. Follow the applicable Microsoft SDK/runtime redistribution terms when distributing binaries.

## Audit scope and limitations

This inventory is based on the repository's current tracked source tree and CMake configuration reviewed for this change. No separate third-party package manager manifest or vendored dependency directory was identified in the reviewed tree. This is not a legal opinion or a complete provenance audit of every historical commit, externally supplied image, font, logo, or release binary.

The final Halo logo and any other externally sourced artwork must be separately cleared for use. Do not assume the root MIT licence covers them.
