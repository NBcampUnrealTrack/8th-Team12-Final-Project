## VFX Tone & Style Guide (Horror / Action)

> 레비 generates Material + Niagara-driven effects in UE5.7. Pick the lane (Horror vs Action), copy the numbers. When unstated, default to **Horror** for ambient/world FX and **Action** for combat/hit FX.

---

### Side-by-Side Quick Reference

| Dimension | HORROR (dread, restraint) | ACTION (punch, readability) |
|---|---|---|
| **Saturation** | 0.30–0.50 base, danger spikes 0.90 | 0.85–1.00 (never below 0.85) |
| **Emissive (linear)** | 0.2–0.5 (rare focal light 0.4–0.8) | Launch 4.0–6.0 → peak 8.0–12.0 → end 0.5–1.0 |
| **Bloom Intensity** | 0.20–0.35 (never >0.35) | Control via emissive, post bloom ≤1.5 |
| **Bloom Threshold** | 0.8–1.0 | 0.7–1.0 |
| **Blend Mode** | Translucent only (never Additive) | Additive/Translucent for cores |
| **Particle count** | 200–300 (max 500) | Burst 8–200 per stage |
| **Spawn rate** | 20–40 p/s (fog), 50–100 p/s (ash) | 500–2000 p/s spike (burst only) |
| **Lifetime** | 2–5s (slow = dread) | 600–1400ms total |
| **Movement** | Curl Noise drift 10–20 cm/s | Radial burst 400–1500 UU/s |
| **Sim** | CPU (<1000 particles) | CPU if events; GPU for big bursts |
| **Timing feel** | Slow decay, lingering | Snappy: 80ms impact, ease curves |

---

### HORROR — Concrete Specs

**Palette (hex / RGB)**
- Primary blue: `#2D5A6C` (31, 77, 109) · `#1F4D6D`
- Cold green: `#3A5F4F` (58, 95, 79) · `#2E4539`
- Sickly amber accent: `#8B7355` (139, 115, 85) · `#6B5344`
- Fresh blood: `#5C2E2E` (92, 46, 46) · Old blood: `#3D2F2F` (61, 47, 47)
- AVOID bright warm tones (oranges, bright reds, yellows)

**Emissive / Bloom**
- Fog/dust emissive: 0.3–0.5 (subtle glow, no bloom)
- Blood/viscera emissive: 0.2–0.4 (wet read, not glowing)
- Flicker lights: 0.4–0.8, 2–4 Hz slow sine
- Bloom Intensity 0.2–0.35, Threshold 0.8–1.0
- Emissive >1.0 ONLY for rare focal light; otherwise it flattens dread

**Post-Process**
- Global Saturation: 0.7 (lock 0.7–0.85, never >0.85)
- Contrast: 1.1–1.15 · Vignette: 0.4–0.6
- Shadows warmth −0.05 to −0.1 (shift blue)
- NO chromatic aberration, NO motion blur (break dread)

**Fog & Dust**
- Count 200–300 (max 500), spawn 20–40 p/s, lifetime 2–4s
- Size 20–60cm (fewer, larger = cheaper + subtler)
- Opacity: full at birth → 0.1–0.2 alpha at death (lingering haze)
- Drift 10–20 cm/s via Curl Noise (never directional)
- Color blend `#2D5A6C`→`#3A5F4F`, emissive ×0.05–0.15

**Blood & Viscera**
- Burst 30–50 particles, lifetime 1.5–2.5s, size 5–15cm
- Color ramp `#5C2E2E` (fresh) → `#3D2F2F` (old)
- Opacity: 0.2s hold → 0.4s fade · gravity 0.3–0.5 · emissive 0.2
- Pools = decals, not floating particles

**Embers/Ash:** 50–100 p/s, lifetime 3–5s

**Scale-curve shape:** slow ease-out, NO overshoot, NO pop. Birth full → linger → soft fade. Particle death is the unease.

**HORROR Do / Don't**
- DO desaturate cool base; cool = dread
- DO keep emissive <0.5; restraint sells horror
- DO Translucent blend + volumetric transparent shadows (physical presence)
- DO fade to 0.1–0.2 lingering haze; slow death = unease
- DO test read at 10m / 25m / 50m — "something is wrong," not "flashy spell"
- DO CPU emitters for fog/blood/dust (<1000)
- DON'T bloom >0.35, emissive >0.5, or Additive fog (feels safe/heroic)
- DON'T spawn >500/emitter or lifetime <1s (cartoony pop)
- DON'T warm accents, chromatic aberration, motion blur
- DON'T cover >30% screen at close range; restraint is the point
- DON'T flicker >4 Hz or bright; stay 2–4 Hz, 0.4–0.8
- DON'T LOD by shrinking — cull emitters entirely at 5000+ units

---

### ACTION — Concrete Specs

**Palette (color-coded by damage type)**
- Melee: bright orange/red, HDR 8.0–10.0, sat 0.9+
- Ranged: cyan/yellow, HDR 6.0–8.0, sat 0.85+
- Heavy/boss: magenta/gold, HDR 10.0–14.0, sat 0.95+
- Core albedo pure black (0,0,0); emissive 2–3× brighter than albedo

