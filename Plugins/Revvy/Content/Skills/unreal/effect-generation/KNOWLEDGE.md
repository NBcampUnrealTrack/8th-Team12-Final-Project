# 레비 VFX Effect-Generation Knowledge Base (UE5.7 Niagara + Materials)

> ⭐ **품질 우선순위**: 이펙트 품질은 텍스처/플립북이 아니라 **Niagara 오케스트레이션 + 머티리얼 연출(모션)** 에서 나온다. 먼저 Niagara 구조와 머티리얼 모션(panner/극좌표/distortion/dissolve/dynamic param)을 설계하고, 텍스처는 그 모션이 소비할 단순·재사용 입력으로만 만든다. 플립북은 틈새 폴백.

> 📂 **연계 문서**: 효과별 빌드 레시피 `RECIPES.md`, 재사용 머티리얼 함수 `MATERIAL_FUNCTIONS.md`, codex 텍스처 프롬프트 `CODEX_PROMPTS.md`, 무드 톤(Horror/Action) `STYLE_GUIDE.md`, 렌더 아트스타일(실사/툰/스타일라이즈드/애니/픽셀) `STYLES.md`, 다단계 시퀀싱 `SEQUENCING.md` 참고. (톤 축과 스타일 축은 직교 — 둘 다 적용)

## Material & Niagara-Driven 연출 (PRIMARY quality lever — prefer over flipbooks)

> **Priority**: 모든 이펙트의 움직임/생동감은 Niagara 구조 + Material 노드 모션으로 만든다. Flipbook은 기본값이 아니라 마지막 수단이다.

Quality comes from how the material moves UVs/parameters over particle life and how Niagara orchestrates emitters — NOT from baked frame sheets. One master material + dynamic parameters drives hundreds of particles at 5-10x cheaper GPU cost and 75-90% less VRAM than equivalent flipbooks.

### 1. UV Motion in Material (continuous, seam-free)
- **Panner** (`TexCoord` → Panner): stack 2+ layers at different speeds for turbulence. Fire 0.1-0.25 U/V; smoke tail 0.05-0.1; base noise 0.05-0.1, detail noise 0.1-0.25. Drive Time from `Particle.Age` for per-particle sync.
- **Rotator / CustomRotator**: Rotation Angle 0-1 = 0-360°. Drive from `Particle.Rotation` at 0.05-0.3 cycles/frame for swirls/vortices.
- **Polar UV** (atan2 swirls): `Normalize(0.5 - TexCoord)` → `Append(Arctangent2(Y,X), Length(input))` → multiply length/angle by `Time * RotSpeed (0.5)` → feed as CustomUV. Turns linear tiling into concentric rings & spirals.
- **RadialGradientExponential** + `Frac` + `Time*0.3`: expanding/contracting ring & halo masks; Ring Frequency 3-5.
- **Flowmap distortion**: `(FlowmapSample*2-1) * Strength(0.05-0.15)` added to UVs before sampling detail noise; pan the flowmap itself slowly (~0.02) so distortion never looks static. Loop by adding panned Time offset.
- **Sine UV distortion**: `Sin(Time*Freq) * Amplitude(0.05)` added to U/V. Freq 1-4 Hz for breathing auras/pulses.

### 2. Dynamic Material Parameters over Particle Life (the core 연출 channel)
- **Niagara: Set Dynamic Material Parameter** module — 4 slots × R/G/B/A = up to 16 floats/particle. Drive via **Float From Curve** bound to `Particles.NormalizedAge`, `Particles.Age`, `Particles.Velocity`, `Particles.Scale`.
- **Material: DynamicParameter** node reads them back. Convention: R=dissolve threshold (0-1), G=emissive intensity (0-2), B=distortion strength (0-1), A=edge softness (0-0.5).
- Curves decouple frame rate from art intent: EaseInOut for lifetime drift, EaseOut for quick fade, exponential decay for emissive. Emissive ramp: 0 at spawn → peak 1.5-2.0 at NormalizedAge 0.4-0.6 → 0 at death.
- Per-particle randomization (e.g. distortion `(1 - NormalizedAge) * RandomFloat(0.5,1.0)`) kills the "same animation every time" look with a single material.

### 3. Dissolve / Erosion (replaces flipbook burn sheets)
- Sample noise/erosion mask → `Mask*2 - DynamicParameter.R` → **Smoothstep(-0.3, 0.3)** → Opacity Mask. One material covers sharp/soft/colored burns by tuning threshold curve.
- Progressive reveal: `TexCoord.y + RelativeTime*ScrollSpeed(0.5-3.0)` into erosion mask with contrast control. Erosion exponent 1.0-3.0.
- Age-based color shift via `Lerp(SpawnColor, DeathColor, NormalizedAge)`.

