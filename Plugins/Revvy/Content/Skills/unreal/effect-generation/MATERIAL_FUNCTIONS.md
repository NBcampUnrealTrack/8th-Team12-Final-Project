## Reusable Material Function Library

Build these six MFs once; every effect above composes from them. This removes duplicated panner/fresnel/dissolve/LUT logic and keeps tuning centralized. Each MF is Unlit-safe, Niagara-compatible, and works in Additive or Translucent blends. Conventions: UVs are `float2`, age inputs are `Particle.NormalizedAge` (0–1), all functions output to a downstream Multiply/Lerp into Emissive/Opacity.

Shared usage matrix:

| MF | Fire | Smoke | Aura | Shockwave | Spark | Sword Trail |
|---|---|---|---|---|---|---|
| MF_SoftParticle_DepthFade | – | ✓ | – | (opt) | – | – |
| MF_Dissolve | ✓ | (opt) | – | – | – | – |
| MF_Panner2Layer | ✓ | ✓ | ✓ | ✓ | – | ✓ |
| MF_PolarUV | – | – | ✓ | (opt) | – | – |
| MF_FresnelRim | – | (opt) | ✓ | ✓ | – | ✓ |
| MF_GradientMap_LUT | ✓ | – | (opt) | ✓ | ✓ | ✓ |

---

### MF_SoftParticle_DepthFade
Fades sprite opacity near intersecting opaque geometry to kill hard sprite edges. Used by smoke; optional for any translucent/additive sprite.

**Inputs**
- `FadeDistance` (Scalar, default 50.0 UU)
- `ExistingOpacity` (Scalar, default 1.0) — opacity to be modulated

**Outputs**
- `FadedOpacity` (Scalar)
- `DepthFadeMask` (Scalar 0–1) — raw fade factor for reuse

**Node graph**
1. `DepthFade` node (UE built-in) with `Fade Distance` = `FadeDistance` → `DepthFadeMask`
2. (fallback if DepthFade unavailable) `SceneDepth` − `PixelDepth` → Divide by `FadeDistance` → `Saturate` → `DepthFadeMask`
3. Multiply(`ExistingOpacity`, `DepthFadeMask`) → `FadedOpacity`

```
[DepthFade(FadeDistance)] → DepthFadeMask ─┐
[ExistingOpacity] ─────────────────────────×─→ FadedOpacity
```

---

### MF_Dissolve  (noise + threshold)
Erodes opacity by a moving threshold against a noise mask. Drives fire's dissolve-to-embers; reusable for any burn/dissolve.

**Inputs**
- `NoiseMask` (Scalar 0–1) — sampled noise (e.g. combined fire noise)
- `DissolveAlpha` (Scalar 0–1) — threshold/erosion driver (from `DissolveAlpha` dyn param)
- `EdgeWidth` (Scalar, default 0.1) — soft erosion band
- `EdgeColor` (Vector3, default ember `#FF6010`) — optional glowing rim
- `EdgeIntensity` (Scalar, default 3.0)

**Outputs**
- `DissolvedOpacity` (Scalar 0–1)
- `EdgeGlow` (Vector3) — additive rim color to add into Emissive

**Node graph**
1. `Threshold` = (1 − `DissolveAlpha`)  *(as DissolveAlpha→0, threshold rises → more erosion)*
2. `Mask` = Smoothstep(`Threshold`, `Threshold` + `EdgeWidth`, `NoiseMask`) → `DissolvedOpacity`
3. `Band` = Saturate((`NoiseMask` − `Threshold`) / `EdgeWidth`) → `1 − abs(2·Band − 1)` → edge band
4. `EdgeGlow` = `EdgeColor` × edge band × `EdgeIntensity`

```
NoiseMask ─┬─[Smoothstep(thr, thr+EdgeWidth)] → DissolvedOpacity
           └─[edge band] ×EdgeColor ×EdgeIntensity → EdgeGlow
DissolveAlpha →(1−x)→ thr
```

---

### MF_Panner2Layer  (two scrolling noise layers)
Two independent panners sampling (optionally) two textures and multiplying them — the core "scrolling noise" used by fire, smoke, aura, shockwave, sword trail. Set DetailTexture = BaseTexture for a single-texture cross-scroll.

