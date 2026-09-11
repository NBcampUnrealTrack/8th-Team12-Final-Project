## 텍스처·머티리얼 제작 심화 (Iteration 24)

### 1) 채널 패킹 규격 — 우리 표준 팩 정의

**표준 팩 `T_<Effect>_Pack` (RGBA8, DXT5/BC3, 1 B/px, 샘플러 1회):**

| 채널 | 배정 | 내용 규격 |
|---|---|---|
| **R** | 주 형태(Shape) 마스크 | 링/버스트/스트릭 등 메인 실루엣. full 0–255 |
| **G** | Erosion 노이즈 | Perlin/Voronoi 3–4옥타브, dissolve 임계용 고대비 |
| **B** | 디테일 노이즈 | 고주파(8–12옥타브 fBm) 플리커/쉬머 |
| **A** | 엣지 알파 | **최고 대비 마스크를 A에** — DXT5 알파는 8-bit 독립 인코딩이라 가장 깨끗 (CODEX_PROMPTS.md 채널팩 노트와 일치) |

- **임포트 설정(고정)**: sRGB **OFF**, Sampler=**Masks**, Compression=Masks(DXT5), `never_stream=True`, Mip 유지(원거리 LOD), 해상도 PoT 512(기본)/1024(히어로)
- **단일 채널만 쓸 땐** BC4(0.5 B/px), flowmap RG는 BC5(1 B/px) — 팩 불필요
- **머티리얼 분해**: `TextureSample` 1회 → `ComponentMask(R/G/B/A)` 4분기. 별도 텍스처 3장 대비 샘플러 3→1, 대역폭 약 1/3
- **비용 근거**: 512² 팩 = 256KB(BC3), 분리 3장(BC4×3) = 384KB + 샘플러 2개 추가. 50+ 이펙트 스케일에서 팩 우세
- 네이밍: `T_RingPulse_Pack` 식 `_Pack` 접미사, 채널 배정은 에셋 메타/MANIFEST에 기록

### 2) 텍스처 타입별 "좋은 결과" 기준 + 제작 레시피 (codex vs PIL 라우팅)

**합격 기준 (수치 체크리스트):**
1. Additive: 배경 순수 RGB(0,0,0) — 회색 헤일로/베이크된 블룸·그림자·AO 0
2. 중앙 정렬, 요소 높이 프레임의 60–80%
3. 히스토그램 full 0–255 (mid-gray만 몰린 마스크는 불합격 — `smoothstep(0.2,0.4,mask)`에 깨끗한 밴드가 나와야 함)
4. 타일링: 4×4 반복+회전 시 시임 0px (`pixel(0,y)==pixel(w,y)` 스크립트 검증)
5. 플립북: 셀 균등 분할 — 1024÷8=128 ✓ / 1024÷6=170.67 ✗, 셀 마진 1–2px + 엣지 페더 0.5–1px

**라우팅 매트릭스 (Iteration 2/9 실측 기준 유지 + 개선):**

