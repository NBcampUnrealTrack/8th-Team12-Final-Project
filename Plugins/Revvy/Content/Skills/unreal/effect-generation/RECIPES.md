## Per-Effect Build Recipes (material/Niagara-driven, no flipbook)

All effects are flipbook-free: motion, color, and dissolve come from material math driven by `Particle.NormalizedAge` + dynamic material parameters. Reusable nodes are factored into MFs — see the Reusable Material Function Library spec. Numbers below are starting values; tune in editor.

---

### 1. Stylized Fire — scrolling noise + age-based dissolve to embers

**Niagara stack** — `NS_Fire / SE_StylizedFire`, GPU, looping infinite
- Spawn.Basic: rate **50/s**
- Init.Life: **2.0–3.5 s** (lifetime range 1.5 s)
- Init.Velocity: linear **(0,0,0.8) m/s** upward
- Init.Position: offset (0,0,0)
- Update.Forces: gravity **(0,0,-0.15)**, damping **0.98**
- Update.Position: advection on
- Update.DynamicMaterialParameter: bind `NormalizedAge`, `FireGradientT`, `DissolveAlpha`
- Update.Scale (optional): **1.0 → 0.7** over life
- Render.Sprites: camera-facing (Screen), size **0.5–1.2 cm**, `M_StylizedFire_Master`

**Material graph** — `M_StylizedFire_Master`, **Additive**, Surface, two-sided
1. `MF_Panner2Layer`(T_FireNoise_Base @ speed 0.3, T_FireNoise_Detail @ speed 0.6, dir (0,1)) → Combined Noise (~0.05–0.25)
2. `MF_GradientMap`(T_FireGradient_LUT, T = `FireGradientT`) → fire color (red→orange→yellow→white)
3. Multiply(CombinedNoise, FireColor) → Colored Fire
4. Lerp(ColoredFire, EmberColor `#8B4513`, saturate(NormalizedAge − 0.8)) → age color transition
5. `MF_Dissolve`(mask=CombinedNoise, alpha=`DissolveAlpha`) → eroded edges
6. Multiply final × **2.5** → Emissive; opacity from DissolveAlpha mask

**Dynamic params + curves**
| Param | Input | Curve | Use |
|---|---|---|---|
| NormalizedAge | Particle.NormalizedAge | linear 0→1 | panner time, LUT lookup |
| FireGradientT | saturate(clamp(NA,0,0.8)) | 0→1 over 0–0.8, flat after | LUT coord |
| DissolveAlpha | saturate(1 − (NA−0.6)·2.5) | 1→0, threshold @ 0.6 | erode to embers |
| PannerUV_Base | NA·0.5 | linear ramp | base scroll (0.3) |
| PannerUV_Detail | NA·0.8 | linear ramp | detail scroll (0.6) |

**Texture inputs**
- T_FireNoise_Base — *procedural* Perlin/Simplex, 2K R, scale 0.05–0.1, 2 oct
- T_FireNoise_Detail — *procedural* fBm, 2K R, scale 0.1–0.25, 4 oct, lacunarity 2.0
- T_FireGradient_LUT — *codex*: "1D horizontal gradient deep red #8B0000 → yellow #FFFF00 → white, smooth, 512×8, fire color ramp for additive VFX"

**Build steps**
1. Create `M_StylizedFire_Master` (Additive, two-sided), build graph via MFs above; emissive ×2.5.
2. Create `NS_Fire` + `SE_StylizedFire` (GPU); spawn 50/s, life 2.0–3.5 s, vel (0,0,0.8).
3. Add Forces (grav −0.15 Z, damp 0.98); add DynamicMaterialParameter bindings (NormalizedAge, FireGradientT, DissolveAlpha).
4. Sprite renderer camera-facing, size 0.5–1.2 cm; emitter Duration=Infinite, Loop=Infinite.
5. Test: upward motion, red→white color, dissolve-to-ember erosion @ ~60% life, additive silhouette.

---

### 2. Stylized Smoke — soft translucent upward drift, panned noise + flowmap distortion

**Niagara stack** — `NS_StylizedSmoke_01 / NE_SmokeBase_01`, CPU sprite
- Spawn rate **100/s**; Lifetime **4.0–6.0 s**
- Init: size **20–40 UU**, color (1,1,1, α 0.7)
- Velocity: const **(0,0,80) UU/s** + Curl Noise (scale 0.5, 0.8 Hz)
- Damping **0.3**, gravity off
- Scale over life **1.0 → 1.2**
- Opacity over life: 0 → **0.8 @ 30%** → 0 (ease-in-out)
- Init Rotation: random 0–360°
- Renderer: Sprite translucent, **Soft Particles fade 50 UU**; bounds 200×200×300

**Material graph** — `M_StylizedSmoke_Panned`, **Translucent**, Unlit
1. `MF_Panner2Layer`/single-layer: T_CloudNoise_02 @ UV(Time·PanU, Time·PanV)
2. T_FlowMap_Distort RG @ (Time·FlowRotate + DistortAmount·flow) → distortion offset
3. Multiply(CloudNoise, FlowMap) → distorted cloud
4. × VertexColor.RGB (particle tint) → colored cloud
5. × DensityMultiplier (2.0) → Saturate → contrast boost
6. × VertexColor.A → final opacity
7. `MF_SoftParticle` depth fade applied at edges. Emissive = cloud ×1.0 (flat, translucency-driven)

**Dynamic params + curves**
| Param | Curve | Note |
|---|---|---|
| PanSpeed_U | 0.05 → 0.18 @40% → 0.02 | fast early, slow at fade |
| PanSpeed_V | 0.03 → 0.12 @50% → 0.01 | slower vertical drift |
| FlowRotate | 0.2 → 0.3 @30%, flat | quick lock-in |
| DistortAmount | 0.15 → 0.03 (ease-out) | turbulent start, smooth end |
| DensityMultiplier | flat 2.0 | constant contrast |

**Texture inputs**
- T_CloudNoise_02 — *procedural* 512² seamless Perlin/Voronoi grayscale fBm 4–5 oct, lacunarity 2.0, sRGB off
- T_FlowMap_Distort — *procedural* (Houdini/Python curl of Perlin) 256² RG flowmap, float16 EXR, sRGB off, ~0.5 rad/s circulation

**Build steps**
1. Create `NS_StylizedSmoke_01`, bounds (200,200,300), auto-expand.
2. Add `NE_SmokeBase_01`: spawn 100/s, life 4–6 s, size 20–40, α 0.7.
3. Velocity (0,0,80) + Curl Noise (0.5, 0.8 Hz); Damping 0.3.
4. Scale 1.0→1.2; Opacity curve (peak 0.8 @30%); random rotation.
5. Sprite renderer translucent + Soft Particles (50 UU); assign `M_StylizedSmoke_Panned`.
6. Material: Translucent/Unlit, 2-layer panner × flowmap × vertex color × density 2.0 → opacity.
7. Bind PanSpeed_U/V, FlowRotate, DistortAmount curves via DynamicParameter module; compile + test (no flicker).

---

### 3. Magic Aura / Energy Swirl — polar UV motion + Fresnel rim

**Niagara stack** — `NS_AuraSwirl`, 2 GPU sprite emitters, infinite loop, 30 warm-up frames
- **AuraSwirl**: spawn **50/s**, life **3.0 s**, size 1.0→0.2, screen-aligned
- **AuraPulse**: spawn **30/s**, life **2.5 s**, size 1.2→2.5, screen-aligned
- Both: SolveForces, ScaleColor, ColorFromCurve; `M_AuraSwirl_Master`

**Material graph** — `M_AuraSwirl_Master`, **Additive**, Unlit, two-sided
1. `MF_PolarUV`(center (0.5,0.5), axis Z, angle = Time·2) → swirl UVs
2. `MF_Panner2Layer`/single: T_NoiseStreak @ panned swirl UVs (speed 0.5,0)
3. ComponentMask R → single-channel detail
4. `MF_FresnelRim`(exponent 3.0) → rim
5. Multiply(noise, fresnel) × ParticleColor.RGB × ParticleAlpha
6. × EmissiveIntensity (2.0) → Emissive; Opacity = ParticleColor.A; Normal = (0,0,1)

**Dynamic params + curves**
| Emitter | Param | Curve/Formula |
|---|---|---|
| AuraSwirl | Color | t0 (1,0.2,0.8) → t0.5 (0.1,0.5,1.0) → t1 (1,1,0.5) |
| AuraSwirl | Emissive | 2.0 + 0.8·sin(NA·π) breathing |
| AuraPulse | Color | const (0.2,0.8,1.0) cyan |
| AuraPulse | Emissive | 3.0 + 1.2·sin(NA·2·2π) (2 flashes) |
| Global | SwirlRotation 2.0, NoisePan 0.5, FresnelExp 3.0, OverallEmissive 2.0 | MPC |

