## CGHow 기법 학습 (시그니처 패턴 채택)

CGHow(Ashif Ali) 300+ 튜토리얼에서 추출한 시그니처 패턴을 우리 표준(마스터 3종 + 팔레트 8블록 + RECIPES 12패밀리 + OBLIVIO 3종)과 대조. 수치 우선.

---

### 1. 시그니처 머티리얼 패턴 — 우리 마스터 대비 갭

**보유 현황 대조표**

| CGHow 시그니처 | 핵심 수치 | 우리 마스터 보유? | 갭 |
|---|---|---|---|
| Radial Exponent 절차 그라디언트 | density **100–500**(샤프), Divide **0.1–0.01**(소프트) | ❌ — T_RadialGradient **텍스처 의존** | 머티리얼 내 절차 생성으로 텍스처 1장 제거 가능 |
| Sharp×Blur 곱 (링 윤곽) | 샤프 density **~20** × 블러 radial 곱 → 또렷한 외곽 + 페이드 중심 | ❌ | 링/포탈/임팩트 전 패밀리에 재사용 |
| Polar UV 회전 (매직서클/스월) | angle = Time×**2** rad/s 기준 | RECIPES #3에 MF_PolarUV만 존재, **마스터 미탑재** | 4번째 마스터 후보 |
| Fresnel (dot(camera·normal) 직접 구현) | exponent **2.5–3.0** | MF_FresnelRim은 레시피에만, 마스터 파라미터 ❌ | `FresnelPower` 파라미터 추가 |
| SmoothStep 디졸브 | threshold ± **0.1–0.15** | ⭕ Erosion 마스터 (ErosionAmount/Softness) | 동등 — 유지 |
| Panner 루프 | speed **0.1–0.6**, Append(X,0) | ⭕ Erosion 마스터 Panner | 동등 |
| Saturate 포스트-그라디언트 (와이프) | 0–1 클램프 후 Power | △ 부분적 | Power/Contrast 파라미터 표준화 필요 |
| Dynamic Parameter 라우팅 | erosion/sharpness/dissolve를 **DynParam RGBA**로 노출 | △ OBLIVIO에서 G/A 사용하나 **마스터 레벨 컨벤션 없음** | RGBA 의미 고정 필요 |
| WPO 노이즈 변형 | vertex normal × noise × multiplier | ❌ | 오라 메시 셸 breathing용 |
| Two-sided + Before DOF | soft particle fade **50UU** | ⭕ Translucent 마스터 DepthFade | 동등 |

**권장: 4번째 마스터 `M_LevyVFX_Radial`** — 파라미터 6개:
1. `RadialDensity_Sharp` (기본 **20**, 범위 20–500)
2. `RadialDensity_Soft` (기본 **0.05** = Divide 1/20)
3. `PolarRotSpeed` (기본 **2.0** rad/s, 0=정지)
4. `FresnelPower` (기본 **2.5**)
5. `EdgePower` (기본 **2.0** — 엣지 샤프니스, CGHow Power/Contrast)
6. 기존 공통 Tint/EmissiveIntensity 상속

체인: `MF_PolarUV(Time×PolarRotSpeed)` → Radial Exponent(Sharp) × Radial(Soft) → Saturate → Power(EdgePower) → dot(Camera,Normal)^FresnelPower 가산 → ×Tint ×Emissive. **링·포탈·매직서클·슬래시 그라디언트 4개 패밀리를 텍스처 0장으로 커버.**

**기존 마스터 추가 파라미터** (마스터 신설 없이):
- `M_LevyVFX_Additive` += `FresnelPower`(2.5, 0=off) — 에너지/오라 80% 케이스 커버
- `M_LevyVFX_Erosion` += `EdgePower`(2.0) + ErosionAmount의 **DynParam.R 라우팅 옵션**

---

### 2. 효과별 핵심 기법 (모듈 스택 + 수치)

#### 슬래시 (Slash)
- **메시**: 디스크 메시 Z축 스케일 다운, 스트레이트 UV, **Unlit + Two-sided** 필수
- **머티리얼 체인**: TexCoord G/R 채널 분리 → Subtract/Multiply/Contrast → 3색 블렌드 → 노이즈 왜곡 → 지수 커브 색 매핑. Power/Contrast Divide **0.1–0.01**
- **슬래시 오라**: X축 Panner + 중심 sine 마스킹 × 노이즈 → DynParam 디졸브 페이드
- **리본 트레일**: spawn **100/frame**, life **0.3–1.5s** 랜덤, Curl Noise **50** (freq 1–500), **Tessellation OFF**(직선 유지), 폭 커브 테이퍼(꼬리 0)
- **AnimTrail**: Persistent ID + 본 소켓 + Level Sequencer 키프레임 (다중 인스턴스 타이밍/위치 오프셋)

#### 임팩트 (Impact/Hit)
- **8레이어 스택** (스태거 **0.1–0.3s**): Dark Core(fountain) → Flash(밝은 버스트) → Flare(지연 2차) → Glow(랜덤 페이드+drag) → Streaks → Sphere(refraction+fresnel) → Ring(원형 스케일) → 포스트 플래시
- **그라디언트**: Radial Exponent density **100–500** → Saturate → Lerp(외곽 밝음/중심 어두움) → Power
- **지면 파괴**: burst **1000**, debris gravity/collision, 글로우 강도 커브 스케일링
- ⚠️ OBLIVIO 적용 시: Flash/Flare 레이어 emissive **cap 0.3–0.6** + 임팩트 프레임 0–150ms만 스파이크 (기존 호러 제약 유지)

