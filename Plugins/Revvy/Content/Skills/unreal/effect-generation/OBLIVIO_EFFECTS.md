# Oblivio 전용 호러 이펙트 레시피

레비(Levi) 전용 호러 톤 VFX 3종. 전 이펙트 공통 제약: **emissive <0.5 ambient** (임팩트 프레임만 브리프 스파이크), **Bloom 0.25 (절대 >0.35 금지)**, 채도 0.25–0.45 desaturated 팔레트, flipbook 금지 (material math + `Particle.NormalizedAge` 구동), warm orange/yellow/neon 금지. 수치는 시작값 — 에디터에서 튜닝.

---

## 1. 음파 공격 (Sonic Wave Attack) — directional expanding ring cone blast

콜드 블루-화이트(#1F4D6D) + 더스트 브라운(#6B5344). 메시 링 3장 80ms 스태거 → 전파하는 wavefront 읽힘. 카메라 독립 지오메트리 (링은 공격 방향 정렬, 카메라 페이싱 아님). 총 파티클 **135개** @ peak.

**레이어 구성**

| # | Layer | 역할 | 타이밍 |
|---|---|---|---|
| 1 | NE_SonicWaveRing_Prime | 메인 확장 링 (mesh), scale 0.05→1.4 cubic ease-out | spawn 0ms, life 0.4s |
| 2 | NE_SonicWaveRing_Secondary | 2차 링, rim +0.1 밝기 — wavefront 전파 읽힘 | delay 80ms, life 0.45s |
| 3 | NE_SonicWaveRing_Tertiary | 3차 배경 링, emissive 0.2 | delay 160ms, life 0.5s |
| 4 | NE_EnergyRibbon_Directional | 축 관통 에너지 리본 (CPU ribbon, 폭 8cm), curl noise wavering | 0–0.5s @ 8/s (총 40) |
| 5 | NE_MicroRipple_Surface | 선두 엣지 마이크로 리플 (sprite burst 30) | spawn 0ms, life 0.25s |
| 6 | NE_DustShock_Debris | 쇼크 프론트 더스트 (sprite burst 60, gravity) | spawn 0ms, life 0.6s |
| 7 | NE_DistortionWave_Haze | 잔류 heat-haze 굴절 (screen-space, 2장) | spawn 0ms, peak 0.2s, life 0.5s |

**Niagara stack** — `NS_SonicWaveAttack_Levi`, GPU+CPU 하이브리드, bounds ±500cm auto-expand, warm-up 0
- **Ring Prime** (GPU mesh): burst 1, life **0.4s** 고정, vel (0,0,0) — scale-only 확장. Scale curve (0,0.05)→(0.3,0.4)→(1,1.4) cubic ease-out. Alpha (0,1.0)→(0.7,0.8)→(1,0). 회전: attack direction Z축 정렬 (parent BP에서 바인딩). Renderer: `SM_Ring_SonicWave_Primary` (flat torus 256seg r=80cm), `M_SonicWaveRing_Primary` Additive.
- **Ring Secondary**: Prime 복제 + Event delay **0.08s**. Scale (0,0.04)→(1,1.3), emissive 0.25, `M_SonicWaveRing_Secondary`.
- **Ring Tertiary**: delay **0.16s**, scale (0,0.03)→(1,1.2), emissive 0.20.
- **Ribbon** (CPU): rate 8/s, life 0.35s±0.05, vel RandomRange(**200–400**) UU/s + RandomCone ±5° along attack dir, Curl Noise **0.3 / 2.0 Hz**, drag 0.2. Renderer: Ribbon 폭 8cm, taper cubic 0→1→0, link 50cm, subdiv 4, `M_EnergyRibbon_Sonic` Additive.
- **MicroRipple** (GPU sprite): burst 30, life 0.25s, vel 150 UU/s radial (RandomCone 30° XY), scale 0.6→0.8, alpha (0,0.6)→(0.6,0.4)→(1,0). Size 15–30cm, `M_MicroRipple` Additive.
- **DustShock** (GPU sprite): burst 60, life 0.6s, vel RandomCone(45°) 속도 **250–500** UU/s + Z ±50, gravity (0,0,**−600**), damping 0.8, Curl Noise 0.4/1.5Hz. Size 20–40cm, scale 0.8–1.2 random, `M_DustShock_Debris` Translucent + Soft Particles 50cm.
- **DistortionHaze** (CPU sprite): burst 2, life 0.5s, 위치 고정. Size 200×200cm camera-facing, `M_DistortionWave_ScreenRefract` Translucent.

**Material graph**
- `M_SonicWaveRing_Primary` (Additive/Unlit/two-sided, mesh): RadialDistance = length(UV−0.5)×2 → T_RadialGradient 샘플 + Power(dist, 2.5) fresnel-like rim → Lerp 0.6 blend → ×T_SoftGlow(Panner 0.1) shimmer → ×ParticleColor(#1F4D6D↔#2D5A6C) → ×DynParam.G ×2.0 = Emissive (**clamp 0.6**); Opacity = mask × DynParam.A. Secondary/Tertiary는 MI 변형 (G peak 0.25 / 0.20).
- `M_EnergyRibbon_Sonic` (Additive/ribbon): T_RibbonGradient LUT @ (U + DetailNoise×0.05 + Sin(Time×3 + V×10)×0.08) → 사인 wavering → ×ColorOverLife(#F0F0FF→#3A5F4F) ×DynParam.G(velocity taper) ×2.0 = Emissive (clamp 0.4).
- `M_MicroRipple` (Additive/sprite): T_RadialGradient → Smoothstep(NA±0.1) dissolve → invert = rim-only → ×#2D5A6C ×2.0 (clamp 0.3).
- `M_DustShock_Debris` (Translucent/sprite): T_SmokePuff R → Smoothstep(NA±0.15) age erosion → ×#6B5344 → Emissive ×0.1 flat; DepthFade 50cm.
- `M_DistortionWave_ScreenRefract` (Translucent): T_RefractNormalMap Panner(0.2) → remap [−1,1] → ×DynParam.G(refraction 0→**0.08**@0.2s→0) → SceneTexture UV offset; Opacity 0 (distortion only).

**Dynamic params**

| Emitter | Param | Curve | Use |
|---|---|---|---|
| Ring Prime | G (emissive) | (0,0.3)→(0.15,0.3)→(0.3,0.25)→(1,0.05) | 150ms peak 후 감쇠 |
| Ring Prime | A (alpha) | 1.0→0.8@70%→0 | edge fade |
| Ring 2nd/3rd | G | peak 0.25 / 0.20, 동일 프로파일 | 디머 wavefront |
| Ribbon | G | ParticleVelocity.Mag/500 (0–0.8) | stretch/taper |
| Ribbon | Color | #F0F0FF→#3A5F4F@0.5→투명 | 온도 시프트 |
| MicroRipple | G | flat 0.15 | quick pop |
| Dust | B | 1−NA | 난류 settling |
| Dust | Alpha | 0.8→0.6@40%→0 | 잔류 haze (dread) |
| Distortion | G | 0→0.08@0.1–0.2s→0.03@0.5→0 | heat haze 윈도우 |

**Texture inputs**
- T_SoftGlow, T_SmokePuff — *[library]* `/Plugins/Revvy/Content/Skills/unreal/effect-generation/TextureLibrary/`
- T_RadialGradient — *[codex]*: "256×256 radial gradient white center (gaussian r=60px) → black edge, clamp, BC4, sRGB OFF"
- T_NoiseDistort — *[codex]*: "512×512 seamless single-octave Perlin, grayscale, scale 0.1, BC4, sRGB OFF"
- T_RefractNormalMap — *[codex]*: "512×512 Perlin normal map, 4 oct, scale 0.05, seamless, BC5, sRGB OFF, RG flow [−1,1]"
- T_RibbonGradient — *[codex]*: "256×1 ramp white → #3A5F4F @0.5 → transparent, sRGB ON"
- 저장: `/Game/Effects/VFX/Textures/SonicWave/`

**Horror tone values**
- 팔레트: #1F4D6D (primary 블루, sat 0.4–0.5) · #2D5A6C (rim) · #3A5F4F (blue-green) · #6B5344 (dust, sat 0.25–0.3) · #F0F0FF (cool white)
- Emissive cap: Ring 0.30 peak (임팩트 0–0.15s만) / Ribbon 0.35 / Ripple 0.15 / Dust 0.10 / 시스템 총 예산 **≤2.0/frame**
- Bloom 0.25, threshold 0.8–1.0, radius 0.5–0.7. 화면 점유 @10m **<20%**
- 파티클 예산: 135/2000 — PASS. 타이밍: 링 80ms 스태거 = 느린 전파 리듬, 더스트 0.6s 잔류 = aftermath dread

**FEEL pairing** — heavy ability tier, 임팩트 프레임 스택 (frame 0 동시 실행):
1. `SetGlobalTimeDilation(0.10)` — hit-stop **100ms** (튜닝 80–150)
2. `PlaySoundAtLocation(Cue_SonicWaveImpact)` pitch ±0.3 — 3-layer: transient 8–16kHz ~50ms / body 200–400Hz ~80ms / tail reverb 100–500ms
3. `ClientStartCameraShake(CS_SonicWaveHit, 1.2)` — Perlin, loc XY amp 0.5–0.8 @12Hz, 0.25s, blend 0.05s
4. `SpawnSystemAtLocation(NS_SonicWaveAttack_Levi)` attack-direction 정렬
5. ScreenFlash MPC 1.0 → 2프레임(33ms) 페이드 / Rumble 0.7 ×100ms
6. `SetTimer(dilation→1.0, 0.1s)`

**Build steps**
1. 텍스처 4종 생성 (codex 프롬프트, 위 사양) → `/Game/Effects/VFX/Textures/SonicWave/`.
2. `M_SonicWaveRing_Primary` 마스터 + MI Secondary/Tertiary (G 0.25/0.20); `M_EnergyRibbon_Sonic`, `M_MicroRipple`, `M_DustShock_Debris`, `M_DistortionWave_ScreenRefract` 빌드.
3. 이미터 7종 생성 (`/Game/Effects/VFX/Niagara/`): Ring Prime → 복제 + delay 0.08/0.16s → Ribbon(CPU) → Ripple/Dust(GPU burst) → Haze(CPU).
4. `NS_SonicWaveAttack_Levi` 시스템에 7개 결합, bounds ±500, 컴파일.
5. 사운드: `Cue_SonicWaveImpact` (MetaSound 3-layer) + ATT_Impacts (inner 400/outer 2000 log) + CON_Monster_Voice (4–6 voice).
6. `CS_SonicWaveHit` CameraShakeBase BP (위 수치).
7. BP 함수 `Trigger_SonicWaveAttack(TargetLocation, AttackDirection)` — FEEL 스택 순서대로.
8. 테스트: 링 3장 순차 확장 (동시 X), 0.15s emissive peak, @10/30/50m "directional threat" 읽힘, 0.6s 잔류 더스트 확인.

---

## 2. 다크 오라 (Dark Aura) — light-absorbing 흑자색 상승 연기 + sickly rim

ambient 루프 (2–5s 사이클), 임팩트 FX 아님. 배경을 **어둡게** 하는 light-absorb 읽힘 — "마법 오라"가 아니라 "corruption/dread". @10m 어두운 haze, @1m 얇은 sickly rim + 간헐 motes.

**레이어 구성**

| # | Layer | 역할 | 타이밍 |
|---|---|---|---|
| 1 | NE_DarkAuraSmokeBase | 상승 curl 연기 (CPU sprite, 40/s) — 볼류메트릭 본체 | life 3.5–4.5s, 루프 |
| 2 | NE_DarkAuraRimGlow | 메시 셸 fresnel rim — sickly violet 실루엣 | life 4.0s, breathing |
| 3 | NE_DarkAuraEmberMotes | 느린 ember motes (8/s), 시체빛 점멸 | life 2.5–3.5s, 펄스 1–2회/3s |
| 4 | NE_GroundTendrilsVeins | 지면 포복 tendril/vein (6/s), dissolve 침식 | life 2.0–3.0s, radial creep |

**Niagara stack** — `NS_DarkAura`, infinite loop, warm-up **30 frames**, bounds 300×300×400
- **SmokeBase** (CPU sprite): rate **40/s**, life 3.5–4.5s, vel (0,0,**100**) cm/s + Curl Noise (scale 0.6, **0.4 Hz**, damping 0.8) → lateral drift 15–25cm/s. Pos ±15cm XY, size 15–40cm, rot random + 15–45°/s. Scale 1.0→1.15. ColorOverLife `Curve_DarkAuraColor`, OpacityOverLife S-curve peak **0.65 @ 40%** → 0.1. Renderer: Translucent/Unlit camera-facing, `MI_DarkAuraSmoke`, soft particle 60 UU.
- **RimGlow** (GPU mesh): rate 1/s, life 4.0s, 고정 위치. Scale breathing 0.9+0.1·sin(NA·2π). DynParam: FresnelPower 2.5+1.5·sin(NA·π), RimEmissive bell 0.2→**0.4 peak**→0.15. Renderer: Mesh Additive, `SM_Sphere_Distorted_32`, `MI_DarkAuraRim`.
- **EmberMotes** (GPU sprite): rate **8/s**, life 2.5–3.5s, vel RandomCone 60° Z @ 50–120cm/s, Curl Noise 0.4/0.3Hz, damping 0.3. Size **0.4–1.0cm**. Opacity 사인 펄스 0→0.4→0. Renderer: Additive, `MI_DarkAuraEmber`.
- **Tendrils** (CPU sprite): rate **6/s**, life 2.0–3.0s, vel radial 수평 30–60cm/s, pos spiral ±40cm @ Z −10 (지면), damping 0.7, Curl 0.3/0.2Hz. Size 8–20cm 폭, velocity-aligned stretch. Renderer: Translucent, `MI_DarkAuraTendril`, soft particle 40 UU.

**Material graph**
- `M_DarkAuraSmoke` (Translucent/Unlit/two-sided): Panner 2층 — T_CloudNoise_Dark(0.08, dir 0.3,0.2) + T_SmokeDetailNoise(0.15, dir −0.2,0.4) → ×VertexColor.A = DensityMask → GradientMap(NA through Curve: #2D5A6C→#3A5F4F@0.5→#1A1A1A) → (1−color)×0.5 light-absorb 다크닝 → Emissive ×0.05–0.15 flicker; Opacity = S-curve × density.
- `M_DarkAuraRim` (Additive/Unlit): Fresnel Power(1−dot(View,Normal), **2.5–4.0** breathing) → ×slow Panner 노이즈 2층 → ×Lerp(#5A4D6C violet, #7A5F4A ember, sat(NA−0.3)) → ×RimEmissive **0.3–0.5** (cap <0.5); Opacity tail fade 후반 40%.
- `M_DarkAuraEmber` (Additive): T_SoftGlow → ×Lerp(#8B7355 amber, #4A3F35 ash, NA) → ×EmberGlow 0.25–0.35 (cap <0.4); Opacity 사인 펄스.
- `M_DarkAuraTendril` (Translucent/single-sided): T_CloudNoise_Dark panned → Dissolve sat((noise−(0.3+Threshold·0.5))·2.5) — life에 따라 침식 → ×Fresnel(2.0) edge → ×Curve(#3A5F4F→#1A1A1A) → Emissive ×0.08–0.12 (cap <0.3).

**Dynamic params**

| Emitter | Param | Curve | Use |
|---|---|---|---|
| Smoke | Color | #2D5A6C → #3A5F4F@0.5 → #1A1A1A | dread 색 진행 |
| Smoke | Opacity | 0→0.1@30%→**0.65@40%**→0.4@70%→0.1 | S-curve, 잔류 haze |
| Smoke | Emissive | 0.05+0.05·sin(NA·2π) | 0.05–0.15 flicker |
| Rim | FresnelPower | 2.5+1.5·sin(NA·π) | 2.5–4.0 breathing |
| Rim | RimEmissive | 0.2→0.35@30%→0.4@50%→0.15 | bell, cap <0.5 |
| Ember | Opacity | 0.05→0.35@25%→0.05→0.35@75%→0.05 | 펄스 2회/life |
| Ember | Glow | 0.35−NA·0.1 | 점차 디밍 |
| Tendril | DissolveThreshold | 0→1 linear | vein 침식 |
| Tendril | Opacity | 0.3→0.25@60%→0 | slow creep-away |

**Texture inputs**
- T_CloudNoise_Dark — *[procedural/codex]*: fBm Perlin 512², 4 oct, lacunarity 2.0, seamless, R8, sRGB OFF
- T_SmokeDetailNoise — *[codex]*: Voronoi+Perlin 하이브리드 512², 5 oct, seamless, R8, sRGB OFF
- T_DistortNoise — *[codex]*: Simplex 256², 3 oct, scale 0.08–0.12, R8, sRGB OFF
- T_DetailNoise — *[procedural]*: single-oct Perlin 256², sRGB OFF
- T_SoftGlow — *[library]* TextureLibrary 재사용
- T_DissolveNoise — *[procedural]* T_CloudNoise_Dark 재사용 가능
- 저장: `/Content/VFX/Textures/DarkAura/`

**Horror tone values**
- 팔레트: #2D5A6C · #3A5F4F · #5A4D6C (violet rim) · #7A5F4A (dim ember) · #8B7355 (amber) · #1A1A1A (near-black) · #4A3F35 (ash)
- 채도: smoke 0.30–0.40, rim/ember 0.35–0.45 (rim 전용 accent). warm orange/red 금지
- Emissive: smoke 0.05–0.15 / rim 0.3–0.4 peak (**<0.5**) / ember 0.25–0.35 (<0.4) / tendril 0.08–0.12 (<0.3)
- Opacity: peak 0.6–0.7, 잔류 0.1–0.2, 100%에서 완전 페이드. Smoke는 Translucent only (Additive 금지)
- 콘트라스트: smoke가 배경보다 **0.15–0.25 어둡게** (darkening ≠ brightening). 플리커 **≤2Hz** (1.5–2Hz max)
- Bloom 0.25–0.30, threshold 0.9–1.0 (rim/ember 팁만 bloom)

**FEEL pairing** — AMBIENT: hit-stop/shake/flash **없음**
- SFX (옵션): 루프 드론 40–80Hz, reverb-heavy, −20dB, Niagara Play Persistent Audio @ vol 0.4–0.6, 이미터 라이프사이클과 함께 페이드
- 근접 <2m: 로컬 vignette +0.05–0.1 (post-process MPC, 플레이어 전용)
- corruption 상태 부여 시: 단발 rumble 0.2 amp ×100ms (루프 금지)
- 씬 라이팅: MPC `AuraLightAbsorb` 0→0.15 램프 (ambient −0.1~−0.2 다크닝) → 페이드 시 0 복귀
- 스크린 distortion (옵션/고비용): rim 가장자리 굴절 0.01–0.03, 프레임 예산 부족 시 스킵

**Build steps**
1. 노이즈 텍스처 3–4종 생성 (codex) → `/Content/VFX/Textures/DarkAura/`.
2. 마스터 머티리얼 4종 (`/Content/Effects/DarkAura/Materials/`): Smoke(Translucent) / Rim(Additive, fresnel) / Ember(Additive) / Tendril(Translucent, dissolve). 파라미터 노출: EmissiveMultiplier, FresnelPower, RimEmissive, DissolveThreshold 등.
3. MI 4종: MI_DarkAuraSmoke (Emissive 0.10) / MI_DarkAuraRim (Fresnel 3.0, Rim 0.35) / MI_DarkAuraEmber (Glow 0.30) / MI_DarkAuraTendril (Dissolve 0.3, Emissive 0.10).
4. 커브 6종 (`/Content/Effects/DarkAura/Curves/`): DarkAuraColor·Opacity, RimColor (Cycle 루프), EmberOpacity, TendrilColor·Opacity.
5. `NS_DarkAura` 생성: Duration Infinite, Auto-Activate false, warm-up 30. 이미터 4종 추가 (위 stack 사양), 컴파일.
6. PIE 테스트: 색 진행 blue-purple→green→black, opacity peak 0.65@40%, rim breathing, ember 펄스 1–2회/3s, tendril radial creep. @10m haze / @1m rim+motes. Emissive >0.5 픽셀 없음 확인. GPU **<0.3ms** 목표.
7. 튜닝 노브: smoke rate 30–40, curl 0.3–0.5Hz, rim 0.3–0.4, ember 4–12/s, tendril 3–10/s.
8. `BP_DarkAuraCharacter` 컴포넌트로 캐릭터 루트 부착, 위키 문서화. 옵션 프리셋: _Subtle / _Intense MI.

---

## 3. 핏빛 할퀴기 (Blood Claw Slash) — 2–4 평행 곡선 클로 슬래시 + 혈흔

비주얼 본질: **wet dark red, not neon**. anticipation 0ms → 임팩트 스냅 0.08s (1프레임@60fps) → 0.27s dissolve-out → 스테인 3.5–4.5s 잔류. Finite 0.5s, 루프 없음.

**레이어 구성**

| # | Layer | 역할 | 타이밍 |
|---|---|---|---|
| 1 | NE_ClawSlash_Main | 2–4 평행 곡선 슬래시 아크 (ribbon, taper) + erosion dissolve | 0–0.08s 확장, 0.08–0.18s peak, 0.18–0.45s dissolve |
| 2 | NE_BloodSpray_Burst | 임팩트 혈액 분사 (100±15, gravity arc + velocity stretch) | trigger 0.08s, life 1.2–1.8s |
| 3 | NE_BloodTrail_Droplets | 2차 무거운 드립 (20±5, heavy gravity) | delay 0.12s, life 0.8–2.0s |
| 4 | NE_StainDecal_Ghost | 잔류 스테인 (world-aligned 1장, slow darken) | delay 0.4s, life 3.5–4.5s |

**Niagara stack** — `NS_BloodClawSlash`, CPU+GPU 하이브리드, Duration **0.5s finite**, no loop, bounds 400×600×300 (드립 낙하용 높이)
- **ClawSlash_Main** (CPU ribbon): burst 1 @ t=0, life **0.45s** 고정. Size curve **(0,0.05)→(0.08,0.8)→(0.18,1.0)→(0.45,0)** — 임팩트 스냅. ColorOverLife #5C2E2E→#3D2F2F (alpha 255→180). DynParam: NormalizedAge, DissolveThreshold, EmissiveIntensity, RibbonWidth. Renderer: Ribbon 폭 **8–12cm**, taper cubic 0→1→0, max 100, link ≤40cm, subdiv 6, OldestFirst, `M_BloodSlash_Ribbon` Translucent.
- **BloodSpray** (GPU sprite): burst **100±15 @ delay 0.08s** (임팩트 프레임). Life 1.2–1.8s, vel RandomCone 45° Z @ **200–500** cm/s, size 0.2–0.4cm. Gravity **400** cm/s², drag 0.3. Size curve 1.0→0.6@50%→0.05. Opacity 0.9→0.85@30%→0. Renderer: **VelocityAligned** (모션 방향 stretch), `M_BloodSpray_Additive`, bounds ±500.
- **Trail_Droplets** (CPU): burst 20±5 @ delay 0.12s, life 0.8–2.0s, 하향 바이어스 50–80cm/s, gravity **600** cm/s² heavy. Size 0.1–0.3cm. Opacity 0.6→0.8 hold@20%→0.4@70%→0. `M_BloodDriplet_Sprite`.
- **StainDecal** (CPU): burst 1 @ delay 0.4s, life 3.5–4.5s, Z-pinned 지면 world-aligned flat. Size **15–35cm**. DynParam StainAlpha. `M_BloodStain_Flat`, bounds 500×500×10.

**Material graph**
- `M_BloodSlash_Ribbon` (Translucent/Unlit): T_RibbonGradient_SlashMask @ UV(U, NA) × T_BloodRibbon_Detail(Panner 0.05) → ×Curve_BloodFreshToDark(NA) → ErosionMask = Smoothstep(0.1, 0.4, DissolveThreshold − NA·2.5) — age 진행 시 침식; Opacity = Taper × Erosion × (1−NA)×0.8; Emissive ×DynParam clamp **0–0.4** (peak 0.35 @임팩트, <0.1 이후).
- `M_BloodSpray_Additive` (Additive): T_BloodSprite_Droplet × Curve_BloodHot_to_Dark ((1,0.2,0.2)→(0.2,0.08,0.08)) → Emissive peak **0.4** @0–0.2s → 0.1; Opacity = A×(1−NA)²×0.9 quad fade; VelocityMagnitude로 UV stretch.
- `M_BloodDriplet_Sprite` (Translucent): T_BloodSprite_Round × Fresnel(exp 3.0, bias 0.1) rim × Curve_BloodOld ×0.3 dark → Emissive flat **0.2**; Opacity = form × rim × (1−NA)×0.8.
- `M_BloodStain_Flat` (Translucent/Unlit): T_StainNoise_Organic ×1.5 saturate 콘트라스트 → ×Curve_StainAge (#5C2E2E→#3D2F2F→#2A1A1A 건조/산화) → Emissive **0.05–0.1** (wet sheen만); Opacity = mask × StainAlpha curve.

**Dynamic params**

| Param | Curve | Use |
|---|---|---|
| DissolveThreshold | 0→0.2, 0.08→0.5 (스냅), 0.45→1.0 | 슬래시 침식 reveal/hide |
| EmissiveIntensity | 0→0.1, 0.08→**0.35 peak**, 0.18→0.15, 0.45→0 | <0.1s 플래시 후 dark |
| RibbonWidth | 0→0.05, 0.08→1.0 스냅, 0.18→0.95, 0.45→0 | 임팩트 스냅 feel |
| BloodAlpha | 0→0.9, 0.3→0.85, 1→0 quad | 스프레이 불투명도 |
| VelocityMagnitude | normalized 0→1 | 방향성 stretch UV |
| StainAlpha | 0→0.6, 0.4→0.65 hold, 2.5→0.2, 4.5→0 | 잔류 dread fade |

**Texture inputs**
- T_RibbonGradient_SlashMask — *[procedural]*: 64×256 1D white center→transparent, BC4, sRGB OFF, clamp
- T_BloodRibbon_Detail — *[procedural]*: 256² seamless Perlin fBm 3 oct, BC4, sRGB OFF
- T_BloodSprite_Droplet — *[codex]*: "blood droplet VFX sprite, 256×256, white radial gaussian falloff, black bg, additive-ready, sRGB ON, clamp"
- T_BloodSprite_Round — *[procedural]*: 128² gaussian white, BC1, sRGB ON
- T_StainNoise_Organic — *[codex]*: "dark blood stain organic pattern, 512×512, Voronoi+Perlin cellular, seamless, R8 grayscale, sRGB OFF"
- Curve_BloodFreshToDark / Hot_to_Dark / BloodOld / StainAge — *[procedural]* 1D 컬러 램프 (sRGB, 256샘플)

**Horror tone values**
- 팔레트: #5C2E2E (fresh) · #4A2424 (coagulate) · #3D2F2F (old) · #2A1A1A (dried). desaturated red only — bright orange/yellow 금지
- Emissive: 임팩트 peak **0.35 (<100ms)** / 중반 0.15 / tail 0.1 / 스테인 0.05–0.1. **절대 >0.4 금지**
- Bloom 0.25–0.30, threshold 0.8–1.0 — peak emissive만 미세 glow
- 채도: base 0.35–0.45 → 임팩트 spike 0.60–0.70 (<100ms, 젖은 광택) → 0.20–0.30 (old blood). **>0.85 금지**
- 타이밍: anticipation 0ms / 임팩트 1프레임(16ms) / dissolve 270ms / 스테인 3.5–4.5s — sharp snap 후 느린 dark linger = dread

**FEEL pairing** — heavy melee 프리셋, 임팩트 프레임 **t=0.08s** 동기 (<50ms 허용오차):
- Hit-stop **100–150ms** + screen-shake 3–4px offset 2–3프레임 (100–150ms) — 모두 t=0.08s 동시
- SFX: sharp wet-cut @0.08s / wet-splatter foley @0.12s / drip-loop tail 0.2–0.4s, 전부 임팩트 프레임 ±25ms 키
- 카메라: 1–2프레임 desaturation 펄스 (sat 0.85 → 프레임 6에 1.0 복귀) + 옵션 vignette 0.05–0.1 → 0.2s 페이드
- 무게감: 0.45s finite = single-hit authority; gravity arc + ribbon dissolve = "weight and mess"

**Build steps**
1. `MPC_BloodClawSlash` 생성: EmissiveBase 0.35, BloomIntensity 0.28, ColorSaturation 0.40, BloodFresh/Old/Stain 컬러 벡터.
2. 텍스처 5종 + 컬러 커브 4종 생성 (procedural 우선, codex 폴백).
3. 머티리얼 4종: Ribbon(Translucent, erosion) / Spray(Additive, quad fade) / Driplet(Translucent, fresnel rim) / Stain(Translucent, slow darken).
4. `NS_BloodClawSlash` 생성 (`/Game/Effects/VFX/BloodClawSlash/`): finite 0.5s, no loop, bounds 400×600×300, warm-up 0.
5. NE_ClawSlash_Main (CPU ribbon): life 0.45s, size 스냅 커브, DynParam 4슬롯, RibbonRenderer 사양대로.
6. NE_BloodSpray_Burst (GPU): burst 100±15 @ delay 0.08s, gravity 400, VelocityAligned renderer.
7. NE_BloodTrail_Droplets (CPU): burst 20±5 @ delay 0.12s, gravity 600 heavy.
8. NE_StainDecal_Ghost (CPU): burst 1 @ delay 0.4s, world-aligned flat, life 3.5–4.5s.
9. MI 4종 (EmissiveBase 0.35/0.40/0.2/0.08) + 커브 7종 (Dissolve/RibbonWidth 스냅 포함).
10. 컴파일 + 프로파일: CPU **<1.2ms**, GPU **<2ms**. 검증: 임팩트 싱크 @0.08s, emissive peak <0.1s, bloom ≤0.3, 드롭릿 gravity arc, 스테인 3.5s+ 잔류. 옵션 `BP_BloodClawSlash` (NiagaraActor, auto-activate false) — 근접 히트 이벤트에 스폰.
11. 호러 restraint 검증: emissive ≤0.40 / bloom ≤0.35 / 채도 0.35–0.45 base / 밝은·어두운 환경 양쪽에서 "wet dark" 읽힘 (neon 금지) / gravity 400–600 (무겁게, floaty 금지).

---

## Sonic Wave 기법 업그레이드 (튜토리얼 학습)

실전 튜토리얼(80.lv 쇼크웨이브, techarthub Ring Pulse, CGHow 쇼크웨이브 5.x, RealtimeVFX radial burst, Epic 스니펫 wave distortion)에서 추출한 기법. `OBLIVIO_EFFECTS.md` §1 음파 공격(`NS_SonicWaveAttack_Levi`) 레시피를 보강한다. **호러 톤 제약(emissive <0.5, Bloom 0.25, desaturated 팔레트)은 유지** — 튜토리얼 원본 수치는 대부분 밝은 additive 기준이므로 그대로 가져오지 말고 emissive cap 안으로 스케일다운.

### 1. 링 메시 vs 스프라이트 선택 기준

| 기준 | Mesh Renderer (캡 없는 실린더 / 플랫 토러스) | Sprite Renderer |
|---|---|---|
| 확장 메인 링 (wavefront) | ✅ 표준. 스케일 확대 시 품질 유지, 카메라 독립 지오메트리 | ❌ 큰 스케일에서 빌보드 티남, 방향성 상실 |
| 공격 방향 정렬 | ✅ Mesh Orientation Binding으로 축 정렬 가능 | 제한적 (Facing/Alignment 벡터) |
| 마이크로 리플 / 더스트 / 헤이즈 | 과함 | ✅ 적합 (버스트 30–60, 카메라 페이싱) |
| 런타임 머티리얼 제어 | 다이내믹 파라미터 슬롯 0–3 | DynParam 동일 |

→ 현 레시피의 **링 3장 = mesh, 리플/더스트/헤이즈 = sprite** 구성은 튜토리얼 합의와 일치. 검증 완료, 변경 불필요. 실린더 메시는 **상/하 캡 제거** 필수 (불필요 픽셀 + 정면 시점 깨짐 방지).

### 2. Scale 커브 — 정확한 작성법

- 구동: **Float from Curve 다이내믹 인풋 + `ParticleRelativeTime`(0–1 정규화)** 핀. 레시피의 `Particle.NormalizedAge` 구동 원칙과 동일 계열.
- 비선형 가속: 커브 키 대신 **Power 노드**(머티리얼 측)로도 가능 — 지수가 클수록 피크가 늦음. 링 타이밍 미세조정에 사용.
- 튜토리얼 기준 시작값 (제너릭 쇼크웨이브): lifetime 0.4–0.6s, uniform scale 1.0→2.0, 스폰 실린더 H=1.0/R=10.0, Add Velocity(From Point) 20, Drag 8.
- **레시피 값이 더 정밀** — Ring Prime `(0,0.05)→(0.3,0.4)→(1,1.4)` cubic ease-out 유지. 튜토리얼에서 확인된 보강점: ease-out 링은 **vel (0,0,0) + scale-only 확장**이 정석 (속도 기반 확장은 메시 링에서 모서리 왜곡 유발). 현 레시피 그대로.

### 3. 다중 링 stagger 타이밍

- 기법: 이미터 복제 + **Emitter State `Loop Delay`**(또는 버스트 딜레이)로 오프셋. 레시피의 0 / 0.08 / 0.16s 3장 구성은 튜토리얼 "staggered multi-ring" 패턴과 정확히 일치 — 80ms 간격이 "전파" 읽힘의 스위트스폿.
- 보강 1 — **내부 레이어 듀얼 이미터**: 외곽 확장 링(size-over-life + 스케일 확장) 뒤에 **스케일 없는 내부 스모크 레이어**(velocity 상속, Drag 8)를 깔면 wavefront 두께감 상승. → `NE_DustShock_Debris`에 Drag 8 적용 검토 (현 damping 0.8 → 튜토리얼은 Drag 모듈 8.0 사용).
- 보강 2 — **선두 엣지 더스트**: 2차 파티클을 메인 링 위치에서 스폰 + **velocity 상속**으로 링을 따라가는 트레일 형성. `NE_MicroRipple_Surface`(현재 radial 고정 속도)에 링 스케일 추적 옵션 추가 가능.
- 싱글 레이어 링은 구식 — 현대 소닉붐은 항상 multi-emitter stagger. 레시피 7-이미터 구성 유지.

### 4. 디스토션 패스 — 2가지 경로

**A. 인월드 굴절 스프라이트 (현 레시피 방식, `M_DistortionWave_ScreenRefract`)**
- Translucent **Unlit**, 굴절률 시작값 **~1.5**, 색 제어가 필요하면 `DeriveHDRfromLDR` 노드.
- 노멀 소스: 엔진 콘텐츠 `T_Base_Tile_DetailNormal` **R 채널** 주도 — 현 레시피의 `T_RefractNormalMap`(codex 생성, BC5) 대체 가능하나 codex 텍스처가 톤 제어에 유리하니 유지.
- 현 refraction 커브 0→**0.08**@0.2s→0은 튜토리얼 권장 범위 내 (호러 톤에서 0.1 초과 금지).

**B. 포스트프로세스 wave distortion (업그레이드 옵션 — 대형 임팩트용)**
- 블렌드 위치: **Before Tonemap**(정확) 또는 After Tonemap(저렴).
- 그래프: `RadialGradientExponential`(radius/falloff) × `(ScreenPosition − Center) + UV` 오프셋, frequency/amplitude/speed를 다이내믹 MPC 파라미터로.
- 적용 시점: FEEL 스택 frame 0, hit-stop 100ms과 동기. amplitude는 0.15s 윈도우 안에서만 — 호러 톤상 화면 전체 왜곡은 1회성 임팩트 전용, 잔류 금지.
- 레시피 보강: `Trigger_SonicWaveAttack`에 옵션 단계로 추가 (PP 머티리얼 + MPC `WaveCenter`/`WaveAmp`, 0.25s 타이머로 0 복귀).

### 5. 사운드웨이브 시각화 (얇은 동심 링 / 주파수 라인)

- **얇은 동심 링**: 다중 이미터 레이어로 가는 링 여러 장 — 레시피 3-링과 동일 골격. 추가 기법: **`T_RingPulse_M` 방식 — RGB 3채널에 링 마스크 3개 인코딩** + 채널별 독립 강도 제어 → 텍스처 1장으로 링 3개 셰이딩, 드로우콜/텍스처 절약. codex 프롬프트 예: "256×256, R/G/B 각 채널에 반지름 0.35/0.55/0.75 얇은 링(폭 6px gaussian), Mask 압축, sRGB OFF".
- **노멀 디스토션**: `T_RingPulse_N`(노이즈 노멀, R/G 채널) + **`Motion_4WayChaos_Normal`**(엔진 내장 4방향 카오스 UV 왜곡, frequency/scale 제어)로 링 가장자리에 음파 떨림 부여. `M_SonicWaveRing_Primary`의 T_SoftGlow Panner 시머를 이것으로 업그레이드 가능 — 더 "진동" 읽힘.
- **타이밍 제어**: Power 노드(지수↑ = 피크 지연) + `MF_PulseTime`류 머티리얼 함수로 링별 펄스 위상차 — 머티리얼 레벨 stagger를 이미터 stagger 위에 중첩하면 사운드웨이브 특유의 박동.
- **주파수 라인**: 그라디언트 애니메이션 머티리얼 — Additive/Unlit, 단일 텍스처 + UV/rotation/scale 랜덤화로 축 방향 세로 라인 묶음. `NE_EnergyRibbon_Directional`의 보조 레이어로 8–12장 얇은 플레인 스프라이트(폭 2–4cm)에 적용하면 "음파 주파수" 시각화. 색은 #2D5A6C, emissive ≤0.2.
- **DepthFade 필수**: 평면 링 메시가 지면/벽과 교차할 때 z-fighting — `M_SonicWaveRing_*` 3종 모두에 DepthFade(50cm) 추가. **(현 레시피 누락 항목 — 적용 권장)**

### 6. 레시피 반영 요약 (NS_SonicWaveAttack_Levi diff)

| 항목 | 현 레시피 | 보강 |
|---|---|---|
| 링 머티리얼 | T_RadialGradient + Power(2.5) rim | + DepthFade 50cm (z-fight), 선택: T_RingPulse_M 3채널 통합 |
| 링 시머 | T_SoftGlow Panner 0.1 | Motion_4WayChaos_Normal로 교체 검토 (진동감↑) |
| 더스트 | damping 0.8 | Drag 모듈 8.0 + velocity 상속 (링 추종 트레일) |
| 디스토션 | 인월드 스프라이트 0.08 | + 옵션 PP 패스 (RadialGradientExponential, Before Tonemap) |
| 주파수 라인 | 없음 | 리본 보조 얇은 라인 스프라이트 8–12장, emissive ≤0.2 |
| 파티클 예산 | 135 | 라인 추가 시 ~147 — 여전히 PASS (<2000) |

금지 사항 재확인: Cascade 금지, Sprite로 메인 링 금지, 싱글 레이어 링 금지, enum 핀에 표시명 외 값 금지 (니아가라 스펙 문서 참조).

---

## 블러드 슬래시 업그레이드 — RealisticBlood 팩 학습 반영 (4-레이어 블러드 표준)

프로덕션 블러드 = 4레이어 합성 (레이어별 역할·팩 실측):
1. **Burst** — 순간 분출 스프레이 (0.1~0.5s, 스프라이트 GPU, Slash 계열이 스폰 최다)
2. **Parabolic** — 중력 포물선 낙하 드롭릿 (gravity+drag, 스프라이트 300~1500)
3. **Decal** — 바닥/벽 고임 (CPU Collision → 데칼 렌더러, 방향성 머티리얼 변형 매칭 — 임팩트 각도별 스플래터)
4. **Drip** — 잔류 드립 (저스폰 장수명 0.5~3s, 색·크기 페이드)

머티리얼: 마스터 2개(M_LiquidBase translucent + M_LiquidDecalBase)에서 MI 46종 파생 — 색/방향/강도는 전부 MI 파라미터. 웻룩 = albedo 다크레드 + Normal맵(_N_) + 리플렉션 텍스처.
플러그인 내장 텍스처: T_BloodSplatter_Core/Spray_Directional/Gore_Thick/Splotch/Puddle/Droplets/Decal_Splat + T_Normal_LiquidWave + T_LiquidReflection (MANIFEST 참조).
참고 원본: Content/RealisticBlood/<효과군>/Niagara — Hit/Slash/Stab 시스템을 템플릿 복제(duplicate)해 색·스케일만 조정하는 게 최速 (LOD High/Med/Low 변형 포함).