| 타입 | 경로 | 레시피 |
|---|---|---|
| Additive 단일 스프라이트(불/링/스파크/임팩트/매직/트레일) | **codex ✅** | 기존 템플릿 + 스타일 modifier APPEND (셀/핸드페인트/애니 3종 검증됨) |
| 플립북 | **codex ✅ (≤6×6)** | 8×8은 셀 정렬 흔들림 위험 — 6×6 이하 + 머티리얼 SubUV frame blending 보정 |
| Seamless 노이즈/erosion/flowmap | **codex ❌ → 절차적** | 1순위 UE `Noise` 노드(텍스처 저장 0), 2순위 PIL/NumPy |
| Gradient LUT | **PIL** | 256×8 또는 512×8 sRGB, 키컬러 큐빅 보간 (불: #8B0000@0 → #FF6600@0.35 → #FFFF00@0.7 → #FFFFFF@1.0) |
| 채널 팩 합성 | **PIL** | codex/절차 채널 각각 생성 → `Image.merge("RGBA", …)` → sRGB OFF Masks 임포트 |

**PIL 절차 레시피 (수치):**
- 주기적 Perlin: `sin(2π·x/period)` wrap 기반, base 512² 2–4옥타브 scale 0.05–0.15, detail 256–512² 8옥타브 lacunarity 2.0
- Erosion 마스크: Voronoi + 히스토그램 평활화(대비 스트레치)로 0–255 풀레인지 강제
- Flowmap: Perlin 스트림함수 ψ의 직교 그래디언트 ∇⊥ψ (divergence-free curl), RG ±1→0–255 인코딩, 512² BC5
- codex 노이즈를 부득이 타일링: offset(50%,50%) + 경계 16–32px linear 블렌드 패스

**codex 프롬프트 개선 2건:** ① 플립북 템플릿 기본 grid를 8×8→**6×6(36프레임)**으로 하향(검증 안정 구간), ② 채널팩용 단일 채널 생성 시 `single channel, full 0-255 range, linear no gamma` 강제 — A채널 후보는 "high contrast for alpha clipping" 명시.

### 3) 머티리얼 노드 체인 추가분 (MEL 조립 가능 형태)

기존 `expr()`/`connect_material_expressions()` 패턴(build_vfx_assets.py L60–64)으로 그대로 조립 가능한 4종:

**(a) PolarUV 스월** — 클래스: `Subtract`(UV−Center) → `ComponentMask`×2 → 커스텀 HLSL(`MaterialExpressionCustom`: `float2(atan2(d.y,d.x)/(2*PI)+0.5 + Time*RotSpeed, length(d)*RadialScale)`) → `Frac` → 텍스처 UVs 입력. atan2는 네이티브 노드가 불안정하므로 **Custom 노드 1개로 묶는 것이 MEL 조립 최단** (입력 d, RotSpeed, RadialScale 3개). RotSpeed 0.5–2.0 rad/s.

**(b) Radial Gradient 절차** — `MaterialExpressionMaterialFunctionCall` + `set_editor_property("material_function", load_asset("/Engine/Functions/Engine_MaterialFunctions01/Gradient/RadialGradientExponential"))`. Density 2.5–4.0(엣지 샤프닝), Radius 0.5. 텍스처 0장으로 링/소프트글로우 대체.

**(c) Flowmap 디스토션** — `TextureSampleParameter2D`("Flowmap") → `Multiply`(×2) → `Subtract`(−1) [디코드] → `Multiply`(×ScalarParameter "DistortionStrength" 0.05–0.15) → `Add`(+TexCoord) → 메인 텍스처 UVs. Flowmap 자체는 Panner 0.02 U/V로 저속 스크롤.

**(d) DynamicParameter 배선 규약 (고정 컨벤션):**
- 머티리얼: `MaterialExpressionDynamicParameter` 1노드(Index 0), 출력 핀 Param1–4 = R/G/B/A
- **R=dissolve(0–1), G=emissive(0–2), B=distortion(0–0.15), A=edge softness(0–0.5)** — 전 마스터 공통, 예외 금지
- Niagara: `Set Dynamic Material Parameter` 모듈은 **이미터 스택 맨 마지막**(이후 설정값은 머티리얼에 안 보임), 커브 드라이버는 `Particle.NormalizedAge`(프레임레이트 독립 — Time 노드 금지)

### 4) 마스터 아키텍처 결론 — 프로그램 생성 관점 권장

**결론: uber+static switch 기각, "소형 마스터 3–5개 + 깊은 MI 계층" 채택.**

근거(수치):
1. Static switch는 런타임 비용 0이지만 **2^N 셰이더 퍼뮤테이션** — 독립 스위치 3개=8변형, 프로젝트 한도 <20 변형. AI가 마스터를 반복 재생성하는 우리 파이프라인에선 컴파일 시간이 이터레이션 속도를 직접 깎음
2. MEL에서 `StaticSwitchParameter`의 MI별 override 설정·검증 경로가 스칼라/벡터 대비 불안정 — 프로그램 생성 신뢰도 저하
3. MF는 컴파일러가 인라인 — 공통 체인(Panner2Layer/Dissolve/FresnelRim/GradientMap/PolarUV/SoftParticle 6종) 중앙화 비용 0, 마스터는 MF 합성만
4. 운영 비율 목표: **MI:master ≥ 5:1**(Xociety 실측 5.5:1) — 변형은 전부 파라미터(MIC of MIC: `M_Master → MI_Variant → MI_Variant_Bright`)로, 새 마스터는 파라미터로 표현 불가할 때만
5. 현 3마스터(Additive/Translucent/Erosion, 스위치 0개)가 이펙트의 약 80% 커버 — 부족분은 마스터 추가(아래 §5)로 해결, 스위치 도입 불필요

### 5) build_vfx_assets.py 추가 우선순위 (≤3)

| 순위 | 추가 항목 | 내용 / 근거 |
|---|---|---|
| **P1** | **`M_LevyVFX_Dynamic`** (DynamicParameter 마스터) | Erosion 체인 복제 후 `ErosionAmount/EmissiveIntensity/DistortionStrength/EdgeSoftness`를 `MaterialExpressionDynamicParameter` R/G/B/A로 치환. 현재 3마스터 모두 per-particle 수명 제어 수단이 ParticleColor.A뿐 — dissolve 커브·emissive 피크(수명 0.4–0.6 지점) 연출 불가. 노드 +1, 파라미터 컨벤션은 §3(d) 고정 규약 |
| **P2** | **`M_LevyVFX_Polar`** (오라/룬 회전 마스터) | PolarUV Custom 노드 + `RadialGradientExponential` 함수콜 + Fresnel(Exp 3.0, remap 0.3–1.0) + 기존 Tint/EmissiveIntensity. `T_MagicRunic` 회전(RotSpeed 파라미터 0.5–2.0)·오라·쇼크웨이브 계열 커버 — RECIPES의 Aura/Shockwave 2종이 현 마스터로 표현 불가 |
| **P3** | **Erosion 마스터 채널팩 변형 파라미터** | `M_LevyVFX_Erosion`의 `ErosionMask` 별도 샘플러를 메인 `Texture`의 **G채널 ComponentMask**로 대체하는 `M_LevyVFX_ErosionPacked` (§1 표준 팩 전제). 샘플러 2→1, 텍스처 에셋 2→1 — 팩 텍스처 도입과 동시 적용 |

공통: 세 항목 모두 기존 `new_material()`/`expr()`/idempotent-skip 패턴 재사용, MI는 `MIS` 테이블에 행 추가만으로 등록.