**Texture inputs**
- T_NoiseStreak — *procedural* Voronoi/Perlin streaky grayscale 512²–1024², high-freq, seamless (or *codex*: "512×512 Voronoi streaky noise, high-frequency, grayscale, seamless, for swirling energy")
- T_Fresnel_Rim (optional) — *procedural* 1D black→white falloff LUT 1024×1

**Build steps**
1. `create_system('NS_AuraSwirl','/Game/Effects/VFX')`.
2. Add emitters `AuraSwirl` + `AuraPulse` (GPU sprite templates).
3. AuraSwirl: lifetime 3.0, add SolveForces + ScaleColor + ColorFromCurve; SpriteRenderer → `M_AuraSwirl_Master`.
4. AuraPulse: lifetime 2.5, size 1.5, color modules, SpriteRenderer.
5. Spawn rates 50 / 30.
6. `create_material('M_AuraSwirl_Master')`; set Additive/Unlit/two-sided.
7. Add NoiseStreak texture param, ParticleColor, Fresnel(exp 3.0), Panner(0.5,0.0), Multiply chain, Intensity(2.0) → Emissive; ParticleColor.A → Opacity.
8. `compile_material` + `layout_expressions` + save; compile system.
9. Make `MI_AuraSwirl_CyanBlue` (Intensity 3.5, FresnelExp 2.5, PanX 0.8). Import T_NoiseStreak. Open in editor + test.

---

### 4. Shockwave Ring — expanding mesh ring, single burst (impact)

**Niagara stack** — `NS_ShockwaveRing_Impact / Emitter_ShockwaveRing`, CPU, single burst
- Spawn rate 1.0, duration **0.01 s** (1 particle)
- Lifespan **0.35 s**; velocity (0,0,0) — scale-driven expansion
- Mesh renderer: SM_Ring_Flat_Torus, **Additive**, no decals, SortByAge
- Scale over life: **0.08 → 1.2** (exp ease-out)
- 4 material-param updates: DynamicRadialScale, DynamicAlpha, DynamicFresnelPower(2.5), DynamicEmissiveIntensity

**Material graph** — `M_ShockwaveRing_Additive`, **Additive**, Unlit, single-sided
1. `MF_GradientMap`/radial: T_RadialGradient sampled at mesh UV → GradientMask
2. RadialDistance = length(TexCoord − 0.5)·2
3. `MF_FresnelRim` via Power(RadialDistance, FresnelPower) → edge emphasis
4. Multiply(GradientMask, FresnelEdge) → EdgeMask
5. `MF_Panner2Layer`/scroll: T_RadialGradient @ (UV + scroll·0.3) → detail; multiply into edge
6. Lerp(EdgeMask, EdgeMask·0.8, RadialScale) → FinalRadialMask
7. × DynamicAlpha → Opacity; × EmissiveIntensity·2 (clamp 4) → FinalEmissive
8. × (0.8,1.0,1.2) white-blue tint → BaseColor/Emissive

**Dynamic params + curves**
| Param | Binding | Curve |
|---|---|---|
| DynamicRadialScale | NormalizedAge | exp ease-out (0,0.08)→(1,1.2) |
| DynamicAlpha | NormalizedAge | flat 1.0 to 70%, then linear →0 |
| DynamicFresnelPower | const | 2.5 |
| DynamicEmissiveIntensity | NormalizedAge | gaussian peak @ mid (0,0.3)→(0.5,1.0)→(1,0.1) |

**Texture inputs**
- T_RadialGradient — *procedural* 256² white center → black edge, linear, clamp
- T_DisplacementNoise (optional distortion) — *procedural* 512² Perlin seamless, 4 oct, R = normal variation
- T_EdgeGradientExponential (optional) — *codex* baked pow(radial, 2.5); or compute via Power node

**Build steps**
1. Reference/create SM_Ring_Flat_Torus (flat disc, 256 seg, r=100 cm).
2. Generate T_RadialGradient (256²), T_DisplacementNoise (512²).
3. Create `M_ShockwaveRing_Additive` (Additive/Unlit/single-sided); build radial-dist → power(fresnel) → gradient → scroll → alpha → emissive×2 (clamp 4) → white-blue tint.
4. Create CPU emitter: spawn 1 @ dur 0.01, life 0.35, vel 0; mesh renderer (SM_Ring, M_Shockwave, Additive, SortByAge); fixed bounds 300³.
5. Scale curve 0.08→1.2 exp ease-out; add 4 material-param updates with curves above.
6. Create `NS_ShockwaveRing_Impact`, add emitter; warm-up 0.
7. Test: ~8 cm → 120 cm over 0.35 s, bright ~250 ms then fade, fresnel edge glow. Tune ease-out bias / fresnel.
8. Optional `BP_ShockwaveRing` (NiagaraActor, Activate on BeginPlay) → `SpawnSystemAtLocation` on hit.

---

### 5. Spark / Ember Burst — GPU velocity-aligned, hot-to-dark

**Niagara stack** — `NS_SparkBurst / NE_SparkBurst`, GPU, no loop
- Spawn: burst **120**, delay 0
- Init: mass 0.1, life **1.2 s**, pos (0,0,0)
- Velocity: RandomCone **30°** Z, speed **300–800**
- Forces: gravity **0.8**, drag **0.5**
- Size: linear **0.3 → 0.05**
- Color: Curve_SparkColor (white-yellow → orange → red → black)
- DynamicMaterialParameter: ParticleVelocity (for stretch)
- Renderer: Sprite **VelocityAligned**, `MI_SparkBurst`, ViewDepth sort; bounds ±500

**Material graph** — `M_SparkBurst_Master`, **Additive**, Unlit
1. T_Spark_Round sampled (static UV)
2. `MF_GradientMap`/lerp: Lerp(HotColor (1,0.6,0.1), DarkColor (0.3,0,0), NormalizedAge) → ColorOverLife
3. × Curve_EmissiveFade × 4.0 → Emissive
4. Opacity = SpriteTex.A × ParticleAlpha × Curve_OpacityFade
5. Normal = (0,0,1); BaseColor = black

**Dynamic params + curves**
| Curve | Keys (NA → value) |
|---|---|
| Curve_EmissiveFade | 0→1.0, 0.25→0.9, 0.5→0.7, 0.75→0.4, 1→0 (exp decay) |
| Curve_SparkColor | white-yellow → orange → dk-orange → deep red → black |
| Curve_OpacityFade | 0→1.0, 0.67→0.8, 1→0 (linear drop final 0.4 s) |