**Inputs**
- `BaseTex` (Texture2D), `DetailTex` (Texture2D)
- `UV` (Vector2, default TexCoord[0])
- `BaseSpeed` (Scalar, default 0.3), `BaseDir` (Vector2, default (0,1))
- `DetailSpeed` (Scalar, default 0.6), `DetailDir` (Vector2, default (0,1.2))
- `TimeScale` (Scalar, default 1.0) — bind to age (e.g. `NormalizedAge`) for life-driven scroll
- `Combine` (StaticBool, default Multiply) — Multiply vs Add

**Outputs**
- `Combined` (Scalar/Vector) — base ⊗ detail
- `BaseSample`, `DetailSample` (for separate reuse)

**Node graph**
1. `T` = Time × `TimeScale`  (or feed age via `TimeScale`)
2. `Panner A`(UV, T, `BaseDir`·`BaseSpeed`) → `BaseSample` = `BaseTex` sample
3. `Panner B`(UV, T, `DetailDir`·`DetailSpeed`) → `DetailSample` = `DetailTex` sample
4. `Combined` = Multiply(`BaseSample`, `DetailSample`)  *(switch to Add via Combine)*

```
UV ─┬─[Panner A (BaseSpeed,BaseDir)] → BaseTex → BaseSample ─┐
    └─[Panner B (DetailSpeed,DetailDir)] → DetailTex → DetailSample ─×─→ Combined
Time×TimeScale → both panners
```

---

### MF_PolarUV  (Cartesian → polar swirl coordinates)
Converts UVs to polar (angle/radius) and applies time-based rotation — the swirl motion for magic aura; optional for radial shockwave scroll.

**Inputs**
- `UV` (Vector2, default TexCoord[0])
- `Center` (Vector2, default (0.5,0.5))
- `RotationSpeed` (Scalar, default 2.0) — radians/sec from `Time`
- `RadialScale` (Scalar, default 1.0), `AngularScale` (Scalar, default 1.0)

**Outputs**
- `PolarUV` (Vector2) — (angle 0–1, radius 0–1) packed for texture lookup
- `Radius` (Scalar), `Angle` (Scalar)

**Node graph**
1. `D` = `UV` − `Center`
2. `Radius` = Length(`D`) × `RadialScale`
3. `Angle` = atan2(`D.y`, `D.x`) / (2π) + 0.5   → normalized 0–1
4. `Angle` = `Angle` + Time × `RotationSpeed` (frac to wrap) ; × `AngularScale`
5. `PolarUV` = Append(`Angle`, `Radius`)

```
UV−Center ─┬─[Length]×RadialScale → Radius ─┐
           └─[atan2 /2π +0.5] +Time·RotSpeed → Angle ─┘ → Append → PolarUV
```

---

### MF_FresnelRim  (view-based edge glow)
View-angle rim highlight. Powers aura rim, shockwave edge, sword-trail edge feathering.

**Inputs**
- `Exponent` (Scalar, default 3.0) — rim sharpness
- `BaseReflect` (Scalar, default 0.0), `Bias` (Scalar, default 0.0)
- `Normal` (Vector3, default vertex normal) — pass (0,0,1) for flat sprites
- `RemapMin`/`RemapMax` (Scalar, default 0.0 / 1.0)

**Outputs**
- `Rim` (Scalar 0–1)

**Node graph**
1. `F` = `Fresnel`(Exponent=`Exponent`, BaseReflectFraction=`BaseReflect`, Normal=`Normal`)
2. `F` = `F` + `Bias` → Saturate
3. `Rim` = Lerp(`RemapMin`, `RemapMax`, `F`)  *(remap to taste; e.g. 0.3–1.0 for aura)*

```
[Fresnel(Exponent, Normal)] +Bias → Saturate → [Lerp(RemapMin,RemapMax)] → Rim
```

---

### MF_GradientMap_LUT  (1D gradient/color-ramp lookup)
Maps a scalar (usually age or a noise/luminance value) through a 1D LUT texture or an inline two/three-color lerp. Drives fire color ramp, shockwave tint, spark hot-to-dark, sword color, and optional aura hue.