#### 포탈 / 매직서클
- **Polar UV 회전**: Custom HLSL 노드(Magic_Circle.ush 방식) 또는 MF_PolarUV — angle=Time×2
- **링 윤곽**: Vector-to-Radial 샤프(density **~20**) × 블러드 radial **곱** = 또렷한 윤곽 + 페이드 중심
- **링 동기 분포**: Spawn Burst(**1000+**) + **Execution Index 링크** → 원형 균등 배치; Vortex Velocity로 스월
- **왜곡**: X/Y **분리 파라미터** (대각선 아티팩트 방지) + 노이즈 UV 왜곡

#### 오라 (Aura)
- **메시 셸**: 실린더/스피어 + **dot(CameraVector·VertexNormal)** 프레넬 (Fresnel 노드 대신 — 제어성↑), Power 커브로 엣지 강도
- **WPO breathing**: VertexNormal × Noise × multiplier → 메시 변형 (지오메트리 변경 없이 wobble)
- **G채널 상하 그라디언트**: TexCoord green → Power 페이드
- **모션**: Curl Noise **50** + Spring 복귀 + jitter (유기적 비반복 모션 시그니처)
- ⚠️ 우리 NS_DarkAura의 RimGlow 레이어와 동형 — dot-product 프레넬 + WPO만 추가하면 됨

---

### 3. 스타일라이즈드 룩 레시피 (CGHow 공식)

1. **3레이어 최소 구성**: 코어 이펙트 + 액센트 파티클(sparks) + 왜곡/글로우 레이어 — 각각 별도 머티리얼
2. **메시 > 스프라이트**: 토러스(토네이도)/디스크(슬래시)/플라토닉 스피어 subdiv 3(헥사 실드) — 스프라이트 아틀라스 대신 절차 머티리얼 입힌 메시
3. **Step vs Power 토글**: Step 노드=크리스프 엣지(별/기하), Power 블렌딩=소프트 — 파라미터 하나로 스타일 전환
4. **3색 존**: 코어 글로우 → 미드 전이 → 밝은 엣지, Lerp 체인 + 존 마스킹 (단일 머티리얼 내)
5. **픽셀레이트**: floor(UV×N)/N 수학만으로 블록 스타일 (텍스처 불필요)
6. **노이즈가 텍스처를 대체**: cloud-filter/Perlin/curl — 플립북 금지 원칙과 **완전 일치** (우리 표준과 동일 철학)
7. **스폰 지오메트리가 실루엣 결정**: 실린더(불)/radial(에너지)/sphere r=100(curl) — 파티클 모양보다 스폰 위치
8. ⚠️ CGHow "Additive Unlit 풀밝기 글로우 80%" 공식은 OBLIVIO 호러 톤과 **충돌** — 채택 시 emissive <0.5 ambient + Bloom 0.25 캡 그대로 유지, 구조(레이어/존/메시)만 가져온다

---

### 4. 팔레트/레시피 추가 우선순위 (5개)

| # | 항목 | 내용 | 효과 |
|---|---|---|---|
| 1 | **마스터 4호 `M_LevyVFX_Radial`** | Polar UV(rot 2.0) + Radial Exponent(sharp 20/soft 0.05) sharp×blur 곱 + FresnelPower 2.5 + EdgePower 2.0 | 링·포탈·매직서클·임팩트 그라디언트 텍스처 0장화; T_RadialGradient codex 생성 단계 제거 |
| 2 | **팔레트 9블록 `EM_SlashArc`** | 디스크 메시 burst 1, life 0.2–0.35s, Unlit two-sided + Erosion 마스터, DynParam 디졸브 | 슬래시 패밀리 공백 해소 (ExpandingRing으로 대체 불가한 방향성 아크) |
| 3 | **RibbonTrail 블록 업그레이드** | Curl Noise 50(freq 1–500) + Tessellation OFF + 폭 커브 테이퍼 + life 0.3–1.5s 랜덤 디폴트 | CGHow 리본 표준 수치 채택 — 현 8cm 고정폭 대비 꼬리 테이퍼 품질↑ |
| 4 | **DynParam RGBA 컨벤션 고정** | R=Erosion, G=Emissive, B=Power/Sharpness, A=Alpha — 마스터 3+1종 전체 통일 | OBLIVIO에서 G/A만 쓰던 임시 관행을 표준화; 파티클별 커브로 머티리얼 재컴파일 없이 제어 |
| 5 | **RECIPES 스태거 표준 절** | 2차 +80ms/3차 +160ms(링), 일반 0.1–0.3s + Spawn Burst+ExecIndex 링 동기 패턴 명문화 | SonicWave 80ms 스태거를 전 임팩트 패밀리 규칙으로 일반화 |

**근거 파일**: `C:\Unreal\8th-Team8-CH3-Project\Plugins\Revvy\Content\Skills\unreal\effect-generation\RECIPES.md` (MF_PolarUV/MF_FresnelRim 레시피 존재 확인), `OBLIVIO_EFFECTS.md` (DynParam G/A 사용·80ms 스태거 기존 적용), `Plugins\Revvy\Content\Python\levy_vfx.py` (팔레트 블록 from/set/tint/material 스키마 — EM_SlashArc 추가 시 빌더 변경 불필요)