**Texture inputs**
- T_Spark_Round — *procedural*/*codex* 64² white radial gaussian falloff to transparent, clamp
- T_Spark_Streak — *codex* 256×64 velocity-aligned white directional streak
- T_NoiseDistort — *codex* 128² single-octave Perlin, 0–1 R, for emissive flicker

**Build steps**
1. Create `M_SparkBurst_Master` (Additive/Unlit, two-sided off); build round-tex → lerp hot/dark by NA → ×EmissiveFade ×4 → emissive; opacity via curves.
2. Create curves Curve_EmissiveFade, Curve_SparkColor, Curve_OpacityFade.
3. Create `MI_SparkBurst` (EmissiveBrightness 4.0, SpriteTexture = T_Spark_Round).
4. Generate textures (round/streak/noise) via codex.
5. Create `NE_SparkBurst` (GPU): burst 120, life 1.2, mass 0.1, size 0.3; velocity RandomCone 30° 300–800; forces gravity 0.8 / drag 0.5; size 0.3→0.05; color curve; ParticleVelocity dyn param.
6. Sprite renderer VelocityAligned, MI_SparkBurst, bounds ±500.
7. Create `NS_SparkBurst` (add emitter), `BP_SparkBurst` (AutoDestroy true).
8. Test: 30° cone, gravity fall, white-yellow → red-black over 1.2 s; GPU cost < 0.5 ms.

---

### 6. Energy Trail / Sword Slash — ribbon trail, MPC color, shimmer + distortion

**Niagara stack** — `NS_SwordSlash_Trail / NE_SwordSlash_Trail`, **CPU** (ribbon coherence), fixed bounds (200,200,200)
- Max particles 200; lifetime **0.8 s** (±0.1)
- Spawn burst **25** (±2); rate 0 (burst-driven)
- Init: mass 1.0, random seed
- Update Position: inherit velocity, drag 0.3, gravity 0
- Velocity Field: curl/simplex scale 0.15, freq 2.0
- Ribbon Renderer: Trailing, max width **20 cm**, taper cubic ease-in (0→1→0), Additive, Unlit, OldestFirst, UV tiling U=1 **V=8**, link-by-distance ≤50 cm, subdivisions 8
- ColorOverLife bound to MPC Color0/1/2

**Material graph** — `M_SwordSlash_Trail`, **Additive**, Unlit (Surface, used-with-Niagara)
1. T_Ribbon_Gradient @ U=ribbon length → base gradient × ParticleColor.RGB (from MPC)
2. `MF_Panner2Layer`/scroll: T_Ribbon_Detail @ V + Time·4.0 → shimmer (×0.3 additive)
3. T_Distortion_Noise → remap −1..1 → UVOffset = noise·0.05·NormalizedAge → re-sample detail
4. `MF_FresnelRim`(power 3.0, bias 0.2) remap 0.3–1.0 → edge glow + soft edge
5. Width taper × RibbonWidth → opacity multiplier
6. Final color = (base + shimmer)·fresnel·sin(NA mid-pulse); Emissive ×2.0; Opacity = gradient.A·detail.R·fresnel·taper·AlphaLifeCurve

**Dynamic params + curves**
| Param | Curve/Source |
|---|---|
| NormalizedAge | linear 0→1 over 0.8 s |
| ParticleColor (MPC Color0/1/2) | t0 C0 100% → t0.3 C1 peak → t0.6 fade → t1 0 (cubic ease-out) |
| RibbonWidth | cubic ease-in 0→1@0.1→1@0.65→0@1 |
| EmissiveIntensity | bell 0.2→1.0@0.4→0 |
| DistortionAmount | Fresnel·(1−NA²) |
| DetailScrollSpeed | MPC scalar, const 4.0 |

**Texture inputs**
- T_Ribbon_Gradient — *procedural* 64² white→transparent 1D ramp, clamp, BC4/DXT1
- T_Ribbon_Detail — *procedural*/*codex* 256² Perlin 4 oct seamless R8, V-scroll shimmer
- T_Distortion_Noise — *procedural*/*codex* 256² Simplex 3 oct, remap −0.5..0.5, R8
- T_Taper_Falloff (optional) — *procedural* 256×1 gaussian falloff; prefer in-shader Fresnel

**Build steps**
1. Create `MPC_SwordSlashColors`: Scalar DetailScrollSpeed (4.0), Vectors Color0 (1,0.5,0,1), Color1 (1,0.8,0.2,1), Color2 (1,1,0.5,1).
2. Create textures (gradient 64², detail 256², distortion 256²).
3. Create `M_SwordSlash_Trail` (Additive/Unlit, used-with-Niagara); graph per above; expose DetailScrollSpeed, BaseColorMultiplier, DistortionStrength + texture params.
4. Create `NE_SwordSlash_Trail` (CPU): life 0.8±0.1, burst 25±2, max 200; update position drag 0.3; velocity field curl 0.15/2.0; bind MPC colors.
5. Ribbon renderer: Trailing, width 20, taper cubic, MI material, OldestFirst, UV U1 V8, subdivisions 8.
6. Create `NS_SwordSlash_Trail` (single emitter, bounds 200³, warmup 0).
7. Create `MI_SwordSlash_Trail` (DetailScroll 4.0, Distortion 0.05); assign to ribbon.
8. Create curves Curve_RibbonAlpha, Curve_RibbonWidth, Curve_EmissiveIntensity; assign to modules.
9. Optional `BP_SwordSlash_Spawner` — Niagara comp on sword, burst on swing inheriting weapon velocity.
10. Test: ribbon trails, gradient fade, V-scroll shimmer, width taper head/tail, MPC live color tweak, single draw call/emitter.

---

### Cross-effect quick reference

| Effect | Sim | Renderer | Blend | Spawn | Life | Key MFs |
|---|---|---|---|---|---|---|
| Fire | GPU | Sprite | Additive | 50/s loop | 2–3.5 s | Panner2Layer, GradientMap, Dissolve |
| Smoke | CPU | Sprite | Translucent | 100/s | 4–6 s | Panner2Layer, SoftParticle |
| Aura | GPU | Sprite ×2 | Additive | 50+30/s loop | 2.5–3 s | PolarUV, FresnelRim, Panner2Layer |
| Shockwave | CPU | Mesh | Additive | 1 burst | 0.35 s | GradientMap, FresnelRim, Panner2Layer |
| Spark | GPU | Sprite (vel) | Additive | 120 burst | 1.2 s | GradientMap |
| Sword Trail | CPU | Ribbon | Additive | 25 burst | 0.8 s | Panner2Layer, FresnelRim, GradientMap |

---

# 확장 효과군 (Iteration 11)

---

### 7. Water Splash / Spray — expanding crown mesh + droplet sprites + foam ring, refraction, blue-white

**Niagara stack** — `NS_WaterSplash`, GPU, single burst on activate (Duration=Infinity, loop off, warmup 0)
- **NE_SplashCrown** (mesh sprite, 1 burst): spawn 1 @ dur **0.02 s**, life **0.6 s**, vel (0,0,0) — scale-driven
- Update.Forces: gravity **0**, damping **1.0** (lock velocity)
- Update.Scale over life: **0.05 → 1.3** (exp ease-out, peak ~0.4 s)
- Update.DynamicMaterialParameter: bind `DynamicCrownAlpha`, `DynamicRefractStrength`, `DynamicSurfaceDepth`
- Mesh renderer: SM_SplashCrown_Ring, `M_WaterSplashCrown_Master`, Translucent, sort by age, bounds **400×400×200**
- **NE_Droplets** (sprite, cascade): spawn **80/burst**, delay **0.08 s**; life **1.5–2.2 s** (range 0.7)
- Init.Velocity: RandomCone **45° Z**, speed **150–350 cm/s**; size **0.4–0.8 cm**; color white (1,1,1,0.9)
- Update.Forces: gravity **250 cm/s² Z**, drag 0.2, air resistance 0.15
- Update.Size: linear **0.6 → 0.05**; Opacity: 1.0 → 0.8 @30% → 0 (ease-in-out)
- Sprite renderer: camera-facing (Screen), sort by depth, no soft particles, bounds ±600
- **NE_FoamRing** (sprite): spawn **120/burst**, delay **0.12 s**; life **0.8–1.2 s** (range 0.4)
- Init.Position: spiral XY ±80 cm ring, Z=0; velocity radial outward **80–180 cm/s** (RandomCone 15° horiz); size **0.6–1.8 cm**
- Update.Scale: 1.0 → 1.4; Opacity: 0 → 0.7 @20% → 0.6 @50% → 0; color cyan tint (0.7,1.0,1.0)
- Sprite renderer camera-aligned, `M_WaterFoamRing`, Translucent, bounds ±300

**Material graph** — three materials
`M_WaterSplashCrown_Master`, **Translucent**, Lit, two-sided, Refraction enabled
1. `MF_Panner2Layer`(T_WaterNormal_Base @ 0.5 dir (1,0), T_WaterNormal_Detail @ 0.9 dir (0.7,0.8), timeScale NA·0.8) → RefractNormal
2. RefractNormal × `DynamicRefractStrength` (0.15) → refraction offset
3. `MF_PolarUV`(center (0.5,0.5), axis Z, angle Time·1.2) → polar UVs for radial flow
4. `MF_Panner2Layer`(T_WaterDistort @ polar UV speed 0.4, T_FoamSpeckle @ UV speed 0.6) → DistortedNoise (0.0–0.2)
5. Lerp(refraction sample, white, saturate((NA−0.5)·3)) → SurfaceColor (white foam crest)
6. `MF_FresnelRim`(exp 2.0, bias 0.1, remap 0.2–1.0) → RimFresnel; × 0.8 + DistortedNoise × 0.2 → Fresnel
7. Multiply(SurfaceColor, Fresnel) × (0.8,1.0,1.2) blue-white → Emissive **×0.5** (subtle), × `DynamicCrownAlpha`
8. Opacity = (RimFresnel·0.9 + DistortedNoise·0.1) × saturate(1−(NA−0.4)·1.5) × `DynamicCrownAlpha`; Normal = RefractNormal

`M_WaterDroplet_Sprite`, **Translucent**, Lit, two-sided, no refraction
1. T_Droplet_Sphere → DropletMask (radial falloff); `MF_FresnelRim`(exp 4.0, bias 0.2) → DropletFresnel
2. Lerp(cyan (0,0.8,1.0), white, saturate(DropletFresnel + NA·0.3)) → DropletColor × **1.5** → Emissive (bright)
3. Opacity = DropletMask × DropletFresnel × ParticleAlpha × saturate(1−(NA−0.7)·2.0); Normal = (0,0,1)

`M_WaterFoamRing`, **Translucent**, Unlit, two-sided
1. T_Foam_SpecklesAlpha → FoamMask; `MF_Panner2Layer`(T_FoamSpeckles @ 0.3, T_FoamJitter @ 0.7, NA) → FoamDetail
2. FoamDetail × FoamMask → FoamPattern; `MF_FresnelRim`(exp 1.5, bias 0.3, remap 0.1–0.9) → RimFade
3. Lerp(white, cyan (0.6,1.0,1.0), NA·0.5) × ParticleColor.RGB × **1.2** → Emissive
4. Opacity = FoamPattern × RimFade × ParticleAlpha (curve-modulated); Normal = (0,0,1)

**Dynamic params + curves**
| Param | Binding | Curve (NA → value) |
|---|---|---|
| DynamicCrownAlpha | NormalizedAge | 0→0, 0.15→0.9, 0.4→0.85 (hold), 0.8→0.3, 1→0 |
| DynamicRefractStrength | NormalizedAge | 0→0.15, 0.2→0.20, 0.5→0.12, 0.8→0.03, 1→0 |
| DynamicSurfaceDepth | NormalizedAge | 0→0, 0.25→1.0, 0.6→0.5, 1→0 |
| CrownScaleExpand | NormalizedAge | 0→0.05, 0.4→1.3 (exp ease-out), 0.6–1→1.35 settle |
| DropletDragFade | NormalizedAge | 0→0.2, 0.5→0.25, 1→0.15 |
| FoamScaleGrow | NormalizedAge | 0→1.0, 0.5→1.4, 1→1.35 |
| FoamColorSaturation | NormalizedAge | 0→1.0, 0.5→0.8, 1→0.4 |

**Texture inputs**
- T_WaterNormal_Base — *procedural* Perlin normal 512² seamless, 2 oct, scale 0.05–0.1, DXT5 (blue-dominant)
- T_WaterNormal_Detail — *procedural* Simplex normal 512² seamless, 4 oct, scale 0.1–0.25, DXT5
- T_WaterDistort — *procedural* 256² radial gradient white center → transparent edge, R8, clamp
- T_FoamSpeckle / T_FoamSpeckles — *codex*: "foam whitecap speckle noise, 512²/256² seamless, 5–6 oct (Voronoi/Perlin hybrid), ~40% transparent / ~60% opaque, R8 sRGB-off, clamp"
- T_Droplet_Sphere — *codex*: "128² water droplet, radial gaussian white center, soft transparent falloff, bright fresnel rim, clamp, sRGB-off"
- T_FoamJitter — *procedural* Voronoi cells 256² seamless, scale 0.3–0.5, R8 sRGB-off

**Build steps**
1. Create 3 materials per graph (Crown: Translucent/Lit/two-sided + Refraction; Droplet: Translucent/Lit, no refraction; Foam: Translucent/Unlit); expose RefractStrength (0.15), CrownAlpha (0.9), DropletFresnelPower (4.0), FoamFresnelPower (1.5) + texture params; compile to `/Content/VFX/Materials/Water/`. Make MIs.
2. Generate textures to `/Content/VFX/Textures/Water/` (normals DXT5, distort/jitter R8, foam + droplet via codex); assign to MIs.
3. Create `NS_WaterSplash`; **NE_SplashCrown**: spawn 1 @ dur 0.02, life 0.6, vel 0; forces grav 0 / damp 1.0; scale 0.05→1.3 exp ease-out; bind 3 dyn params; mesh renderer SM_SplashCrown_Ring, MI crown, bounds 400×400×200.
4. **NE_Droplets**: burst 80 delay 0.08, life 1.5–2.2, vel RandomCone 45° Z 150–350; forces grav 250 Z / drag 0.2 / air 0.15; size 0.6→0.05; opacity curve; sprite camera-facing, MI droplet, bounds ±600.
5. **NE_FoamRing**: burst 120 delay 0.12, life 0.8–1.2, spiral ±80 cm ring, vel radial 80–180; scale 1.0→1.4; opacity S-curve; sprite camera-aligned, MI foam, bounds ±300.
6. System: Duration=Infinity, no loop, warmup 0, single burst on activate; fixed bounds 400×400×200; compile. Optional `BP_WaterSplashActor` (poolable, activate on impact).
7. Test: crown 0.05→1.3 over 0.6 s with blue refraction wave, alpha fade @0.4 s; droplets burst +0.08 s arc down under 250 cm/s²; foam +0.12 s radial, peak @50%, cyan haze by 1.2 s; emissive subtle (crown ×0.5, droplets ×1.5, foam ×1.2). GPU < 0.8 ms (~250 particles).

---

### 8. Electric Arc / Lightning Chain — ribbon arc + noise jitter, additive white-cyan core, fast flicker, branches

**Niagara stack** — `NS_ElectricArc / NE_ArcMain`, GPU, single-burst, very short life
- Spawn.Basic: rate **1.0** (single arc per spawn call), single-burst
- Init.Life: **0.15–0.25 s** (very short)
- Init.Position: world arc source or bound to skeletal socket; Init.Velocity zero (position-based)
- Update.Forces: none; Update.Position: none (arc driven by ribbon linking)
- Update.DynamicMaterialParameter: bind `NormalizedAge`, `ArcFlickerAmount`, `ArcJitterAmount`, `ColorIntensity`, `DissolveAlpha`
- Ribbon renderer: Trailing, max particles **20**, max width **8–15 cm**, taper none/ease-out, **Additive**, Unlit, OldestFirst, UV tiling U=1 **V=4**, link-by-distance ≤**25 cm**, subdivisions **4**, `M_ElectricArc_Master`
- **NE_ArcBranch** (optional sub-ribbon): spawn **0.3–0.8/parent**, same life, width 3–6 cm, 45° offset, link to parent positions

**Material graph** — `M_ElectricArc_Master`, **Additive**, Unlit, Surface, single-sided
1. Sample T_LightningNoise @ (TexCoord0 + NA·3.5, TexCoord1 + Time·8.0) → Jitter mask (R)
2. `MF_Panner2Layer`(T_ArcDetail_Base @ 0.8 dir (1,0), T_ArcDetail_Crisp @ 2.2 dir (0,1), timeScale NA·2.5) → Detail (multiply); ComponentMask R → streak
3. Power(Detail, 1.8) → sharpen arc lines
4. Sample T_ArcGradient_1D (white-cyan-blue-white LUT) @ NA → BaseArcColor
5. `MF_FresnelRim`(exp 4.5, bias 0.1) → Rim; Add(BaseArcColor, Rim·0.6) → CoreColor
6. Add(Detail, Jitter × `ArcJitterAmount`) → combined detail; Multiply(CoreColor, Detail) → colored arc
7. Sine(NA·π·FlickerFreq) remap 0.6–1.2 → FlickerMultiplier; × `ColorIntensity` (3.0) → FinalEmissive
8. Optional `MF_Dissolve`(mask=Detail, alpha=`DissolveAlpha`, threshold @0.8 life) → fade-out
9. Opacity = saturate(Detail·Detail); Normal = (0,0,1); optional WPO = Detail·NA·0.5

**Dynamic params + curves**
| Param | Binding | Curve/Formula |
|---|---|---|
| NormalizedAge | Particle.NormalizedAge | linear 0→1 over 0.15–0.25 s |
| ArcFlickerAmount | const/curve | 1.0 (or 0→1.2@0.3→0.8@1) |
| ArcJitterAmount | NormalizedAge | 0→0, 0.2→1.0, 0.8→0.3, 1→0.1 (peak mid) |
| ColorIntensity | NormalizedAge | 0→0, 0.1→3.5, 0.5→2.5, 0.8→0.4, 1→0 |
| DissolveAlpha | NormalizedAge | flat 1.0 until 0.8, then 1→0 |
| FlickerFreq | MPC scalar | 12.0 rad/s (~2 Hz) |
| JitterScale | MPC scalar | 0.3–0.5 cm point displacement |

**Texture inputs**
- T_LightningNoise — *procedural* 256² Simplex/Perlin cellular, seamless, 3 oct, lacunarity 2.0, freq 0.5, R8 sRGB-off, clamp
- T_ArcDetail_Base — *procedural* 512² Perlin streamline (V-axis aligned), seamless, 2 oct, R8 sRGB-off
- T_ArcDetail_Crisp — *procedural* 512² highpass Voronoi cracks, seamless, high-freq, R8 sRGB-off
- T_ArcGradient_1D — *codex*: "512×8 1D gradient, white center → cyan @0.3 → electric blue (0,0.7,1) @0.6 → bright white @1.0, additive lightning core ramp, clamp"

**Build steps**
1. Generate/import 4 textures to `/Game/Effects/VFX/Textures/`.
2. Create `M_ElectricArc_Master` (Additive/Unlit/Surface, single-sided); expose ColorIntensity (3.0), JitterScale (0.35), FlickerFreq (12.0), ArcWidthCM (10.0) + texture params; build graph above.
3. Make `MI_ElectricArc_Core` (Intensity 3.5, JitterScale 0.4, FlickerFreq 14.0) and `MI_ElectricArc_Branch` (Intensity 2.0, JitterScale 0.25, FlickerFreq 10.0).
4. Create `NS_ElectricArc`; **NE_ArcMain**: spawn 1/burst, life 0.20, pos socket, vel 0; add Update.Life + DynamicMaterialParameter (no forces). Ribbon: Trailing, width 10 cm, max 20, link ≤25 cm, subdivisions 4, MI core, Additive, OldestFirst, UV U1 V4.
5. Optional **NE_ArcBranch**: spawn 0.4/activation, lateral offset ±(5–10) cm, life 0.18, ribbon width 5 cm, MI branch.
6. Create curves Curve_ArcColorIntensity (0→0, 0.1→3.5, 0.5→2.5, 0.8→0.4, 1→0) and Curve_ArcJitter (0→0, 0.2→1.0, 0.8→0.3, 1→0.1).
7. Create `MPC_ElectricArc` (ColorIntensity_Global 3.0, FlickerFreq 12.0, JitterScale 0.35; vectors ArcCoreColor (1,1,0.2), ArcGlowColor (0,0.8,1.0)); reference in material.
8. Bind dyn params (NA, ArcFlickerAmount, ArcJitterAmount, ColorIntensity, DissolveAlpha 1.0 until 80% then 0). Optional screen-space distortion via SceneTexture × Detail·0.02.
9. Compile; `BP_ElectricArc` (auto-activate false). Test: 0.20 s bright white-cyan core + glow, ~2 Hz flicker, jitter, fade by 0.25 s. No flipbook — all motion is material math.
10. Tuning knobs: Lifetime 0.15–0.35 s, ColorIntensity 2.0–5.0, JitterScale 0.1–0.7, FlickerFreq 8–16 Hz, ArcWidth 5–20 cm.

---

### 9. Stylized Ice / Frost — mesh shards (translucent + Fresnel), dissolve frost spread, cold cyan-white, slow→snap

**Niagara stack** — `NS_StylizedFrost_Shards / NE_FrostShards`, GPU/CPU hybrid, infinite loop, warmup 20 frames
- Spawn.Basic: rate **35/s**; Init.Life **3.5–5.0 s** (range 1.5)
- Init.Velocity: linear **(0,0,0.4) m/s** up + RandomCone **20°** spread (speed 100–150 cm/s)
- Init.Position: offset box ±10 cm XY; Init.Rotation random 0–360°
- Update.Forces: gravity **(0,0,-0.08)**, damping **0.92**; advection on
- Update.Scale: **1.0 → 1.3** (ease-out 0.4 s, then hold)
- Update.ColorOverLife: white → pale-cyan
- Update.DynamicMaterialParameter: bind `NormalizedAge`, `FrostSpread`, `IceOpacity`, `FresnelPower`
- Mesh renderer: SM_IceShard_01, camera-facing (Screen), size **0.8–1.8 cm**, `M_StylizedFrost_Master`, Translucent, sort ViewDepth, bounds 300×300×400

**Material graph** — `M_StylizedFrost_Master`, **Translucent**, Unlit, two-sided (refractive index 1.33)
1. `MF_Panner2Layer`(T_FrostNoise_Detail @ 0.25, T_IceNoise_Crystalline @ 0.15, dir (0.5,0.3)) → CombinedFrostNoise (~0.1–0.3)
2. `MF_Dissolve`: saturate((CombinedFrostNoise − (0.2 + `FrostSpread`·0.6))·2.0) → FrostEdgeMask (erodes inward via FrostSpread 0→1)
3. ComponentMask R → frost progression
4. `MF_FresnelRim`(exp 2.0 + `FresnelPower`, bias 0.1) → rim
5. `MF_CylindricalUV` sampled w/ Panner (0, 0.8·NA) → subsurface-like depth variation
6. Multiply(CombinedFrostNoise, FrostEdgeMask) × (0.95,1.0,1.1) cyan-white → FrostPattern
7. Lerp(FrostPattern, FrostPattern·Rim, 0.6) → rim-emphasized frost; × `IceOpacity` → Opacity
8. × **1.8** emissive (subtle glow); BaseColor = lerp white → pale-cyan (0.85,0.95,1.0) via ParticleColor × FrostSpread; Normal = Panner.RG remap −1..1

**Dynamic params + curves**
| Param | Formula | Note |
|---|---|---|
| NormalizedAge | Particle.NormalizedAge | panner time, LUT (primary driver) |
| FrostSpread | saturate(clamp(NA−0.15,0,0.85)·1.4) | 0 @0–0.15 (freeze hold), rapid spread @0.6 (snap) |
| IceOpacity | saturate(1−(NA−0.7)·1.8) | hold 1.0 to 70%, drop over final 30% |
| FresnelPower | 2.0 + 1.5·sin(NA·π) | breathes 2.0–3.5, peak mid-life (shimmer) |
| PannerUV_Detail | NA·0.25 | detail scroll |
| PannerUV_Crystalline | NA·0.15 | base scroll (slower) |
| CylindricalDepth | sin(NA·2π + ParticleID·0.5)·0.3 | per-particle subsurface phase |

**Texture inputs**
- T_FrostNoise_Detail — *procedural* fBm Perlin/Simplex 3 oct, 512² R, scale 0.15–0.25, seamless, sRGB off
- T_IceNoise_Crystalline — *procedural* Voronoi fractured 2 oct, 512² R, scale 0.08–0.12, seamless, angular shard look
- T_FrostGradient_LUT — *codex*: "1D gradient 512×8, white #FFFFFF → pale cyan #D5EFFF → cold blue #87CEEB, quadratic smooth, frost color ramp"
- T_SubsurfaceDepth — *procedural* 256² seamless Perlin fBm 2 oct, scale 0.1, inverted falloff for rim subsurface

**Build steps**
1. Generate T_FrostNoise_Detail (512² fBm 3 oct), T_IceNoise_Crystalline (512² Voronoi 2 oct, angular), codex T_FrostGradient_LUT.
2. Create SM_IceShard_01 (40–60 tri needle/shard, center pivot, double-sided).
3. Create `M_StylizedFrost_Master` (Translucent/Unlit/two-sided, RI 1.33); build Panner2Layer dual → Dissolve(FrostSpread 0.2–0.8) → FresnelRim(exp 2–3.5) → Multiply(pattern, rim) → cyan tint → IceOpacity fade; emissive ×1.8.
4. Expose FrostSpread, IceOpacity, FresnelPower, PannerDetail, PannerCrystalline, CylindricalDepth (MPC-bindable).
5. Create `NS_StylizedFrost_Shards` (bounds 300×300×400 auto-expand); **NE_FrostShards**: spawn 35/s, life 3.5–5.0, size 0.8–1.8, vel (0,0,0.4) + RandomCone 20° 100–150.
6. Add Init.Rotation (random 0–360°), Forces (grav −0.08 Z, damp 0.92), Scale (1.0→1.3 ease-out hold), ColorOverLife (white→pale-cyan), DynamicMaterialParameter (4 params w/ curves).
7. Mesh renderer Translucent, MI frost, camera-facing, sort ViewDepth.
8. Create curves Curve_FrostSpread (flat 0 @0–0.15, rapid 0→1 @0.15–0.6, flat 1 after), Curve_IceOpacity (flat 1 @0–0.7, 1→0 @0.7–1), Curve_FresnelPower (sine 2.0–3.5).
9. Make `MI_StylizedFrost_Master` (FrostSpread 0.5, IceOpacity 1.0, FresnelPower 2.5). Compile + test: upward drift + cone spread, 0.15 s freeze hold, rapid frost @0.6 s snap, cyan shimmer + rim, fade final 30%. GPU < 1.2 ms.
10. Tune: grav/damp for motion feel, fresnel peak timing, FrostSpread threshold 0.2–0.3, cyan tint saturation.

---

### 10. Wind / Dust Gust — directional curl-noise streaks, soft low-opacity, swirl via polar UV, debris specks

**Niagara stack** — `NS_WindDustGust / NE_WindDustGust`, GPU, duration-bound burst (not infinite)
- Spawn.Basic: rate **80/s** (duration 2.5 s gust); Init.Life **3.5–5.0 s** (range 1.2)
- Init.Velocity: **(250,0,50) m/s** base (forward-up) + RandomCone **25°** spread
- Init.Position: offset +X 50 UU (ahead of source); Init.Size **30–50 UU**
- Init.Rotation: random 0–360°, RotationRate 180°/s
- Update.Position: advection on, inherit velocity; Update.Forces: gravity **(0,0,-25)**, damping **0.65** (turbulent drag)
- Update.Scale: **1.0 → 1.3** (expansion)
- Update.DynamicMaterialParameter: bind `NormalizedAge`, `PanSpeed`, `CurlStrength`, `OpacityFade`, `DirectionalFlow`
- Sprite renderer: Translucent, **soft-particle fade 80 UU**, **velocity-aligned** (stretch along motion), size 30–50 cm, `M_WindDust_Master`, bounds ±300

**Material graph** — `M_WindDust_Master`, **Translucent**, Unlit, single-sided
1. `MF_Panner2Layer`(T_CloudNoise_Curl @ 0.4, T_DetailNoise_Fine @ 0.8, dir (1,0.2)) → CombinedNoise (~0.15–0.4)
2. `MF_PolarUV`(center (0.5,0.5), angle Time·`CurlStrength`·0.5) → PolarCoords (swirl)
3. Remap(CombinedNoise via PolarCoords, −0.5..0.5) → DistortAmount
4. Sample T_CloudNoise_Curl @ (BaseUV + DistortAmount·0.08·NA) → FinalNoiseMask (~0.1–0.5)
5. Multiply(FinalNoiseMask, `DirectionalFlow`) → FinalAlpha
6. Lerp(CloudColor (0.75,0.72,0.7), DustColor (0.65,0.58,0.5), saturate(NA−0.7)) → ColorShift (fade to dust @70%)
7. FinalColor = ColorShift × FinalNoiseMask × **1.2** (subtle emissive ~0.3); BaseColor = white; Normal = (0,0,1)
8. Opacity = FinalAlpha × OpacityFade × VertexColor.A (`MF_SoftParticle` 80 UU edge fade)

**Dynamic params + curves**
| Param | Curve (NA → value) | Use |
|---|---|---|
| NormalizedAge | linear 0→1 | color shift @70%, distortion peak @40%, opacity decay |
| PanSpeed | const 0.4 | base layer scroll (fast early turbulence) |
| CurlStrength | 0→0.35, 0.2→0.6, 1→0.1 | peak swirl mid-life, settle |
| DirectionalFlow | const 1.0 (forward-X) | MPC-overridable wind modifier |
| OpacityFade | 0→0, 0.25→0.65, 0.6→0.6, 1→0 | peak early, gentle fade |
| DetailPanSpeed | const 0.8 | fine noise scroll |
| DistortionStrength | 0→0.05, 0.4→0.12, 1→0.02 | turbulent peak, smooth end |

**Texture inputs**
- T_CloudNoise_Curl — *procedural* Perlin/fBm 512² seamless, 4 oct, lacunarity 1.8, scale 0.08–0.15, R 0–1, sRGB off (curl distortion field)
- T_DetailNoise_Fine — *procedural* Voronoi + Perlin hybrid 512² seamless, 5 oct, scale 0.15–0.3, sRGB off (fine dust grain)
- T_DirectionalMask — *procedural* 1D linear gradient white→black (0.5→1.0), 256×16, sRGB off (directional fade along X)
- T_DriftPattern — *codex*: "256² seamless Perlin flow-field (curl streamlines), grayscale linear, 3 oct, directional L-R sweep" (fallback: T_CloudNoise_Curl rotated 45°)

**Build steps**
1. Create `NS_WindDustGust`; **NE_WindDustGust** (GPU, bounds 300³); spawn 80/s, duration 2.5 s.
2. Add Init.Life 3.5–5.0, Init.Velocity (250,0,50) + RandomCone 25°, Init.Size 30–50, Init.Rotation 0–360°.
3. Update.Position advection; Forces grav −25 Z / damping 0.65; ScaleOverLife 1.0→1.3.
4. DynamicMaterialParameter bindings: NormalizedAge (Particle.NormalizedAge), OpacityFade (curve 0→0.65@25%→0.6@60%→0), CurlStrength (0.35→0.6@20%→0.1), DirectionalFlow (1.0 / MPC).
5. Sprite renderer: M_WindDust_Master, Translucent, soft-particle fade 80, velocity-aligned, auto-expand bounds.
6. Create `M_WindDust_Master` (Translucent/Unlit): Panner2Layer (Curl 0.4, Fine 0.8, multiply) → PolarUV swirl → distort (offset·0.08·NA) → color lerp (cloud→dust @70%) × 1.2 → DirectionalFlow mask → SoftParticle → opacity (× Particle.Alpha).
7. Expose T_CloudNoise_Curl, T_DetailNoise_Fine, PanSpeed, CurlStrength, DirectionalFlow, DistortionStrength, EmissiveIntensity; make `MI_WindDust_Default` (Emissive 0.3, Distortion 0.05, CurlStrength 0.6).
8. Import textures srgb=false, compression Mask; compile material + system (warmup 0).
9. Test: forward-X motion w/ 25° spread, soft cloud + fine specks, opacity peak @25% → fade to 0, curl swirl @20–60%, directional gust (not radial). GPU < 0.3 ms @ 100 particles.

---

### 11. Stylized Poison / Acid Cloud — bubbling translucent + dripping sprites, dissolve pop, green-purple gradient

**Niagara stack** — `NS_PoisonAcidCloud`, GPU, infinite loop, warmup 30 frames, bounds 300³ auto-expand
- **NE_PoisonBubbleCloud** (sprite, loop): spawn **60/s**; life **2.5–4.0 s**
- Init.Velocity linear **(0,0,0.4) m/s** up; Init.Position (0,0,0); Init.Scale 1.0; Init.Color (0.2,0.8,0.3,0.6)
- Update.Forces: gravity **(0,0,-0.08)**, damping **0.92**; advection on
- Update.Velocity: curl noise scale **0.3**, freq **1.2 Hz** (boil turbulence)
- Update.Scale: **1.0 → 1.4** (swell); Update.Color: green → purple via ColorOverLife
- Update.DynamicMaterialParameter: bind `NormalizedAge`, `BubbleDissolveAlpha`, `PoisonGradientT`, `BubbleGlowIntensity`
- Sprite renderer: camera-facing (Screen), size **0.8–1.8 cm**, `M_PoisonBubble_Master`, **Additive**
- **NE_PoisonDripSprite** (sprite, loop): spawn **20/s**; life **1.8–2.5 s**
- Init.Velocity RandomCone **25° Z-down**, speed **80–150 cm/s**; Init.Position jitter ±15 cm XY; scale 0.8–1.2; color (0.3,0.6,0.2,0.7)
- Update.Forces: gravity **(0,0,-0.35)**, damping **0.85**; Update.Scale: **0.8 → 0.3** (shrink)
- Update.DynamicMaterialParameter: bind `NormalizedAge`, `DripStretch`, `CorrosionAmount`, `DripGlowIntensity`
- Sprite renderer: **velocity-aligned**, size 0.4–0.8 cm, `M_PoisonDrip_Stretch`, Translucent, soft particles 50 UU

**Material graph** — three materials
`M_PoisonBubble_Master`, **Additive**, Unlit, two-sided
1. `MF_Panner2Layer`(T_PoisonNoise_Base @ 0.25, T_PoisonNoise_Detail @ 0.55, dir (0.3,0.7)) → CombinedNoise (~0.08–0.28)
2. `MF_GradientMap`(T_PoisonGradient_LUT, T=`PoisonGradientT`) → poison color (green #3DBB00 → purple #8B00FF → toxic yellow #CCFF00)
3. Multiply(CombinedNoise, PoisonColor) → Colored Poison
4. Lerp(ColoredPoison, BubbleHighlight (1,1,0.6), saturate(0.5−NA·2)) → rim glow on fresh bubbles
5. `MF_Dissolve`(mask=CombinedNoise+Detail, alpha=`BubbleDissolveAlpha`) → pop-bubble erosion
6. × **3.0** → Emissive; Opacity from BubbleDissolveAlpha; Normal = (0,0,1)

`M_PoisonDrip_Stretch`, **Translucent**, Unlit, velocity-aligned
1. T_PoisonDrip_Base @ static UV × VertexColor.RGB → drip tint
2. × `DripStretch` curve (1.0→1.8→0.9, squash then relax) → width modulation
3. × VertexColor.A → opacity; `MF_SoftParticle` 50 UU edge fade
4. Emissive = drip × GlowIntensity (1.2); BaseColor = black; Normal = (0,0,1)

`M_CorrosionDecal` (optional), **Translucent**, Masked, two-sided
1. T_CorrosionMask sampled from WorldPosition offset; erosion = T_CorrosionNoise via TimeOffset
2. AlphaMask = `CorrosionAmount` curve over time
3. BaseColor = green-brown (0.3,0.4,0.1) × AlphaMask; Normal = T_CorrosionNormal (subtle bump)

**Dynamic params + curves**
| Param | Formula/Curve | Note |
|---|---|---|
| NormalizedAge | Particle.NormalizedAge | linear 0→1 |
| PoisonGradientT | saturate(clamp(NA,0,0.75)) | 0→1 over 0–75%, flat after (LUT) |
| BubbleDissolveAlpha | saturate(1−(NA−0.5)·2.8) | 1→0 threshold @50% (sharp pop) |
| DripStretch | cubic [0,1.0]→[0.3,1.8]→[0.8,0.9] | stretch mid-flight, recover |
| CorrosionAmount | saturate((NA−0.4)·1.5) | 0→1 over 40–100% (delayed onset) |
| BubbleSizeScale | 1.0→1.4 linear | swell |
| DripSizeScale | 0.8→0.3 linear | shrink |
| BubbleGlowIntensity | sin(NA·π)·2.0 | breathing glow peak mid-life |
| DripGlowIntensity | saturate(1−(NA−0.3)·2.0) | 1→0 after 30% |

**Texture inputs**
- T_PoisonNoise_Base — *procedural* Perlin/Simplex seamless, 2K R8, scale 0.05–0.12, 2 oct, sRGB off
- T_PoisonNoise_Detail — *procedural* fBm Perlin, 2K R8, scale 0.15–0.3, 4 oct, lacunarity 2.1, sRGB off
- T_PoisonGradient_LUT — *codex*: "1D gradient 512×8, sickly green #3DBB00 → purple #8B00FF → toxic yellow-green #CCFF00, smooth easing, additive poison ramp"
- T_PoisonDrip_Base — *codex*: "256×128 vertical tear/drop sprite, white center fading to transparent, turbulent edges, clamp, velocity-aligned"
- T_CorrosionMask — *procedural* Voronoi/Worley 512² seamless, R8, organic acid-erosion blobs, sRGB off
- T_CorrosionNoise — *procedural* Simplex 3 oct, 256², R8 for time-driven erosion
- T_CorrosionNormal — *codex*: "256² normal map of corroded/pitted surface, sRGB off, subtle decal bump"

**Build steps**
1. Create `M_PoisonBubble_Master` (Additive/Unlit/two-sided, use-with-Niagara): Panner2Layer (Base 0.25 + Detail 0.55 multiply) → GradientMap(PoisonGradientT) → Dissolve(BubbleDissolveAlpha) → rim highlight Lerp(poison, yellow) saturate(0.5−NA·2) → emissive ×3.0. Expose PoisonGradientT, BubbleDissolveAlpha, BubbleSizeScale, BubbleGlowIntensity, PannerUV_Base/Detail.
2. Create `M_PoisonDrip_Stretch` (Translucent/Unlit): T_PoisonDrip_Base × VertexColor.RGB, DripStretch width modulation, × VertexColor.A, SoftParticle 50 UU, emissive ×1.2. Expose DripStretch, CorrosionAmount.
3. Optional `M_CorrosionDecal` (Translucent/Masked): T_CorrosionMask + T_CorrosionNoise (time-offset), CorrosionAmount → opacity, green-brown tint, T_CorrosionNormal slot.
4. Create `NS_PoisonAcidCloud` (infinite loop, 30 warmup, bounds 300³ auto-expand).
5. **NE_PoisonBubbleCloud** (GPU sprite): spawn 60/s, life 2.5–4.0, vel (0,0,0.4), color (0.2,0.8,0.3,0.6); forces grav −0.08 / damp 0.92; curl noise (0.3, 1.2 Hz); scale 1.0→1.4; ColorOverLife green→purple→yellow; bind 4 dyn params; sprite camera-facing, MI bubble, Additive.
6. **NE_PoisonDripSprite** (GPU sprite): spawn 20/s, life 1.8–2.5, vel RandomCone 25° Z-down 80–150, pos jitter ±15 cm, scale 0.8–1.2; forces grav −0.35 / damp 0.85; scale 0.8→0.3; bind 4 dyn params; sprite velocity-aligned, MI drip, Translucent, soft particles 50 UU.
7. Add both emitters; compile system. Create curves: Curve_BubbleSizeOverLife (1.0→1.4), Curve_DripSizeOverLife (0.8→0.3), Curve_BubbleColorOverLife (green→purple→yellow), Curve_DissolveThreshold (1→0 cubic from 50%), Curve_DripStretch (0→1.0, 0.3→1.8@40%, 0.8→0.9@100%).
8. Make MIs: `MI_PoisonBubble_Master` (Glow 2.0, Emissive 3.0), `MI_PoisonDrip_Stretch` (Corrosion 0.0, Glow 1.2).
9. Generate textures (noise procedural BC4, gradient/drip/normal codex uncompressed LUT). Test in level: bubbles swell+glow then pop (dissolve), drips stretch mid-fall then shrink, green→purple shift; tune PoisonGradientT + BubbleDissolveAlpha for pop timing, curl freq for boil. GPU ~0.4 ms @ 60+20/s.
10. Optional `BP_PoisonAcidSource` (NiagaraComponent + M_CorrosionDecal child) for ground corrosion on impact; NPC hurt material tint green (0.3,0.8,0.2) + chemical vignette in poison radius.

---

### 12. Holy / Light — additive light shafts + rising sparkle motes + radial halo + lens-flare accents, warm gold-white

**Niagara stack** — `NS_HolyLight_Heal`, GPU+CPU hybrid, bounds 400×400×600, warmup 30 frames, loop infinite
- **NE_LightShaft_Cones** (GPU sprite, loop): spawn **35/s**; life **3.0–4.5 s** (range 1.5); vel linear **(0,0,1.2) m/s** up; size **2.0–3.5 cm**
- Forces grav **(0,0,-0.08)**, damping 0.95; advection on; Scale **1.0 → 0.5** cubic ease-in; ColorOverLife gold→white→pale gold
- DynamicMaterialParameter: `NormalizedAge`, `GradientT`, `PulseIntensity`, `FresnelExp`, `PannerUV`; renderer camera-facing, `M_HolyLight_Shaft`, sort ViewDepth
- **NE_SparkleAura** (GPU sprite, loop): spawn **80/s**; life **1.8–2.8 s** (range 1.0); vel RandomCone **25° Z** 40–120 cm/s orbital; pos random sphere r 5–15 cm; size 0.3–0.8 cm; rotation 0–360°
- Forces grav (0,0,-0.3), drag 0.6, turbulence (curl 0.8 Hz, scale 0.4); Scale 1.0→0.2; ColorOverLife white→gold→pale gold
- DynamicMaterialParameter: `NormalizedAge`, `SparkleGlowAlpha`, `SparkleColor`; renderer camera-facing, `M_HolyLight_Sparkle`, soft particles 20 UU
- **NE_HaloMesh** (CPU mesh, single loop instance): spawn 1.0; life **1000 s** (persistent); pos (0,0,50 cm); scale oscillate **1.0→1.3→1.0** (4.0 s period)
- DynamicMaterialParameter: `NormalizedAge` (mod 4.0), `PulseAlpha`, `HaloGradientT`, `EmissiveIntensity`; mesh SM_Disc_Flat, `M_HolyLight_Halo`, Additive, bounds 500×500×100
- **NE_LensFlare_Accent** (GPU sprite, burst+loop): spawn burst **15** + rate **5/s**; life **1.2–2.0 s**; vel RandomCone **40° Z** 80–200 cm/s; pos (0,0,20 cm); size 1.0–2.5 cm; color white
- Forces grav (0,0,-0.2), damping 0.92; Scale 1.0→0.3 exp ease-out; DynamicMaterialParameter: `NormalizedAge`, `LensFlareAlpha`, `FresnelFade`; renderer camera-facing, `M_HolyLight_LensFlare`

**Material graph** — four materials, all **Additive**, Unlit, two-sided
`M_HolyLight_Shaft` (Surface)
1. `MF_Panner2Layer`(T_GodRayNoise_01 @ 0.2, T_GodRayNoise_02 @ 0.4, dir (0,1), time NA·4) → CombinedNoise (~0.1–0.3)
2. `MF_GradientMap`(T_HolyGradient_LUT, `GradientT`) → gold #D4AF37 → white → pale gold #F0E68C
3. Multiply(CombinedNoise, GradientColor) → Colored shaft; `MF_FresnelRim`(exp `FresnelExp` 2.5, bias 0.1) → Rim; multiply → rim-lit
4. Lerp(ColoredLight, ×0.6, saturate(NA−0.7)) → dim at end; `MF_Dissolve`(mask=CombinedNoise, alpha=saturate(1−(NA−0.5)·1.8)) → soft edge
5. Final = (Colored·Rim)·(1.0 + `PulseIntensity`·sin(NA·π·2)) × **1.8** → Emissive; Opacity = CombinedNoise·DissolveAlpha·ParticleColor.A; BaseColor=black

`M_HolyLight_Sparkle`
1. T_Sparkle_Round (screen-aligned UV) → shape; `MF_GradientMap`(T_SparkleGradient_LUT, `SparkleGlowAlpha`) → white→pale gold
2. Fresnel(exp 2.0, bias 0.2) → edge glow; Multiply(tex, gradient, fresnel) × sin(NA·π) × ParticleColor.RGB × **2.0** → Emissive
3. Opacity = tex.A × ParticleColor.A × sin(NA·π); Normal = (0,0,1)

`M_HolyLight_Halo`
1. RadialDist = length(TexCoord0−0.5)·2; Power(RadialDist, 1.5) → soft edge
2. `MF_GradientMap`(T_RadialGradient_White, `HaloGradientT`) → white center → pale gold → transparent edge
3. `MF_Panner2Layer`(T_HaloDetail_Subtle @ 0.1, rotating angle `PannerUV`) → shimmer; Lerp(radial, radial×shimmer, 0.4)
4. × (1.0 + 0.4·sin(HaloGradientT·π·2)) × `EmissiveIntensity` (2.0–3.0) → Emissive; Opacity = (radial×shimmer)·saturate(1−RadialDist·0.8); Normal=(0,0,1)

`M_HolyLight_LensFlare`
1. T_LensFlare_Starburst × ParticleColor.RGB → flare; Fresnel(exp 3.0) → rim
2. × sin(NA·π·2) (double pulse) × (1.0 + 0.3·cos(NA·π·4)) (micro-shimmer); `MF_GradientMap`(T_LensFlareGrad_White, `LensFlareAlpha`) → brighten over start
3. Lerp(tex, ×0.4, saturate((NA−0.6)·2)) → fade tail; Final × **2.5** → Emissive; Opacity = tex.A × `FresnelFade` × sin(NA·π·2) × ParticleColor.A

**Dynamic params + curves**
| Param | Curve/Formula | Emitter |
|---|---|---|
| NormalizedAge | linear 0→1 | all |
| GradientT | 0→1 over 0–0.75 NA, flat after | Shaft |
| PulseIntensity | sin(NA·π·2)·0.8 (0→0, 0.25→0.6, 0.5→1.0, 0.75→0.5, 1→0) | Shaft |
| FresnelExp | const 2.5 | Shaft |
| PannerUV | linear 0→2.0 | Shaft (god-ray scroll) |
| SparkleGlowAlpha | 0→0.2, 0.3→1.0, 0.7→0.8, 1→0 (cubic) | Sparkle |
| SparkleColor | white @0 → warm gold (1,0.9,0.6) @0.5 → pale gold (0.95,0.85,0.5) @1 | Sparkle |
| PulseAlpha | 1.0 + 0.3·sin(NA·π·2) | Halo |
| HaloGradientT | frac(NA·0.5) (slow 8 s loop) | Halo |
| EmissiveIntensity | 0→2.5, 0.5→3.0, 1→2.5 | Halo |
| LensFlareAlpha | 0→0.1, 0.2→1.0, 0.8→0.4, 1→0 | LensFlare |
| FresnelFade | 0→1.0, 0.6→0.8, 1→0 (cubic ease-out) | LensFlare |

**Texture inputs**
- T_GodRayNoise_01 — *procedural* Perlin/Simplex seamless 512² R, scale 0.08–0.15, 2 oct, lacunarity 2.0 (base gradient scroll)
- T_GodRayNoise_02 — *procedural* fBm Simplex seamless 512² R, scale 0.15–0.3, 4 oct (shaft detail variation)
- T_HolyGradient_LUT — *codex*: "1D gradient 512×8, gold #D4AF37 → white #FFFFFF → pale gold #F0E68C → white, cubic smooth, sRGB, holy color ramp"
- T_Sparkle_Round — *procedural*/*codex* 128² radial gaussian white→transparent, clamp, BC4 (sparkle mote)
- T_SparkleGradient_LUT — *codex*: "1D gradient 256×4, white #FFFFFF → pale gold #F0E68C → transparent, smooth, sRGB off"
- T_RadialGradient_White — *procedural* 256² radial white center → black edge, linear, clamp, grayscale (halo fade)
- T_HaloDetail_Subtle — *procedural* Voronoi/Perlin seamless 256² R, low-freq (scale 0.5), ~0.5 Hz curl (halo shimmer)
- T_LensFlare_Starburst — *codex*: "512² starburst lens flare, 8-point symmetry, white center → transparent rays, clamp"
- T_LensFlareGrad_White — *procedural*/*codex* 1D 256×1, black → white → semi-transparent, smooth ramp

**Build steps**
1. Generate textures: T_GodRayNoise_01/02 (512² Perlin/fBm seamless), T_HolyGradient_LUT (codex, sRGB), T_Sparkle_Round (128² gaussian), T_SparkleGradient_LUT (256×4), T_RadialGradient_White (256² linear), T_HaloDetail_Subtle (256² Voronoi), T_LensFlare_Starburst (codex 512²), T_LensFlareGrad_White (256×1).
2. Create `M_HolyLight_Shaft` (Additive/Unlit/two-sided/Surface): Panner2Layer dual → GradientMap → Fresnel multiply → Dissolve → pulse sin → ×1.8 emissive; expose NormalizedAge, GradientT, PulseIntensity, FresnelExp, PannerUV. Make `MI_HolyLight_Shaft` (FresnelExp 2.8, Pulse 0.7, Emissive 1.9).
3. Create `M_HolyLight_Sparkle`: round tex → GradientMap → Fresnel → sin(NA·π) fade → ×2.0 emissive; expose NormalizedAge, SparkleGlowAlpha. Make MI.
4. Create `M_HolyLight_Halo`: radial-dist power(1.5) → GradientMap → Panner2Layer detail → pulse → ×EmissiveIntensity; expose NormalizedAge, HaloGradientT, EmissiveIntensity. Make `MI_HolyLight_Halo` (Emissive 2.8, detail blend 0.4).
5. Create `M_HolyLight_LensFlare`: starburst × Fresnel × sin(NA·π·2) × cos(NA·π·4) → GradientMap → fade-tail lerp → ×2.5 emissive; expose NormalizedAge, LensFlareAlpha, FresnelFade. Make MI (Emissive 2.5, FresnelExp 3.0).
6. `create_system('NS_HolyLight_Heal', '/Game/Effects/Healing/')`. **NE_LightShaft_Cones** (GPU): spawn 35/s, life 3.0–4.5, vel (0,0,120 cm/s), forces grav −0.08 / damp 0.95, advection, scale 1.0→0.5 cubic ease-in, ColorFromCurve gold→white→pale gold, bind 5 dyn params, sprite camera-facing, MI shaft, Additive, bounds 400×400×600.
7. **NE_SparkleAura** (GPU): spawn 80/s, life 1.8–2.8, vel RandomCone 25° 40–120, pos sphere 5–15 cm, size 0.3–0.8, forces grav −0.3 / drag 0.6 + curl 0.8 Hz scale 0.4, scale 1.0→0.2, ColorFromCurve white→gold→pale gold, bind NormalizedAge + SparkleGlowAlpha, sprite camera-facing, soft particles 20 UU, MI sparkle, bounds 250×250×300.
8. **NE_HaloMesh** (CPU mesh): spawn 1.0 single loop, life 1000 s, scale oscillate 1.0→1.3 sine 4.0 s period, bind NormalizedAge + PulseAlpha + HaloGradientT + EmissiveIntensity, mesh SM_Disc_Flat, MI halo, Additive, bounds 500×500×100.
9. **NE_LensFlare_Accent** (GPU): spawn burst 15 + rate 5/s, life 1.2–2.0, vel RandomCone 40° 80–200, pos (0,0,20 cm), size 1.0–2.5, forces grav −0.2 / damp 0.92, scale 1.0→0.3 exp ease-out, bind NormalizedAge + LensFlareAlpha + FresnelFade, sprite camera-facing, MI lens flare, Additive, bounds 300×300×400.
10. Add all 4 emitters; system bounds auto-expand, warmup 30, loop infinite; compile. `BP_HolyLight_Heal` (NiagaraComponent, auto-activate). Test: shafts rise gold→white→fade, sparkles orbital drift + soft particles, halo pulse (4 s) + radial shimmer, lens-flare burst + gentle twinkle double-pulse. Overall warm golden-white aura, no flicker.
11. Optimize: profile in PIE, GPU < 1.2 ms (4 emitters combined); reduce spawn rates if needed (Shaft 35→25, Sparkle 80→60). Bake to `/Game/Effects/Healing/`. Create `MPC_HolyLightParams` (EmissiveScale 2.5, PulseSpeed 1.0, HolyTint (1,0.9,0.7)). Optional `BP_HolyLight_Spawner` (auto-spawn on heal cast, duration despawn).

---

### Cross-effect quick reference (continued)

| Effect | Sim | Renderer | Blend | Spawn | Life | Key MFs |
|---|---|---|---|---|---|---|
| Water Splash | GPU | Mesh + Sprite ×2 | Translucent | 1 + 80 + 120 burst | 0.6 / 1.5–2.2 / 0.8–1.2 s | Panner2Layer, PolarUV, FresnelRim, Dissolve |
| Electric Arc | GPU | Ribbon (+sub) | Additive | 1 burst | 0.15–0.25 s | Panner2Layer, FresnelRim, GradientMap(1D), Dissolve |
| Ice / Frost | GPU/CPU | Mesh | Translucent | 35/s loop | 3.5–5 s | Panner2Layer, Dissolve, FresnelRim, CylindricalUV |
| Wind / Dust | GPU | Sprite (vel) | Translucent | 80/s (2.5 s burst) | 3.5–5 s | Panner2Layer, PolarUV, SoftParticle |
| Poison / Acid | GPU | Sprite ×2 (+decal) | Additive + Translucent | 60 + 20/s loop | 2.5–4 / 1.8–2.5 s | Panner2Layer, GradientMap, Dissolve, SoftParticle |
| Holy / Light | GPU+CPU | Sprite ×3 + Mesh | Additive | 35 + 80 + 1 + (15 burst+5/s) | 3–4.5 / 1.8–2.8 / ∞ / 1.2–2 s | Panner2Layer, GradientMap, FresnelRim, Dissolve |