**Inputs**
- `T` (Scalar 0–1) — lookup coordinate (e.g. `FireGradientT`, `NormalizedAge`)
- `LUT` (Texture2D, optional) — 1D color ramp (sampled at UV (T, 0.5))
- `UseLUT` (StaticBool, default true)
- `ColorA`/`ColorB`/`ColorC` (Vector3) — inline ramp fallback (e.g. hot/mid/dark)
- `MidPoint` (Scalar, default 0.5) — pivot for 3-color inline ramp

**Outputs**
- `Color` (Vector3)

**Node graph (LUT path)**
1. `UV` = Append(Saturate(`T`), 0.5)
2. `Color` = `LUT` sample (Linear, Clamp) at `UV`

**Node graph (inline path)**
1. `lo` = Lerp(`ColorA`, `ColorB`, Saturate(`T` / `MidPoint`))
2. `hi` = Lerp(`ColorB`, `ColorC`, Saturate((`T` − `MidPoint`) / (1 − `MidPoint`)))
3. `Color` = Lerp(`lo`, `hi`, step(`MidPoint`, `T`))

```
T ─[Saturate]─ Append(T,0.5) → LUT(Linear,Clamp) → Color        // UseLUT=true
T ─ two-stage Lerp(ColorA→B→C @ MidPoint) → Color               // UseLUT=false
```

---

### Composition notes (how recipes wire these together)
- **Fire** = `MF_Panner2Layer`(fire noise) → `MF_GradientMap_LUT`(FireGradientT) → `MF_Dissolve`(DissolveAlpha) → ×2.5 Emissive.
- **Smoke** = `MF_Panner2Layer`(cloud+flowmap) × VertexColor × Density → `MF_SoftParticle_DepthFade` → Opacity.
- **Aura** = `MF_PolarUV` → `MF_Panner2Layer`(noise streak) × `MF_FresnelRim` × ParticleColor × Intensity → Emissive.
- **Shockwave** = `MF_GradientMap_LUT`(radial) × `MF_FresnelRim`(via Power) × `MF_Panner2Layer`(scroll) × Alpha × Emissive → tint.
- **Spark** = `MF_GradientMap_LUT`(hot→dark by age) × EmissiveFade ×4 → Emissive.
- **Sword Trail** = `MF_GradientMap_LUT`(MPC colors) + `MF_Panner2Layer`(detail shimmer) + distortion offset × `MF_FresnelRim` × taper → Emissive/Opacity.

Recommended location: `/Content/VFX/MaterialFunctions/`. Expose each MF's tuning inputs as Material Parameters in the master materials so per-effect tuning and Material Instances need no MF edits.

---

## 검증 노드체인 레퍼런스 (Epic 공식 + adversarial 검증, UE5.5+)

> 위 MF들의 **검증된 정확 노드 체인**(6렌즈 research → adversarial 전부 isCorrect=high). MF 설명과 충돌 시 **이쪽 우선**. 공통 필수: 머티리얼 Usage **`Used with Niagara Sprites`(+Mesh/Ribbon)** 안 켜면 컴파일 실패→체커머티리얼 폴백("안 보임" 1순위). emissive는 HDR(>1)+Bloom.

### ① Dissolve (노이즈 디졸브 + 엣지 발광 밴드)
- **체인**: `TexCoord`(타일 1~4)→[opt `Panner` 0.02~0.1]→`TextureSample`(노이즈, **Sampler Type=LinearGrayscale**, R=N). 디졸브: `SmoothStep(Min=Dissolve, Max=Dissolve+EdgeHardness 0.04, Value=N)`→**Opacity Mask**. 엣지밴드: `Subtract(SmoothStep(Dissolve,Dissolve+EdgeWidth,N), SmoothStep(Dissolve+EdgeWidth,Dissolve+2·EdgeWidth,N))`=EdgeMask→`×EdgeColor`(HDR 12,3,0.2)`×EdgeIntensity`(4~10)→**Emissive**.
- **설정**: Blend=Masked(소프트엣지면 Translucent/Additive+Opacity), Two Sided On, Opacity Mask Clip 0.3333.
- **검증 정밀**: ⚠️**per-particle 디졸브=`DynamicParameter` 노드 필수**(ScalarParameter는 MI공용·입자별 불가). aliasing시 `(N-Dissolve)/max(0.0001,fwidth(N))+0.5`. Subtract만=하드엣지→SmoothStep 감쌀것.

