## VFX Art-Style Guide (Render Style axis — orthogonal to mood tone)

> This is the **RENDER STYLE** axis. It is orthogonal to the **MOOD TONE** axis (STYLE_GUIDE.md = Horror vs Action) and to the **per-effect recipes** (RECIPES.md). Compose all three: pick a render style here (how it's drawn), apply a mood tone (Horror restraint vs Action punch — emissive/saturation/timing numbers), then build with a recipe (the Niagara/Material skeleton). When project style is unstated, infer from existing assets/references; default render style = **Realistic/Semi-Realistic** unless the art direction is clearly stylized.
>
> How they combine: render style sets blend mode, shape/edge language, lighting model, and texture source. Mood tone overrides the *intensity numbers* (emissive, saturation, bloom, timing). Recipe gives the node/emitter structure. Example: `Anime/Gacha render style + Action mood + Recipe #6 Sword Trail` = crisp geometric ribbon, HDR emissive 8–12, snappy 0.1–0.3s fade.

---

### 1. REALISTIC / SEMI-REALISTIC (AAA shooter, UE5 photoreal)

**Shading / material**
- Blend **Translucent** (NOT Additive by default); Translucency Lighting Mode = **Volumetric Directional** so smoke/fire respond to scene lights.
- **Soft Particles ON** (DepthFade / CameraDepthFade) to kill hard intersection seams; enable **Spherical/translucency normals** for directional shading.
- Domain Surface, lit. Emissive grounded in combustion space: white-yellow core 8–16, orange mid 3–6, dark edges. Volumetrics use Extinction (density) + Albedo, both varying with depth.
- Additive reserved ONLY for far-distance glow/light-ghosting, never bright foreground flares. Enable translucency shadow casting.

**Shape & motion**
- **Curl noise** (3D vector field), not Brownian — coherent vortices, directional wind bias. Smoke biased down/out, hot fire up + side-scatter. No floaty weightless motion; inherit velocity from collisions/forces.
- Diffuse soft-falloff edges on smoke; sharp hot core → translucent halo on fire. NO posterization/steps.
- Mesh particles (low-poly cloud chunks) under sprite sheets for closeup depth; secondary sparks/embers/soot with dampened turbulence + faster settle gravity.

**Texture**
- Procedural, not hand-painted: Perlin/Simplex warped by curl noise, baked to **Sparse Volume Textures** or SubUV flipbooks (8×8/16×8, 32–64 frames). Hierarchical noise = low-freq rolling shapes + high-freq detail.
- Fire = stretched Voronoi warped by moving Perlin, 2–4 frame R/G/B channel offset for color shift. Erosion via high-freq Perlin sharp threshold (not uniform fade). Match texel density to sprite size — no blur in fire cores.

**Refs:** Clair Obscur: Expedition 33, Helldivers 2, CoD/Destiny 2/Overwatch 2, UE5 Nanite+Lumen showcases, Houdini→UE5 SVT workflows.

---

### 2. TOON / CEL-SHADED (Guilty Gear Strive, Hi-Fi Rush, Borderlands)

**Shading / material**
- **Unlit** for emissive particles. **Posterize/Step** node quantizes to 2–3 discrete bands; ColorRamp with **Constant (not Linear)** interpolation = hard color edges. Flat emissive only (no diffuse/spec/roughness/normal-map detail).
- **Outline** = inverted-normal back-face hull: duplicate mesh scaled by `OutlineThickness` via WPO, opaque black, `TwoSidedSign × -1`. Optional dither to fight posterize banding.
- Blend Additive/Translucent for FX; Opaque for outline geometry.

**Shape & motion**
- Comic-impact silhouettes: angular punch/slash, geometric star/burst (sharp, not organic). **Smear frames** (1-frame elongation on high-velocity impact). Snappy 3–4 frame impact holds, no ease-out, sudden snap to glow/trail.
- Stepped vertex animation (clamp/step, hold poses). Puffy/blocky smoke (low-poly cube/sphere vertex-morph or radial bursts, minimal interpolation). Trails = short sharp 90° bends, not curves. Outlines always present, thickness constant or scale-tied (not depth-soft).

**Texture**
- Hand-drawn flat sprites, sharp inked edges, **no anti-aliasing**. Line-art: bold black strokes, consistent 1–3px weight. 2–3 flat color blocks, no dithering, no blur. **Point (Nearest) sampling**, no mipmap blur. Normal maps flat neutral (0.5,0.5,1.0) only.

**Refs:** Guilty Gear Strive, Hi-Fi Rush, Borderlands, Jet Set Radio, Wind Waker, Persona 5, Genshin (outline rings).

---

### 3. STYLIZED HAND-PAINTED (painterly, warm-saturated, chunky shapes)

**Shading / material**
- **Additive** primary for bright readable FX over busy backgrounds; Translucent+Soft-Masked for secondary soft smoke. Emissive 1.5–3.0, warm hue-shifted to read.
- Master material with **gradient ramps** (Lerp color remap on grayscale source) for palette swaps without re-painting. **Particle Perpendicular Normals** for bulbous bubble-like volume (not flat billboards). **Alpha-crushing / erosion** for crisp thick edges (WoW signature — sharp alpha transition, not soft fade). Unlit preferred to preserve painted color intent.

**Shape & motion**
- Exaggerated chunky silhouettes over naturalism; fluid not geometric. Soft but **densely shaped** smoke (visible density gradients, not wispy/dispersed). Smear on impact, fast accel ramp → gradual decay (not linear). Curl-noise swirl for readable looping turbulence.
- Layered sparks: chunky mesh primaries over fine sprite secondaries. Timing: snap (rise/peak <0.2s) → long fade (0.6–1.5s). Warm fire/magic = round puffy clusters; cold/ice = sharp crystalline spikes.

**Texture**
- **Hand-painted source** (visible brush strokes in the PNG). Grayscale bases with high-contrast internal gradients (not flat grays) to feed gradient-map lookups. Dual-layer: sharp alpha-crushed primary + soft secondary glow. 512²–1024² (readable strokes at runtime). UV distortion via simple sin-wave panning, not complex displacement. Minimal in-material procedural noise — favor authored handcraft.

**Refs:** WoW (Dragonflight), Fortnite, Dota 2, League of Legends style guide, Arcane (ribbons), Genshin.

---

### 4. ANIME / GACHA (Genshin, Honkai Star Rail, Arknights, PGR)

**Shading / material**
- **Additive** primary, bloom-heavy. Very high emissive (10–100+) to trigger global bloom (post bloom intensity 3.0–5.0+ — materials expect it). **Inverted Fresnel rim** (bright center, fade at edge) — opposite of cinematic. Saturated single-hue element coding (red=fire, blue=ice, cyan=wind, yellow=lightning).
- Zero/minimal metallic+roughness — emissive carries everything. Alpha-additive (not premultiplied) for layered sparkles. **Polar/radial UV** for kaleidoscopic shatter. Fresnel power 1.5–3.0 to sharpen glow ring. Flat-shaded geometry, subtle/no normals.

**Shape & motion**
- **Geometric**: stars, diamonds, teardrops, hexagons, sharp splinters (NOT soft clouds). Ribbon slashes: thin, sharp leading edge, snappy 0.1–0.3s fade. Snap impact (scale 0→1 in 1 frame) then rapid fade (~80% of anim). Crisp sparkle dots, instant appear/disappear.
- 2–3 frame smear streaks. Shimmer rotation (360°/s) on ribbons; rotating geometry on orbs. Lens-flare streaks at impact (8–12 crisp white rays). Concentric expanding sharp rings (polar coords).

**Texture**
- **Crisp gradients** (white→color hard edge, NOT gaussian blur). Soft cloud reserved for background haze only; foreground = hard-edged geometric masks. Dissolve = high-contrast Perlin. Worley/cellular for shattering. Radial/polar gradients for rings. Hand-drawn crisp 8/12-point stars. UV-scrolled flowing noise (2–4 u/s). Texel density 4–8× normal (sharp artifacts OK, no blur).

**Refs:** Genshin Impact, Honkai: Star Rail, Arknights, Punishing: Gray Raven, Tower of Fantasy.

---

### 5. PIXEL / RETRO & GEOMETRIC / ABSTRACT (two related lanes)

**Shading / material**
- **Pixel/Retro:** post-process LUT palette reduction (NES 64 / PICO-8 16), Posterize clamping to discrete bands, **ordered (Bayer) dithering** pinned in camera/geometry space (not screen-space). Unlit emissive. **Masked** mode for hard-pixel silhouettes; AlphaComposite over pure Additive to keep saturation on light backgrounds. Custom-depth stencil for per-object dither.
- **Geometric/Abstract:** Translucent Additive / AlphaComposite / Screen for clean bright overlays, Modulate/Multiply for dark depth. Pure Emissive Unlit glow. NO normal maps — flat color + simple Fresnel rim. Vector-derived radial gradients / distance fields, not organic noise.
- Blend cheat-sheet: Additive=bright energy (risk blowout) · Modulate=safe saturated dark · AlphaComposite=best color preservation · Screen=vector overlays · Translucent-Unlit=base · Masked=hard pixel edges.

**Shape & motion**
- **Pixel/Retro:** blocky silhouettes, hard edges via Masked/dither boundary. Snappy non-eased linear/instant frame changes (frame-by-frame feel). Chunky square bursts, thick 3–5px pixel lines on 45°/cardinal only. Hand-keyframed discrete frames, minimal procedural trails, sharp pop-in.
- **Geometric/Abstract:** perfect circles/rects/triangles/polygons, concentric/nested/radial-grid silhouettes. Smooth easing (snappy start, long ease-out). Thin 1–2px clean radial/perpendicular lines. Trailing arcs, rotating geometry, sine-breathing scale, radial ripple expansion.

**Texture**
- **Pixel/Retro:** Perlin+white noise **quantized/posterized to pixel grid**; procedural Bayer dither (4×4/8×8/16×16); baked LUT palette textures; low-res (32²-effective) sampling; 16²/32² sprite atlases. Hard mask cutoffs or dither edges — NO anti-aliasing.
- **Geometric/Abstract:** vector-derived procedural (radial gradient math, fresnel/ring, distance fields for perfect circles/rects). Color from material parameters (no color texture samples) for flat precision. Emissive gradients + rim masks are the only texture types; minimal grain.

**Refs (Pixel):** Hyper Light Drifter, Katana ZERO, Celeste, Shovel Knight, Nuclear Throne, Enter the Gungeon. **(Geometric):** Superhot, Monument Valley, Thomas Was Alone, Sayonara Wild Hearts, Baba Is You.

---

### Render-style quick reference

| Style | Default Blend | Lit? | Edges | Texture source | Outline |
|---|---|---|---|---|---|
| Realistic | Translucent (Volumetric Dir.) | **Lit** | soft, depth-faded | procedural / SVT flipbook | no |
| Toon/Cel | Additive/Translucent (Opaque outline) | Unlit | hard, posterized | hand-drawn flat, point-sampled | **yes** (inverted hull) |
| Hand-Painted | Additive (+ soft Translucent) | Unlit | crisp alpha-crushed | hand-painted grayscale + ramp | no |
| Anime/Gacha | Additive (bloom-heavy) | Unlit | crisp hard gradient | high-res crisp gradient/geo masks | optional |
| Pixel/Geometric | AlphaComposite/Masked · Add/Screen | Unlit | blocky-dither / perfect-vector | quantized LUT / vector SDF | no |
