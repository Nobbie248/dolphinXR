# DolphinXR Metroid Prime Visor Fixes

## Current Status

Last updated 2026-06-04.

Metroid Prime 1 GC thermal visor is working correctly on both Vulkan/OpenXR and D3D11/OpenXR.
The heat effect appears in both eyes, updates each frame, and uses the correct per-eye view for
each eye. Mario Kart Wii EFB copies were also re-tested after the D3D11 change and remained
correct.

Current-good test baseline:

- Backends: Vulkan and D3D11
- OpenXR runtime: VirtualDesktopXR
- Headset: Meta Quest 3
- Game: Metroid Prime 1 GC
- Regression check: Mario Kart Wii EFB copies

The current implementation is intentionally smaller than the earlier D3D11 experiment. Both Vulkan
and D3D11 use the same core idea: preserve the 2-layer thermal EFB copy through palette conversion.
No D3D11 game shader generator changes are required.

## Backend Toggles

Two separate VR pane toggles control the backend-specific palette conversion paths:

- `Metroid Prime Thermal Visor Fix (Vulkan)`
  - Config key: `MetroidThermalVisorFix`
  - Enables layered palette conversion for Vulkan/OpenXR.

- `Metroid Prime Thermal Palette Fix (D3D11)`
  - Config key: `MetroidD3DThermalPaletteFix`
  - Enables the same layered palette conversion for D3D11/OpenXR.

Both toggles are default-on in the VR settings pane. Both are still tightly gated to:

- Metroid Prime 1 GC profile only
- OpenXR only
- 640x448 stereo EFB copy source
- color copy only
- non-XFB copy
- source texture with at least 2 layers

## Why Vulkan Works

During validation, the important diagnostic markers were:

```text
VR_THERMAL_SOURCE: ... reason=METROID_THERMAL_HEAT_GEOMETRY dst=0x0047ad40 native=640x448 layers=2 hash=00000000f523d90d
VR_THERMAL_TEXLOOKUP: stage=7 addr=0x0047ad40 raw=640x448 texfmt=0x8 tlutfmt=0x1 palette_size=32 base_hash=00000000f523d90d full_hash=f8409cde0667b1da
VR_THERMAL_TEXLOOKUP_ENTRY: entry_efb=true entry_layers=2 entry_stride=2560 base_match=true full_match=false
VR_THERMAL_PALETTE: addr=0x0047ad40 native=640x448 layers=2 entry_efb=true entry_stride=2560 source_stride=2560 tlutfmt=0x1 layered_pipeline=true
```

Those now-removed logs showed the thermal source was still stereo when stage 7 was loaded:

- The source copy is a 2-layer 640x448 EFB copy at `0x0047ad40`.
- The stage-7 lookup's `base_hash` matches that EFB copy.
- `full_hash` differs because the game reads the source as a paletted texture (`texfmt=0x8`,
  `tlutfmt=0x1`, `palette_size=32`).
- The critical fix is `layered_pipeline=true`, meaning palette conversion renders both texture
  array layers instead of only converting layer 0.

Before this fix, the texture cache could find the stereo EFB source, but `ApplyPaletteToEntry`
converted it with a normal fullscreen utility triangle. On layered targets, that only reliably
filled layer 0. The resulting bound stage-7 texture still reported `layers=2`, but the right-eye
layer was stale, empty, or effectively a left-eye duplicate.

Vulkan works because palette conversion uses the existing passthrough layer geometry shader for
2-layer sources. It writes layer 0 and layer 1 separately, so the thermal color/composite path
receives a fresh per-eye paletted source.

## Why D3D11 Works

The new D3D11 fix uses the same narrow palette-conversion solution as Vulkan. It does not add a
D3D11 fullscreen-mono texture-layer fallback, does not write the eye to `tex0.z`, and does not
modify generated game pixel or geometry shaders.

The important discovery was that the less destructive fix is enough: when D3D11 keeps the MP1
thermal stage-7 source stereo through palette conversion, the thermal visor effect renders correctly
without the old shader-layer override path.

This keeps the D3D11 fix narrow:

- Only `ApplyPaletteToEntry` chooses a layered palette pipeline.
- Only MP1 thermal-sized stereo EFB copy sources can use it.
- Only D3D11/OpenXR uses it when `Metroid Prime Thermal Palette Fix (D3D11)` is enabled.
- Mario Kart Wii EFB copies remain unaffected in testing.
- No X-Ray-specific D3D11 shader fallback is added by this fix.

## Current Thermal Path

The working frame sequence is:

1. Metroid thermal heat geometry renders in stereo.
2. DolphinXR's Metroid element classifier identifies the relevant thermal/visor/HUD layers.
3. The game creates a 2-layer 640x448 color EFB copy for the thermal source.
4. Stage 7 later reads the same address as a paletted texture.
5. `TextureCacheBase::ApplyPaletteToEntry` detects a 2-layer MP1 thermal source.
6. If the relevant backend toggle is enabled, `ApplyPaletteToEntry` asks `ShaderCache` for the
   layered palette conversion pipeline.