### ② Fresnel Rim (실드/포스필드/홀로그램)
- **체인**: `Fresnel`(Utility; 핀 `ExponentIn`·`BaseReflectFractionIn`·`Normal`)→`×RimIntensity`(1~10)→`×RimColor`(HDR)→**Emissive**. Opacity=Fresnel`+BaseOpacity`(0~0.1)`Saturate`→**Opacity**. per-particle: `ParticleColor`.RGB→RimColor, `.A`→Opacity.
- **설정**: 글로우=Additive+Unlit(Opacity 무시), 반투명 실드=Translucent+`Translucency Lighting Mode=Surface TranslucencyVolume`. Two Sided On.
- **검증 정밀**: ⚠️**Fresnel 실제 기본 Exponent=5.0, BaseReflectFraction=0.04**(흔한 6/0.1 부정확). BaseReflect=1.0이면 림 사라짐→낮게. 노멀맵을 Normal에 넣으려면 Tangent→World 먼저. **upgrade_masters.py가 MaterialExpressionFresnel+ExponentIn로 동일구현 확인.**

### ③ Panner/Flowmap 2층 (흐르는 에너지/왜곡)
- **체인**: 왜곡층 `TexCoord(0)`→`Panner`(0.15)→`TextureSample`(노이즈, **sRGB OFF**)→grayscale면 `×2.0 −1.0`→`×DistortionIntensity`(0.02~0.1, **0.05 시작**)=offset. 메인층 `TexCoord(0)`→[`×타일`]→`Add(offset)`→`Panner`(반대부호 −0.1)→`TextureSample`(에너지)→`×Color`(HDR)→[`×ParticleColor`]→**Emissive**.
- **설정**: Additive+Unlit(순수가산 Opacity 미연결)/Translucent+Unlit. Two Sided.
- **검증 정밀**: grayscale `×2−1` 안 하면 **대각 밴딩**. 2층 반대부호로 반복감 제거. ("Fractional Part" 토글 모던 Panner에 없음).

### ④ Polar Coordinates (마법진/룬)
- **체인(빌트인)**: `TexCoord(1,1)`→`×4.0`→`−2.0`(재중심)→**`VectorToRadialValue`**(Material **Function**; 출력 R=각도 0..1·G=반경). 회전: 각도`+(Time×RotateSpeed 0.02)`→`Frac`→`×TileU`(룬 6~8)→`Append(각도,반경)`→`TextureSample`(룬, **MipValueMode=Derivative+DDX/DDY** 심제거)→`×Tint×Brightness`(HDR)→**Emissive**. 링밴드: 반경 `SmoothStep(0.35,0.40)×inv SmoothStep(0.50,0.48)`→Opacity.
- **설정**: Unlit+Additive/Translucent, Two Sided.
- **검증 정밀**: ⚠️`VectorToRadialValue`=**Function**(프리미티브 아님). 회전은 Append 전 각도에 +Time. 하드빌드는 `Arctangent2`(비쌈, Fast 대안). 심=Derivative MipValueMode/MipLevel 0.

### ⑤ Soft Particle (DepthFade — 연기/먼지)
- **체인**: `TextureSample`.A`×ParticleColor.A`→`DepthFade`.Opacity; `DepthFade`.FadeDistance=Scalar(8~64, **기본 100은 작은 스프라이트엔 과함**)→**Opacity**. 색: `TextureSample`.RGB`×ParticleColor`→**Emissive**.
- **설정**: ⚠️**Blend=Translucent 필수**(Opaque서 SceneDepth 쓰레기). Unlit/Default Lit.
- **검증 정밀**: DepthFade=`saturate((SceneDepth−PixelDepth)/FadeDistance)`. **불투명 지오에만 페이드**(반투명끼리 안 됨). FadeDistance≈스프라이트크기 이하. 카메라각 의존.