### 4. Gradient Mapping (LUT) — color iteration without recoloring textures
- Grayscale source (**Linear**, not sRGB) → `Append(value, 0)` → sample 256×1 gradient (sRGB) → output color. Swap one gradient to re-skin all particles. Pack 3 B&W sources into RGB (Masked compression) for ~2/3 memory savings; panning/flowmap/erosion automatically flow through the LUT.

### 5. Fresnel, Depth Fade, Dither (cheap quality reads)
- **Fresnel**: ExponentIn 3-12 (higher = edge-pushed), BaseReflectFraction 0.0-0.5. `Lerp(base, rimColor, Fresnel)`; modulate by DynamicParameter for per-particle rim. Energy shields, glass rims.
- **Depth Fade / soft particles**: `Saturate((SceneDepth - PixelDepth) / FadeDistance)` into Opacity; fade 2-50 cm. Removes hard mesh intersections.
- **Dither dissolve**: 4×4 (16 lvl) / 8×8 (64 lvl) Bayer matrix + age threshold + `DitherTemporalAA` to avoid banding in HDR.

### 6. Emissive / HDR for bloom
- Separate emissive from base color; multiplier 1.5-8.0 drives post bloom (HDR RT allows >1). Drive intensity from Color-Over-Life curve. Additive for fire/energy (blacks transparent); Translucent for graded opacity & reflections.

### 7. Mesh & Ribbon Renderers (beat sprites for shockwaves/trails)
- **Mesh Renderer**: thin ring/disc mesh + scrolling-UV material + Fresnel + dissolve = shockwave in one draw call/instance. Opacity = `(1 - NormalizedAge) * Fresnel`, panner speed ~2.0. View-dependent depth/parallax that flipbooks can't express.
- **Ribbon Renderer**: connect by Ribbon ID; color/alpha from `Particle.NormalizedAge`; CustomUV panned 0.1-0.5 along ribbon. Bind width to `Particles.Scale`, twist to `Particles.VelocityZ*0.5`. Solves sprite facing for trails/streams.

### 8. GPU Curl Noise (organic motion, no animation data)
- **Niagara: Curl Noise Force** module: Strength 0.5-2.0, Frequency 1.0-5.0. Per-particle trajectory perturbation that reads as "alive". Run on GPU sim to scale to 1000s of particles. Optional 32³/64³ vector field for large-scale advection.
- Sub-emitter/event chains: **Sample Particles From Other Emitter** + **Receive Location Event** to spawn followers/trails at parent position/velocity.

### Niagara structure quick reference
- Spawn: init DynamicMaterialParameter = (0,0,0,0).
- Update: Set Dynamic Material Parameter (dissolve=NormalizedAge, emissive=Float From Curve, distortion=(1-Age)*rand, edge=const).
- Prefer **GPU sim** for high counts; Mesh/Ribbon renderer over Sprite when facing/volume/trails matter.

## Minimal Reusable Texture Library

**Principle**: Material nodes make the MOTION; textures are just simple static inputs. A handful of small, tileable masks/gradients (each typically 256-512px, BC4/BC5 compressed) powers most effects via panning/distortion/LUT. Procedural-first; only ask codex for the simple isolated mask/gradient/sprite that a material node can't synthesize cheaply.