7. `ShaderCache` uses the existing passthrough geometry shader for the layered palette pipeline.
8. The converted paletted source keeps both eye layers fresh.
9. The thermal fullscreen/composite draws sample a valid per-eye source.

The important part is that stage 7 remains stereo and is not replaced by a static mono offset or by
a copied left-eye image.

## What Was Ruled Out

Static right-eye offsets are not the correct fix. They can make one depth plane look closer, but
they cause left/right borders on close/far objects and cannon heat can bleed onto background
geometry.

Patching the wrong thermal pass also failed:

- Offsetting the hot color-mask signature moved the mask, not the source heat information.
- Offsetting the purple backdrop affected the background overlay and caused stretch.
- Rebinding the backdrop stages `0`/`1` to the tracked EFB source produced stale or wrong output.
- Patching the thermal stage-7 draw directly sometimes produced stereo motion, but caused grayscale,
  wrong palette colors, or transparency because it bypassed the real palette/composite path.

The old D3D11 experiment was also ruled out as too broad:

- It added `cvr_layer_override`.
- It wrote the active eye into `tex0.z`.
- It changed generated D3D11 geometry/pixel shader interfaces.
- It touched uber pixel shaders.
- It affected other games even when the toggle was off.

Those changes have been removed. The current D3D11 fix stays in texture-cache palette conversion.

## Important Code Path

Key pieces now involved:

- `TextureCacheBase`
  - Detects MP1 thermal stereo source candidates.
  - Invalidates the texture cache when either backend thermal palette toggle changes.
  - Chooses a layered palette-conversion pipeline when the active backend toggle and source gates
    match.

- `ShaderCache`
  - Builds normal palette conversion pipelines for single-layer textures.
  - Builds layered palette conversion pipelines using the existing passthrough geometry shader when
    stereo/layered EFB copies are available.

- `VertexManagerBase`
  - Runs the Metroid element classifier.
  - Keeps classified HUD/visor/map-style layers headlocked.
  - Does not contain a D3D11 `tex0.z` visor fallback in the current fix.

- `DolphinQt/Settings/VRPane`
  - Exposes `Metroid Prime Thermal Visor Fix (Vulkan)`.
  - Exposes `Metroid Prime Thermal Palette Fix (D3D11)`.

## Validation Notes

The current runtime path should be checked visually:

- Vulkan/OpenXR should keep the thermal effect aligned and updating in both eyes.
- D3D11/OpenXR should keep the thermal effect aligned and updating in both eyes with
  `Metroid Prime Thermal Palette Fix (D3D11)` enabled.
- Mario Kart Wii EFB copies should remain correct with the D3D11 toggle enabled.
- D3D11 should not need `MonoOverlayRightShift`, thermal UV offsets, shader skips, `tex0.z`, or
  `cvr_layer_override`.

If targeted diagnostics are reintroduced later, the useful invariants are:

- The MP1 thermal source is a 2-layer 640x448 color EFB copy.
- The palette conversion uses the layered pipeline for 2-layer sources.
- The D3D11 fix is palette-conversion only.
- No generated D3D11 game shader code should change for this fix.

## Remaining Work

- Keep both Vulkan/OpenXR and D3D11/OpenXR as reference paths for MP1 GC thermal visor validation.
- Keep Mario Kart Wii EFB copies as the regression check before adding any future D3D11 visor work.
- Consider extending the same layered palette-source reasoning to MP2, MP3, and Trilogy if their
  thermal/visor effects use the same kind of paletted stereo EFB source.

## Files Touched

- `Source/Core/Core/Config/GraphicsSettings.h/.cpp`
  - Adds the Vulkan and D3D11 thermal palette config keys.

- `Source/Core/DolphinQt/Settings/VRPane.h/.cpp`
  - Adds backend-specific thermal palette toggles and descriptions.

- `Source/Core/VideoCommon/VideoConfig.h/.cpp`
  - Reads the backend-specific thermal palette toggles into active video config.

- `Source/Core/VideoCommon/TextureCacheBase.h/.cpp`
  - Detects MP1 thermal stereo source candidates.
  - Selects layered palette conversion for Vulkan or D3D11 when the matching toggle is enabled.

- `Source/Core/VideoCommon/ShaderCache.h/.cpp`
  - Provides separate normal and layered palette conversion pipelines.

- `Source/Core/VideoCommon/MetroidElementClassifier.h/.cpp`
  - Classifies Metroid Prime HUD/visor/map-style layers used by the clean profile path.

- `Source/Core/VideoCommon/VertexManagerBase.cpp`
  - Applies the clean Metroid classifier behavior for headlocked classified layers.