**Timing (the core of action feel)**
- Anticipation: 200–300ms (pre-impact, draw inward 0.3–0.5 scale, +40–60% emissive, vertical squash on melee)
- Impact: 80ms (3–6 frames @60fps) anticipation peak → max expansion
- Dissipation: 400–800ms, ease-out cubic 1.5x→0.0
- Total lifespan: 800–1400ms (melee 900 / ranged 1100 / heavy 1300)

**Scale-curve shape (the "pop")**
- 0 → peak in 80ms (cubic ease-in 0→1→1.6)
- Overshoot 2.0–2.2x at frame 4–6 (1.3 overshoot factor)
- Settle to 1.0x by frame 12–15
- Cubic ease everywhere — NO flat sections, NO linear

**Emissive / Bloom cores**
- Launch 4.0–6.0 → peak 8.0–12.0 (heavy 10.0–14.0) → end 0.5–1.0
- Bloom-bright core: 2×2–3×3 px cluster at 10.0+ brightness, saturated pure hue (no gray) for efficient bloom
- Post bloom radius 0.5–1.0; control bloom via emissive, not post intensity >1.5

**Hit-stop + feedback (sync to frame 0)**
- Hit-stop: 100–150ms standard, 200–300ms heavy/crit
- Screen-shake: 3–5 px offset, 2–4 frame duration (min 2 frames)
- Niagara spawn → material emissive peak → screen-shake all fire t=0; async >50ms breaks tactile read

**Secondary debris/sparks**
- Burst 8–15 particles at impact frame
- 60% fall (gravity 500–800 UU/s²), 40% outward (radial 400–600 UU/s)
- Lifetime 600–1200ms, emission cone 30–45°
- Contrast color to core (orange core → yellow sparks); ±10–15% speed variation
- Don't outlive primary emissive peak by >300ms

**LOD:** mesh cards <3m, sprites 3–10m, pure particle >10m (reduce overdraw, keep pop)

**ACTION Do / Don't**
- DO cubic ease on all scale/emissive transitions (linear = robotic)
- DO add anticipation squash + color shift for read clarity
- DO pair hit-stop with synced screen-shake (frame 0)
- DO color-code by archetype (melee≠ranged≠heavy) for threat learning
- DO pure emissive cores (no spec maps); spec dilutes bloom
- DO normal maps on anticipation meshes (volume cue)
- DON'T desaturate <0.85 (reads "weak/environmental")
- DON'T dissipation >800ms in fast combat (screen clutter)
- DON'T white/near-white cores (sat <0.2) — bloom inefficient, wastes fill
- DON'T exceed 2.2x scale (>2.5x blows out screen)
- DON'T flat scale sections; continuous accel/decel reads polished

---

### Shared — Color & Readability Fundamentals

**Gradient mapping (grayscale → color, memory-efficient)**
- Sample 8-bit grayscale texture, multiply by color parameter, drive emissive
- One grayscale texture drives many ramps via dynamic params (saves ~2/3 memory vs colored)
- Step through multiple ramps over particle lifetime (Fallout 4 method)

**HDR emissive & bloom**
- Push channels >1.0 for HDR bloom; game range 1.0–3.0 (>3.0 risks exposure washout)
- Luminance L = 0.2126R + 0.7152G + 0.0722B — green blooms ~3.3× stronger than blue; prioritize green for readability
- Separate R/G/B channel intensity to bias bloom temperature (warm glow vs cold halo)
  - Warm explosion: (2.5, 1.0, 1.0) · Cool magic: (0.8, 1.5, 2.0)
- Reference emissive: Fire core (1.0,0.8,0.0) em 2.5 · Explosion (1.0,0.6,0.0) em 2.0 · Electrical (0.2,0.4,1.0) em 2.2 · Blood/horror (0.9,0.1,0.1) em 1.8

**Color-over-life ramps**
- Fire: white/yellow (0.0) → orange (0.6) → red (0.85) → dark red (1.0)
- Explosion: yellow (0.0) → orange (0.3) → red (0.6) → transparent (1.0)
- Alpha ramp: 1.0 → 0.8 → 0.3 → 0 (smooth dissolve)
- Use Color Over Life + Alpha Over Life together; desaturate edges/elder particles (core saturated, edge muted)

**Saturation discipline**
- Primary/critical FX: 85–95% · Secondary: 40–60% · Background anchor: 20–40%
- Never >2 highly-saturated colors in one frame (visual cacophony)
- Horror: desaturated base 30–50% + saturated danger spike (red 90%)

**Value / contrast hierarchy**
- Minimum 4.5:1 contrast ratio vs background average
- Bright core (value 0.8–1.0) → dark edge (0.1–0.3) = strong silhouette
- Complementary pairs for max contrast: red-cyan, orange-blue, yellow-purple
- Test on dark, mid-gray, AND bright-white backgrounds — must read on all three

**Team / damage color tokens (standardize, don't hardcode)**
- Store in a Material Parameter Collection; artists tune color + intensity only
- Player dmg = red (1,0,0) em 2.0 · Enemy = cyan (0,1,1) em 1.8 · Heal = green (0,1,0) em 1.5 · Spectral/void = purple (1,0,1) em 1.2
- Red+green alone fails colorblind users — add shape/scale differentiation
- Bake `EyeAdaptationInverse` (×1.0/exposure) into emissive for constant perceived brightness across lighting