### ⑥ SubUV/Flipbook (스프라이트시트)
- **체인**: **`Particle SubUV` 노드**(Texture=시트, Sampler=Color/Linear Color, **Blend 체크**=프레임보간). RGB`×EmissiveIntensity`(5~50, 보통 15)→**Emissive**; A→**Opacity Mask**(Masked)/Opacity. MI 제어는 `TextureSampleParameterSubUV`.
- **설정**: Unlit, Masked/Translucent/Additive, Two Sided On.
- **검증 정밀**: ⚠️**Particle SubUV 노드 UVs 입력 무시**(TexCoord 연결 흔한 실수). 프레임=**렌더러 Sub Image Size**(8,8=64프레임)+**SubUVAnimation 모듈**(Particle Update, Mode=Linear, Number of Frames=64, Particle.NormalizedAge). 렌더러 **Sub UV Blending**+노드 Blend **둘 다** 켜야. 프레임0 고정=SubUVAnimation 누락.

---

## 고급 검증 노드체인 레퍼런스 2 (Epic 공식 + adversarial, UE5.5+)

> 고급 6종(5 high + custom-hlsl 명칭1건). Niagara Usage 플래그 필수, HDR emissive+Bloom.

### ⚠️ 선결: 굴절(Refraction) UE5.2+ 변경 — 굴절계열 공통
- **`Refraction Mode` 드롭다운**(stock; **Substrate=`Refraction Method`**). enum: **None/Index Of Refraction/Pixel Normal Offset/2D Offset**. UE4식 상시핀 폐기.
- **Refraction 핀은 Blend=Translucent+Mode≠None일 때만 노출**(Translucency Lighting Mode=Surface TranslucencyVolume/ForwardShading).
- **VFX 평면=Pixel Normal Offset**(IOR는 평면서 아티팩트). **Normal 핀 구동 필수**(빈 노멀=굴절 0). **값 1.0=무굴절, 2.0=풀**→`Constant 1.0+offset`. IOR 참고: 공기1.0/물1.33/얼음1.31/유리1.52/다이아2.42.

### ⑦ Refraction 왜곡 (열왜곡/쇼크웨이브/클로킹)
- **체인**: `TexCoord`→`Panner×2`(반대부호 0.05/0.02·−0.04/−0.03)→`TextureSample×2`(노멀맵, **Sampler=Normal, Compression=Normalmap**)→`Add`→`FlattenNormal`(Flatness 0.7~0.95)→[Lerp A=(0,0,1) B=왜곡 Alpha=DistortionStrength×ParticleColor.A]→**Normal 핀**. 별도: `Constant 1.0+(DistortionStrength×ParticleColor.A)`→**Refraction 핀**.
- **설정**: Translucent+**Refraction Mode=Pixel Normal Offset**+**Translucency Pass=Before DOF**+Two Sided. 순수 열왜곡=Opacity 0. DistortionStrength 0.1~0.5.

### ⑧ WPO 버텍스 애니 (펄스/바람 — 메시 파티클)
- **체인(펄스)**: `Time`→`Sine`(Period 10)→`Add`(+1.0 외향)→`Multiply`(×10=진폭 cm) ×[`VertexNormalWS` ×opt `TextureSample.R`]→**World Position Offset 핀**(월드 cm, 버텍스셰이더).
- **설정**: **Opaque 유지**(WPO에 Translucent 불필요). **Usage=Used with Niagara Mesh Particles**. 바람=bias 0.0+진폭 25+상단 마스크, 위상=`Dot(ParticlePositionWS, Const)`→Sine에 Add.
- **검증 정밀(Epic Fortnite doc 일치)**: ⚠️WPO=월드 cm(진폭 없으면 ±1cm 안 보임). VertexNormalWS=**버텍스셰이더 전용**(픽셀 입력 에러). UV심서 지오 갈라짐. Niagara Fixed Bounds 키우기. Time은 일시정지 무시.

### ⑨ Gradient LUT (그레이스케일→컬러 램프)
- **체인(텍스처 LUT)**: `TextureSample`(그레이, Linear Grayscale)→`Mask(R)`=U→`AppendVector(U, Constant 0.5=V)`→`TextureSample`(**T_LUT 256×N, Linear Color**)→`×EmissiveIntensity`(2~10)→[`×ParticleColor` **샘플 후**]→**Emissive**. 변형: `Curve Atlas Row Parameter` 노드(에디터서 램프 실시간 편집, 컴파일시 베이크).
- **LUT 텍스처 필수**: **sRGB OFF, Address X/Y=Clamp, Mip Gen=NoMipmaps, BC7, V=0.5**.
- **검증 정밀**: ⚠️sRGB/밉/Wrap 전부 색 왜곡. ParticleColor는 **샘플 후 곱**(U좌표 곱은 무의미). 굴절계열 Mode=None.