- **Seamless Perlin/Simplex noise** (512² or 1024², R8/BC4) — base smoke/turbulence layer, reused everywhere. `[procedural]` (UE5 Noise node / GPU noise; prefer over baked to save memory)
- **Detail/cloud noise** (256-512², R8/BC4) — high-freq secondary distortion layer panned faster than base. `[procedural]`
- **Curl / flowmap** (512², RG8/BC5, R/G = normalized [-1,1] flow) — UV distortion for fire/water/energy, reusable across 50+ effects. `[procedural]` (Curl Noise / divergence-free noise; `[codex]` only if you need a hand-authored/sim-baked directional flow)
- **Radial gradient / soft circle** (256-512², single-channel white→transparent center) — burst cores, glow halos, ring masks; reusable across 100+ effects. `[procedural]` (RadialGradientExponential node — usually no texture needed)
- **Color ramp / gradient LUT** (256×1, sRGB) — grayscale→color remap & heat maps; one swap re-skins all particles. `[procedural]` (in-engine Color Curve / Lerp) or `[codex]` for a specific painted gradient flavor as a master template
- **Fire/heat gradient** (256², RGBA8, white→orange/yellow→transparent) — age-driven hot-to-cool color. `[codex]` (simple authored gradient if a 1D LUT isn't enough)
- **Erosion / crack mask** (512², high-contrast Voronoi/crack B&W) — dissolve & impact reveals. `[procedural]` (hash/Voronoi noise) or `[codex]` for a hand-sculpted crack network
- **Rim-light gradient/stripe** (256², R8) — Fresnel rim temporal variation. `[procedural]`
- **Low-freq normal map** (512², BC5) — subtle light/edge distortion, panned ~0.02. `[procedural]` (Perlin/Voronoi-derived) or `[codex]` for a specific detail normal
- **Spark/streak sheet** (512²-1024×512, thin directional streaks, transparent bg) — projectile trails, impact sparks. `[codex]` (hand-drawn/AI sprites — hard to synthesize procedurally)
- **Soft circle/dot sprite** (128-256², white on black) — particle cores, bloom sources. `[procedural]` (radial node) or tiny `[codex]` sprite
- *(engine-provided, no asset)* Scene Depth, Bayer/dither matrix — `[procedural]` (computed in shader)

**Division of labor**: codex = SIMPLE isolated inputs (masks, gradients, sprite sheets) only when not procedurally generable. The material/Niagara stack supplies all motion, looping, color iteration, and variation.

---

Purpose: numbers-first reference for producing shippable real-time VFX. Pairs AI-generated textures with Revvy API (`NiagaraService`, `NiagaraEmitterService`, `MaterialService`, `MaterialNodeService`, `AssetDiscoveryService.import_texture`).

---

## 0. Golden Numbers (memorize these)

| Knob | Shippable value |
|------|-----------------|
| Texture resolution | Power-of-two only: 512 / 1024 / 2048. Sprite 512–1024; hero flipbook atlas 2048 max |
| Flipbook grids | 2x2 (4f), 4x4 (16f), 6x6 (36f), 8x8 (64f). Frame count power-of-two. Tile ≥64px, 128px ideal |
| CPU vs GPU | <100 particles → CPU. >1000 → GPU. 100–1000 → benchmark, CPU usually wins |
| Emitter count | 3–4 per system; >10 emitters costs >1ms CPU just in overhead. 1×1000 beats 10×100 |
| Per-system CPU budget | <1ms CPU / system; main effects <2ms CPU, <5ms GPU per frame |
| Total particles in view | 50k–100k across all ambient VFX |
| Lifetimes | Fire 0.5–1.2s, Smoke 2–4s, Sparks 0.8–2s, Aura/Trails 1.5–3s. Avoid 10s+ |
| Spawn rates (pps) | Fire 80–120, Smoke 40–80, Sparks 150–300, Aura/Rings 30–60 |
| Emissive multiplier | 2.0–3.0 visible bloom; 8–30 bright fire/magic; only >1.0 triggers bloom |
| Bloom (PPV) | Threshold 3.0–4.0, Intensity 1.0–2.0 |
| LOD distances | LOD1 @2000cm (50% count), LOD2 @4000cm (25%); cull @5000–8000cm |
| Spawn spreading | Spread bursts over 2–3 frames to avoid hitches |

---

## 1. Texture Authoring Specs

**Color space (most common mistake):**
- Color textures (fire, smoke RGB) → **sRGB ON**.
- Masks / erosion / gradient / flowmap / data → **sRGB OFF (linear)**. Non-linear breaks threshold math and gradients. Set Sampler Type = `Masks`.

**Background & element:**
- Additive/screen blend → pure black `RGB(0,0,0)` background (black = transparent in additive). Any gray = halo.
- Single centered element, 60–80% of frame height (off-center "dances" across flipbook frames).
- No baked shadows / AO / bloom — pure diffuse; engine adds lighting & bloom.

**Channel packing (RGBA = 4 values / 1 sample):** R=primary, G=secondary, B=tertiary, A=alpha/erosion. Put high-contrast masks in **Alpha** (DXT5 alpha = 8-bit, cleaner than 4-bit RGB blocks). Normalize all packed channels to the same [0–1] range. Document the layout (e.g. `_RGB` suffix).

**Flipbook geometry:** cells must divide evenly into texture (1024÷8=128 ✓; 1024÷10=102.4 ✗ → jitter). Crop frames tight with 1–2px transparent margin + 0.5–1px edge feather to kill interpolation seams. Disable mipmaps if frame-specific lookup matters; otherwise keep PoT for clean mipdown.

**Seamless tiling (noise/flow):** `pixel(0,y)==pixel(w,y)` and `pixel(x,0)==pixel(x,h)`. QA by tiling 4x4 with rotations. Noise = 4–8 octaves Perlin/Simplex; match octaves to resolution (512 → fewer octaves to avoid alias).

**Flowmaps:** RG = XY velocity normalized [-1,1] → stored [0,255]. Decode in material: `UV = sample*2.0 - 1.0`. 512–1024 res, sRGB OFF.

**Compression:** DXT1/BC1 color (0.5 B/px), DXT5/BC3 RGBA mask (1 B/px), BC4 single-channel mask, BC5 normals/flow. Textures ≤50–60% of VRAM budget. Sprite sheet <5MB @512, <20MB @1K.

**Import in Revvy:**
```python
path = unreal.AssetDiscoveryService.import_texture("C:/Tex/T_Fire.png", "/Game/VFX/T/T_Fire")
# Then set sRGB / compression / Sampler Type on the Texture2D as required (OFF for masks).
```

---

## 2. Material Blend Mode Decision

| Blend | Use for | Notes |
|-------|---------|-------|
| **Additive** | fire, sparks, magic, energy, rings | black=transparent, no specular; invisible on bright/white bg |
| **AlphaComposite** | bright VFX needing daylight visibility | better saturation than pure additive in lit scenes |
| **Translucent** | layered smoke/fog, soft particles | 256 opacity levels, supports specular, ~50+ instr overhead |
| **Modulate** | darkening-only specialized fx | multiplies background; no lighting/separate translucency |

**Emissive:** feed `Texture × ParticleColor × EmissiveScalar(2–3)` → EmissiveColor (NOT BaseColor for glows). Must exceed 1.0 to bloom.
**Two-Sided:** enable for thin sprite/flame planes (doubles overdraw, required for correct read).
**Soft Particles:** needs Scene Depth node + softness param 50–300; prevents hard edges on geometry intersection.
**Overdraw:** prefer alpha-cutout/masked over soft transparency where possible (circular sprite renders only the circle).

### Material recipes (MaterialNodeService node chains)
- **Additive glow:** `TextureSample × ParticleColor → ×EmissiveScalar → EmissiveColor`; blend=Additive; PPV bloom thr 3–4.
- **Translucent soft smoke:** `Tex × ParticleColor → Opacity`; add SceneDepth soft-particle fade; two-sided; blend=Translucent.
- **SubUV flipbook blend:** use **Particle SubUV** node (not Texture Sample) + enable SubUV Blending on renderer → linear interp between frames (samples 2 frames).
- **Dissolve:** `grayscale mask − ScalarThreshold(0–1) → saturate → × baseOpacity → Opacity`; drive threshold by NormalizedAge via Dynamic Parameter; `smoothstep(t-feather, t+feather, mask)` for soft edge.
- **Fresnel rim:** `Fresnel(exp 3–5) × rimColor × intensity(0.5–2) → Emissive`; two-sided translucent.
- **Scroll/refraction:** `Panner(0.1–0.5) → Tex UVs`; normal map `ComponentMask(R,G) → ×Refraction(0.05–0.1) → Refraction` (translucent only).
- Drive animation by **ParticleAge/NormalizedAge**, not real-Time node (frame-rate independent).

```python
r = unreal.MaterialService.create_material("M_VFX_FireAdd", "/Game/VFX/M/")
p = r.asset_path
unreal.MaterialService.set_property(p, "BlendMode", "Additive")
unreal.MaterialService.set_property(p, "TwoSided", "true")
tex  = unreal.MaterialNodeService.create_parameter(p,"Texture","SpriteTex","VFX","",-600,0)
pcol = unreal.MaterialNodeService.create_expression(p,"ParticleColor",-600,160)
mul  = unreal.MaterialNodeService.create_expression(p,"Multiply",-350,0)
emis = unreal.MaterialNodeService.create_parameter(p,"Scalar","EmissivePower","VFX","2.5",-600,300)
mul2 = unreal.MaterialNodeService.create_expression(p,"Multiply",-150,0)
unreal.MaterialNodeService.connect_expressions(p,tex.id,"",mul.id,"A")
unreal.MaterialNodeService.connect_expressions(p,pcol.id,"",mul.id,"B")
unreal.MaterialNodeService.connect_expressions(p,mul.id,"",mul2.id,"A")
unreal.MaterialNodeService.connect_expressions(p,emis.id,"",mul2.id,"B")
unreal.MaterialNodeService.connect_to_output(p,mul2.id,"","EmissiveColor")
unreal.MaterialNodeService.connect_to_output(p,tex.id,"Alpha","Opacity")  # if masked/translucent
unreal.MaterialService.compile_material(p); unreal.EditorAssetLibrary.save_asset(p)
diag = unreal.MaterialNodeService.get_material_diagnostics(p)  # verify textures referenced
```

---

## 3. Niagara Build Pattern (Revvy API)

**Optimized emitter stack (production template):**
1. EmitterUpdate: SpawnRate (or SpawnBurst for one-shot); disable "Require Current Frame Data" if no live game data needed.
2. ParticleSpawn: InitializeParticle (lifetime 0.5–3s, color, size, base velocity).
3. ParticleSpawn: AddVelocity (initial vector).
4. ParticleUpdate: SolveForcesAndVelocity; Drag/Air Resistance (coeff 1–5 / 0.05–0.2).
5. ParticleUpdate: Size Over Life (parametric curve, 4–6 keys, NOT discrete).
6. ParticleUpdate: Color Over Life with fade-out tail.
7. Renderer (Sprite/Ribbon/Mesh) with the VFX material.
8. Disable Cast Shadow + Distance Field on lower LODs; set bounds via "Bounds to Particle Volume".
9. Profile in Insights; target <1ms total.

**Module order matters:** top→bottom; later modules overwrite earlier. Initialize → Velocity → Forces → Size/Color curves.

```python
S = unreal.NiagaraService.create_system("NS_Fire","/Game/VFX").asset_path
unreal.NiagaraService.add_emitter(S,"minimal","Flames")
E = "Flames"
unreal.NiagaraEmitterService.add_module(S,E,"/Niagara/Modules/Emitter/SpawnRate.SpawnRate","EmitterUpdate","SpawnRate")
unreal.NiagaraEmitterService.add_renderer(S,E,"SpriteRenderer","Sprite",{"Material":"/Game/VFX/M/M_VFX_FireAdd"})
unreal.NiagaraService.set_rapid_iteration_param(S,E,"SpawnRate","90")
unreal.NiagaraService.set_rapid_iteration_param(S,E,f"Constants.{E}.Lifetime.Lifetime","0.9")
unreal.NiagaraService.compile_system(S); unreal.NiagaraService.save_system(S)
```

**Color critical rules (from niagara-emitters skill):**
- Many emitters use **ColorFromCurve** which OVERRIDES Scale Color / InitializeParticle. Detect: `get_color_curve_keys(S,E)` returns keys → use `shift_color_hue(S,E,deg)` (preserves gradients) or `set_color_curve_keys`.
- No curve → `set_rapid_iteration_param(S,E,"Constants.{E}.Color.Scale Color","(r,g,b)")` (sets ALL stages; use this, not single-stage).
- Scale Color MULTIPLIES → RGB 0 kills a channel (destroys gradient). Use equal RGB to brighten/dim; `shift_color_hue` to recolor.
- SubUV flipbook: add SubUVAnimation module, set Sub Image Size (e.g. 8x8), StartFrame 0 / EndFrame 63 (=frames−1).

**Renderer choice:** Sprite (most fx), Ribbon (trails/slashes/impact rings — set Smoothing 2–4), Mesh (aura cones, shards), Light (emit light to match glow color/intensity 500–1000).

**Bounds:** GPU needs fixed bounds → "Bounds to Particle Volume" after finalizing, +10–20% padding. Re-check after changing velocity/lifetime.

---

## 4. Per-Effect Recipes → RECIPES.md (단일 출처)

효과별 빌드 레시피(불/연기/스파크/매직/충격파/트레일)는 **RECIPES.md** 가 단일 출처 (material/Niagara-driven, no-flipbook). 중복을 피하려 여기 상세 요약은 제거 — 빌드 시 항상 RECIPES.md를 Read.

빠른 기준선(상세·노드그래프는 RECIPES.md):
- **FIRE** 2층 스크롤 노이즈 × 불 LUT + age dissolve→ember, Additive, Emissive≈8. 스폰 80–120, 수명 0.5–1.2s.
- **SMOKE** panned cloud + flowmap distortion + soft particle, Translucent. 스폰 40–80, 수명 2–4s.
- **SPARKS** GPU burst + velocity-aligned, Additive Emissive≈5. 스폰 150–300, 수명 0.8–2s.
- **MAGIC AURA** polar UV swirl + mesh Fresnel rim, Additive Emissive≈15. 4–8 레이어.
- **SHOCKWAVE** mesh ring + RadialGradient + scale curve(20→400–600cm), 수명 0.6–1.0s, 단일 burst.
- **TRAIL** ribbon + length gradient + 스크롤, Orient-to-Velocity, 수명 0.4–0.8s.

> ⚠️ 모든 모션은 머티리얼 연출(스크롤/dissolve/polar/dynamic param)로 만든다. 플립북은 §0/우선순위 배너대로 폭발 연기 등 진짜 전용 케이스에만 — 위 수치의 "flipbook" 언급(구버전)은 무시.

---

## 5. Profiling & LOD

- Profile: Unreal Insights (Ctrl+Alt+F), play at realistic frequency (20–30 simultaneous casts), capture 2–3s. Targets: Niagara CPU <2ms, GPU <5ms. Use `stat unit`, `stat particles`.
- LOD0 full; LOD1 50–70% count + simpler curves + no cutout; LOD2 25–33% + flat color + no forces. Disable shadows/distance field/collision per LOD.
- Lightweight (stateless) emitters (UE5.7+): no VM/concurrent-tick cost — use for thousands of simple particles (sparks, dust, magic noise).
- GPU collision: Sphere Trace Distance Field (async, 1-frame latency, needs DX12+HW raytracing); Global SDF cheaper/less accurate. Verify mesh distance fields are built.

---

## 6. Top Pitfalls (avoid these to stay shippable)

1. Mask/erosion/flowmap left at **sRGB ON** → broken threshold/gradient. Always sRGB OFF + Sampler=Masks.
2. **Non-PoT** texture or grid that doesn't divide evenly → mip failure, frame jitter.
3. **>10 emitters** or GPU for <100 particles → wasted CPU/overhead. Consolidate; CPU for small counts.
4. **Additive on bright bg** → invisible. Use AlphaComposite, test on mid-gray.
5. **Emissive ≤1.0** → no bloom. Multiply to 2–3+; ensure PPV bloom enabled.
6. **No Two-Sided** on thin planes → particle cuts in/out.
7. **SubUVAnimation module without Particle SubUV node** in material → wrong/skipped frames.
8. **ColorFromCurve + Scale Color RGB=0** → channel destroyed. Use `shift_color_hue`.
9. **Oversized bounds** → false culling/popping. Use "Bounds to Particle Volume".
10. **Spawn all in 1 frame** → hitch. Spread over 2–3 frames.
11. **Long lifetimes (10s+)** + mass death → GC stutter. Keep short.
12. **Double color multiply** (material AND renderer override) → wrong color. Multiply once.
13. **AI texture dark borders / colored bg / baked shadows** → halos & seams. Re-prompt with black bg + no shadows + seamless; edge-blend 16–32px.
14. **Animation tied to Time node** → frame-rate dependent. Use ParticleAge.
15. **No LOD / no Insights profiling** → ships fine in test, drops frames with 20–30 live effects.


---

## Production Niagara & Material Architecture (learned from reference pack)

Reference: **NS_Level_Up VFX System Pack** (UE 4.27–5.0 era, Niagara stack-based paradigm, GPU-compatible HLSL).

### 1. Modular System Design (FULL + part systems)
Ship one combined showcase system **plus** standalone modular part systems that subset its emitters:
- `NS_Level_Up_FULL_Custom` (~9.7MB) — combines ALL emitter types (showcase / "spawn everything").
- `NS_Level_Up_Base_Custom` (~2.8MB) — core geometry only: `NE_ShockwaveMesh`, `NE_ShockwaveRibbon_01/02`.
- `NS_Level_Up_Ground_Custom` (~4.4MB) — ground layer: `NE_Shockwave`, `NE_ShockwaveMesh_01/02`, `NE_ShockwaveEdge_01/02`.
- `NS_Level_Up_Sparkles_Custom` (~2.3MB) — particle layer: `NE_Shockwave` + `NE_Sparkles_01/02`.
- `NS_Level_Up_Arrows_Custom` (~896KB) — standalone, NO inherited emitters, custom `MI_VFX_Arrows_01_Custom` only.

Build order: author FULL first, then **duplicate FULL and delete emitters** to derive each part system. Part systems let a Blueprint spawn only what scalability/gameplay needs (e.g. low-spec = Sparkles + Base, skip Ground).

### 2. Shared Reusable Emitters (NE_) Inherited by Systems
Define **5 base emitters** in an `_Emitters/` subfolder; compose them into many systems. Single emitter definition → reused at different spawn rates/timings as numbered variants (`_01`, `_02`):
- `NE_Shockwave` (~328KB) — sprite base wave.
- `NE_ShockwaveMesh` (~338KB) — mesh-renderer variant.
- `NE_ShockwaveRibbon` — ribbon trails.
- `NE_ShockwaveEdge` — edge detail.
- `NE_Sparkles` (~477KB) — physics particle burst.

A single edit to an `_Emitters/NE_*` propagates to every system that references it. Systems stagger variants via per-instance `EmitterState_LoopDelay` (e.g. wave 0.0s → mesh 0.1s → ribbon 0.15s).

**Emitter module stacks (real order):**
- `NE_Shockwave` Spawn: InitializeParticle → ParticleState → AlignSpriteToMeshOrientation → DynamicMaterialParameters → ScaleSpriteSize → Color. Update: ParticleState → AlignSpriteToMeshOrientation → DynamicMaterialParameters → ScaleSpriteSize → Color.
- `NE_ShockwaveMesh` Spawn: InitializeParticle → InitialMeshOrientation → ParticleState → DynamicMaterialParameters → ScaleMeshSize. Update drops color/orientation.
- `NE_Sparkles` Spawn: InitializeParticle → SphereLocation → AddVelocityFromPoint → ParticleState → ScaleSpriteSize → Color → ScaleVelocity → CurlNoiseForce → SolveForcesAndVelocity → DynamicMaterialParameters.
- **Rule:** `DynamicMaterialParameters` is ALWAYS last in the chain (captures all upstream parameter changes).

### 3. Sprite vs Mesh Renderer Usage
- **Sprite** (`ActiveDIs2SpriteRenderer1`): used by `NE_Shockwave`, `NE_Sparkles`, `NE_ShockwaveRibbon`. Uses `AlignSpriteToMeshOrientation` (MeshOrientationRelativeSpriteFacing/AlignmentVector float3) + `ScaleSpriteSize`.
- **Mesh** (`ActiveDIs2MeshRenderer1`): used by `NE_ShockwaveMesh`. Uses `InitialMeshOrientation` (OrientationVector + OrientationAxis, quaternion via ApplyRotationVector / FindQuatBetween) + `ScaleMeshSize`. Mesh emitters get the Fresnel material function for rim light + reduced draw calls (instancing) on high-end.
- **Point** renderer: `NE_ShockwaveEdge`.
- Module structure is otherwise renderer-agnostic — only orientation/scale modules differ per renderer type.

### 4. Master Material + MID + MPC-Global-Color + Material Function Architecture
- **Master material `M_VFX_AdvParticle_Custom`** (BLEND_Translucent — enables DepthFade + ParticleColor). Paired `_Test` master for hardcoded-color iteration.
- **Parameter naming `Effect_Type_Property_Axis`** with X/Y split for per-axis panning control: `Distor_Speed_X/Y`, `Distor_Scale_X/Y`, `Erosion_Speed_X/Y`, `Erosion_Scale_X/Y`, `GradientColor_Speed_X/Y`, `Mask_Scale_X/Y`. Single-axis scalars: `Distor_Intensity`, `Emissive_Intensity`, `Mask_Multi`, `Mask_Power`, `Wind_Speed`, `OpacityBoost`, `Opacity`, `FadeDistance`, `CamOffset_Amount`. Vectors: `Color 01`, `Color 02`.
- **~10 Material Instances** all child of the one master: 5 `_Custom` (MPC-overridable) — `MI_VFX_Arrows_01_Custom`, `MI_VFX_Glow_01/02_Custom`, `MI_VFX_Lines_01_Custom`, `MI_VFX_Shockwave_01_Custom`, `MI_VFX_ShockwaveTile_01/02_Custom`, `MI_VFX_ShockwaveTile_Ribbon_01_Custom`; + 5 `_Test` with hardcoded colors. Instances only override textures + scalars; processing chain inherited.
- **`MPC_CustomColor`** (Material Parameter Collection) = single global color controller: vectors `Color_01`, `Color_02` + scalar `Fresnel_Emissive` (~0.5 default). ALL custom MIs read from it → change once, every emitter updates, no per-emitter rebinding.
- **Material function `MF_VFX_MeshFresnel`**: ComponentMask → Fresnel (exponent ~2–5) → Multiply(color), references MPC; applied to mesh/ribbon MIs for rim light. `Emissive = (Texture*EmissiveColor) + (Fresnel*FresnelColor)` → bright core + rim stack.

### 5. SubUV / Panner / Fresnel / Distortion Patterns
- **Panner + Time chain** `Time → Multiply → Add → Panner` with independent X/Y speeds for scrolling distortion + gradient UVs (driven by `Distor_Speed_X/Y`, `GradientColor_Speed_X/Y`).
- **Distortion stack**: noise sample → Multiply by `Distor_Intensity` → add to base UV before second sample → Lerp base vs distorted color. Noise textures sampled as `SAMPLERTYPE_LinearGrayscale`; color textures `SAMPLERTYPE_LinearColor`.
- **DepthFade (soft particles)** + `Opacity` scalar wired to opacity pin (NOT opacity mask) for soft depth-aware edges — only possible because blend = Translucent.
- **Fresnel** via `MF_VFX_MeshFresnel`, exponent 2–4, × `Emissive_Intensity` into emissive; edge glow strength globally tuned by MPC `Fresnel_Emissive`.
- **DynamicParameter / DynamicMaterialParameters**: 4 scalar slots `Index0Param1/2/3/4`, mapped 1:1 to material scalars, each write-enabled (`Param0/1/2/3WriteEnabled`). Curve-sampled per-particle for runtime intensity/UV-scale.
- **HDR emissive** 1.5–3.0 magnitude for bloom; glow/sparkle layers effectively additive.
- **SubUV**: not used here (single-tile textures); `ScaleSpriteSize` curve LUT handles time-based scaling instead of atlas anim.

### 6. Curves & Randomization (real param names)
- `FloatFromCurve` / `FloatFromCurve001/002/003` — indexed curves avoid naming collisions across layers (size, opacity, emissive).
- `ColorFromCurve` (float4 RGBA over normalized age 0–1) → × `Color_ScaleColor` (RGB) × `Color_ScaleAlpha` (A) — decouples curve shape from tint intensity.
- `VectorFromCurve` → `ScaleMeshSize.ScaleFactor` for per-axis mesh deformation; `UniformCurveSpriteScale` for sprite growth/shrink.
- `SpawnBurst_Instantaneous` (one-shot, not continuous rate) + `RandomRangeInt(Min,Max,RandomSeed)` → `SpawnCount`. `LoopCountLimit=0` single burst; `>1` periodic retrigger with `EmitterState_LoopDelay` between.
- Lifetime ~0.5–2.0s mesh/ribbon, 1.5–4.0s sparkles; `CalculateRandomRangeFloat` for min/max variance.
- Sparkles physics chain: `SphereLocation` (radius, surface band, seed) → `AddVelocityFromPoint` (OriginOffset, VelocityStrength, VelocityFalloffDistance) → `CurlNoiseForce` (frequency, cone mask, strength) → `SolveForcesAndVelocity` (SpeedLimit, AccelerationLimit). `Drag` ~0.2–1.5.
- Every emitter carries a **`Scalability` module** (`NE_Shockwave_Scalability`, `NE_Sparkles_01_Scalability`) for platform spawn/lifetime reduction.

### 7. Naming Conventions
| Prefix | Meaning | Example |
|--------|---------|---------|
| `NS_` | Niagara System | `NS_Level_Up_FULL_Custom` |
| `NE_` | Niagara Emitter (shared, in `_Emitters/`) | `NE_ShockwaveMesh` |
| `M_` | Master Material | `M_VFX_AdvParticle_Custom` |
| `MF_` | Material Function | `MF_VFX_MeshFresnel` |
| `MI_` | Material Instance | `MI_VFX_Shockwave_01_Custom` |
| `MPC_` | Material Parameter Collection | `MPC_CustomColor` |
| `T_` | Texture | `T_VFX_Shockwave_01`, `T_VFX_Noise_01-04` |
- **`_Custom` vs `_Test` pairing** on every NS + every MI: Custom = MPC-driven (production); Test = hardcoded color (iteration). Switch by swapping `MI_*_Custom` ↔ `MI_*_Test` references.
- **Module param naming** `NE_[Emitter]_[Module]_[Param]` (e.g. `NE_ShockwaveMesh_01_SpawnBurst_Instantaneous_LoopCountLimit`) + emitter-scoped constants `Constants_Emitter_[Module]_[Param]` (e.g. `Constants_Emitter_InitializeParticle_LifetimeMin/Max`).
- Folder layout: `_Emitters/` (NE_), `/Particles/` (NS_), `/Materials/` or `/Shared/Materials/` (M_/MF_/MI_/MPC_).
- Textures used: `T_VFX_Shockwave_01`, `T_VFX_Shockwave_Tiles_01`, `T_VFX_White`, `T_VFX_Glow_01/02`, `T_VFX_Noise_01-04`, `T_VFX_Gradient_01-03`, `T_VFX_Lines_01`, `T_VFX_Arrows_01`, `T_VFX_Projectile_01`.

### 8. Runtime BP Color Customization via MPC
`BP_VFX_CustomLevelUp` (Actor) drives everything through MPC + modular toggles:
- **Modular toggles**: booleans `useArrows` / `useGround` / `useSparkles` gate `SpawnSystemAtLocation` (GameplayStatics) calls per part-system — each independently spawnable/destroyable.
- **Color pipeline**: `CreateWidget(ColorPickerVFX_HSV_C)` → on selection, `K2Node_CallMaterialParameterCollectionFunction` → `SetVectorParameterValue(MPC_CustomColor, Color_01/Color_02)` + `SetScalarParameterValue(Fresnel_Emissive)`. Propagates instantly to all materials, no rebuild.
- **Timeline** `Fresnel_Template` (K2Node_Timeline, 0–1 curve) animates MPC `Fresnel_Emissive` (e.g. 0→1→0.3 over ~2s) for fade-in/sustain across all emitters.
- **Sequencing**: `K2Node_ExecutionSequence` for event order, `CustomEvent_WorldPos` for position triggers, `RetriggerableDelay` to wait out particle lifetimes before `Deactivate()`/destroy. Input bound to a key (e.g. H / LMB).
- **Scalability in BP**: on low quality spawn fewer part-systems + reduce spawn count (`LoopCountLimit=0`) + shorten lifetime (1.0s → 0.3s). Target < 16ms frame.