### ⑩ Custom HLSL 노드
- **언제만**: 빌트인 불가(루프/분기/다중출력). **상수폴딩 차단=명령 폭증**, 단순수학은 빌트인.
- **체인**: `TexCoord`→"UV"(Float2); Time/Dynamic→"Time"(Float1); **`Texture Object` 노드(TextureSample 아님)**→"Tex"→Custom(**Output Type=CMOT Float3**·Inputs·Additional Outputs)→RGB`×ParticleColor`→**Emissive**.
- **HLSL 핵심**: `Texture2DSample(Tex, TexSampler, UV)`(**샘플러 자동명 `<입력>Sampler`**). `FMaterialPixelParameters Parameters` 자동주입(직접 추가 금지). Additional Outputs=`inout`. **Output Type 반환값 일치**. **`float4` 명시**(generic vector/matrix HLSLcc 깨짐). 입력명 공백/키워드 금지. 한줄식은 return/세미콜론 불요.
- **검증(메커니즘 high)**: ⚠️굴절 라벨 stock=**Refraction Mode**(Substrate=Method).

### ⑪ Parallax / Bump Offset (서리/얼음/균열)
- **체인(BumpOffset — VFX 권장 경량)**: `TexCoord`[×Tiling]→BumpOffset.Coordinate+높이 `TextureSample.UVs`. 그레이 높이맵 R→**BumpOffset.Height**(연결전 에러). BumpOffset 출력(시차UV)→**내부 TextureSample들의 UVs**. 핀: Coordinate·Height·HeightRatioInput / 속성: **Height Ratio 0.05(swimming시 0.005)·Reference Plane 0.5**. **음수=함몰/양수=돌출**.
- **체인(POM — 불투명·무거움)**: `Texture Object`→Heightmap Texture, `Constant4Vector`(채널마스크), Min/Max Steps(8/32), TexCoord×타일→UVs. **Parallax UVs**→샘플 UVs, **Pixel Depth Offset**→메인노드 PDO.
- **검증 정밀**: BumpOffset/POM는 **UV만 시프트**(실루엣 불변→grazing 깨짐). Height Ratio 과하면 swimming. POM은 Cast Shadows 끄기.

### ⑫ 교차 발광 (콘택트 글로우/실드 림/물 셋물선)
- **체인(수동)**: `SceneDepth`(불투명 뒤 깊이, **Translucent서만 유효**)`−``PixelDepth`=Difference(교차서 ~0)→`Divide(EdgeWidth cm)`→`Saturate`→**`OneMinus`**(교차서 1)→`Power(EdgeFalloff 2~4)`→`×(GlowColor×GlowIntensity)`→**Emissive**; mask→**Opacity**(엣지전용)/Max(mask,BaseOpacity).
- **체인(1노드)**: `DepthFade`(Fade Distance=EdgeWidth)→`OneMinus`→Power→×Glow→Emissive.
- **설정**: Translucent(SceneDepth Opaque서 에러), Unlit, Two Sided. EdgeWidth cm: 콘택트선 8~20·물 50~150·실드림 100~300. GlowIntensity 2~8(블룸 16~32).
- **검증 정밀**: ⚠️**OneMinus 빠뜨리면 교차선 빼고 다 빛남**. **반투명끼리 안 됨**(CustomDepth 프록시 필요). PixelDepth는 어디서나 OK(SceneDepth만 Translucent).

**출처(검증)**: Epic 공식 docs(material-blend-modes·using-fresnel·animating-uv-coordinates·depth-material-expressions·coordinates-material-expressions·particle-expressions·using-refraction·refraction-using-pixel-normal-offset·RefractionMode API·world-position-offset-MF·curve-atlases·gradient-MF·custom-material-expressions·using-bump-offset·Fortnite create-dynamic-movement) + realtimevfx.com + tharlevfx + beyond-fx/Pete Dimitrov + techarthub + cghow + 80.lv + deepspacebanana. 12렌즈 adversarial: 10 high + 2 명칭교정.
