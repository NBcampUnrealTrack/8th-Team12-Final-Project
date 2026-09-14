## Xociety 프로덕션 구조 학습 (레퍼런스 채택)

근거 데이터: 에셋 2,349개 — NS 572 / NE 25 / MM 156 / MI 740 / 텍스처 393 / 커브 25 / MF 26. 핵심 비율: **이미터 재사용 22:1** (572 NS ÷ 25 NE), **마스터:인스턴스 5.5:1** (740 MI ÷ 135 MM), NS 1개당 평균 이미터 3~5개 합성.

### 1. 폴더·네이밍 거버넌스 (채택)

**폴더 — NS는 컨텍스트 우선, 머티리얼은 비주얼 카테고리 우선** (Xociety /PC /Monster /LVFX ↔ /MI/Impact /MI/Smoke 이원 구조):

```
/Game/VFX/
  PC/        ← 플레이어 발동 NS (피격·투척·스킬)
  Monster/   ← 적 NS
  Level/     ← 환경·연출 NS (Xociety LVFX 대응)
  _Library/  ← NS_EmitterPalette + NS_Template_* (현행 유지)
  _Legacy/   ← §5 격리 전용
/Revvy/VFX/          ← 플러그인 내장 (현행 유지)
  Materials/ Textures/ Curves/(신설, §4-4)
```

**네이밍 규칙 4개:**
1. **NS 3계층 의무**: `NS_[컨텍스트]_[비주얼]_[NN]` — 예: `NS_PC_Hit_01`, `NS_Monster_Death_02`. Xociety 572개 전수가 이 패턴(NS_Beast_Dust_01~10, NS_AR_Hit_Pink). 1-off 금지: `NS_FireBurst` → `NS_PC_FireBurst_01`.
2. **MI 비율 1:5~10**: 색·강도·크기 변형은 MI 파라미터(Tint/EmissiveIntensity)로만. 새 마스터·새 텍스처 금지 — Impact MI 113개가 이미터 25개로 커버되는 원리. 변형 99개 초과 시에만 신규 마스터.
3. **텍스처 접미사 의무**: `_MASK`/`_N`/`_D` 채널 표기, SubUV는 그리드를 파일명에 — `T_Sparks_SubUV_4x4` (프로덕션 `T_Sparks_04_SubUV_1_4` 패턴).
4. **파라미터 명명**: 신규 마스터부터 `Color` 계열 사용 — 프로덕션 빈도 Color 1,679회 vs Tint 3회. 기존 마스터 2종의 `Tint`는 MI 6종 호환 위해 유지.

### 2. 팔레트 추가 블록 (우선순위순 5개)

현행 6블록 대비 Xociety 25 NE의 미커버 갭 — MI 수요 수치 기준 우선순위:

| # | 블록 | 근거 수치 | 역할 | 모듈 구성 (safe-op 검증 모듈만) |
|---|---|---|---|---|
| 1 | **EM_ImpactFlash** | Impact MI 113개(최대 카테고리), NE_Hit·NE_MuzzleFlash 계열 4종 | 피격·총구 1프레임 섬광. EM_BurstSparks와 합성 = Xociety 표준 임팩트 콤보(NE_Hit + NE_Hit_Sparks_02) | SpawnBurst(1~3) + InitParticle(Lifetime 0.05~0.15, SpriteSize 80~150) + Sprite(MI_LevyGlow) |
| 2 | **EM_ElectricArc** | Lightning MI 41개, NE_Electricity_* 4종 | 전기 아크 다발. 깜빡임은 짧은 수명×높은 스폰레이트로 구현 (리본 폭 조작·스크래치 금지 — safe-op matrix 준수) | SpawnRate(20~40) + InitParticle(Lifetime 0.08~0.15, 랜덤 회전·크기) + Sprite(MI_LevyLightning) |
| 3 | **EM_MeshDebris** | 메시 파티클 NE 4종(Rocks/Stone/Parts/Parts_Smoke) — 폭발·파괴 합성의 필수 레이어 | 강체 파편 방출. EM_GroundDust와 페어 = Xociety Parts+Parts_Smoke 콤보 | SpawnBurst + InitParticle(MeshScale 랜덤) + AddVelocity(point, 상방) + Gravity + Drag + Solve + Mesh renderer(기본 큐브). ※AddVelocityInCone 금지(unmet dependency 검증됨) |
| 4 | **EM_ProjectileHead** | NE_Projectile 계열 4종 중 Head 누락분, Trail MI 40개 | 투사체 머리 글로우. EM_RibbonTrail과 페어로 투사체 라이프사이클 완성(Head+Trail = Xociety 표준 콤보) | SpawnRate(高) + InitParticle(짧은 수명, SpriteSize 15~30) + Sprite(MI_LevyGlow) |
| 5 | **EM_HeatDistortion** | NE_Refraction_Sphere, refraction MI 7 + Distortion 파라미터 215회 | 열기·충격파 굴절 평면 | SpawnBurst(1) + InitParticle + Sprite(굴절 MI). §3의 M_LevyVFX_Distortion 신설이 선행조건이라 최후순위 |

블록 추가는 `build_emitter_palette.py` 확장으로 (팔레트 6→11), FASTBUILD 표에 행 추가.

### 3. 마스터 머티리얼 파라미터 확장

전 MM 파라미터 빈도: Texture 2,499 / Color 1,679 / Mask 1,666 / **UV 1,531** / Emissive 437 / Opacity 403 / **Panner 278 / Speed 272** / Fresnel 265 / **Distortion 215** / DepthFade 188 / **Erosion 147 / Dissolve 126** / SubUV 6.

**3-1. 기존 2종에 추가 (호환 무손상):** `UV_TilingX/Y`(기본 1.0), `UV_PannerX/Y`(기본 0.0) — UV 1,531+Panner 278+Speed 272 = 최대 미커버 영역. 연기·트레일의 흐름 표현 해금. 기본값이 무동작이므로 기존 MI 6종 영향 0.

**3-2. 3번째 마스터 `M_LevyVFX_Erosion` 신설** (프로덕션 `M_SmoothStep_Erosion_Translucent` 직계 대응, Erosion 147+Dissolve 126회):
- **왜**: 불·마법·연기의 소멸을 알파 페이드 대신 "가장자리부터 타들어가는" 침식으로 — 프로덕션이 전용 마스터를 둘 만큼 표준 기법.
- **구성**: Translucent·Unlit. 파라미터 = `Texture` / `ErosionNoise`(T_PerlinNoise 1장 신규 임포트, build_vfx_assets.py에 추가) / `EdgeWidth`(0.05) / `EdgeColor`(HDR) / `Color` / `EmissiveIntensity`(2.0) / `OpacityScale`(0.8) / `DepthFadeDistance`(60).
- **로직**: `alpha = smoothstep(Erosion, Erosion+EdgeWidth, Noise)` — **ErosionAmount는 ParticleRelativeTime 기반으로 머티리얼 HLSL에 굽기** (DynamicParameter 모듈 추가 불필요 = 우리 환경에서 검증된 안전 패턴, SonicWave 링 성장과 동일 방식).
- **MI 프리셋 3종**: `MI_LevyErosion_Fire` / `_Smoke` / `_Magic`.

**3-3. 후순위:** `M_LevyVFX_Distortion`(굴절 — §2 블록 5 선행조건, Distortion 215회), `FresnelPower`(265회지만 메시 전용 → ExpandingRing용 옵션만). **SubUV는 보류** — 프로덕션조차 6회로 저활용, 플립북 텍스처 확보 후 4번째 마스터로.

### 4. 시스템←이미터 재사용: copy_emitter 대비

| | Xociety | 우리 (levy_vfx) |
|---|---|---|
| 방식 | 독립 NE_ 에셋 25개를 NS가 **참조(부모 상속)** | 팔레트 NS 1개에서 **copy_emitter 사본** |
| 전파 | NE 1회 수정 → 50+ NS 동시 반영 (NE_Hit_Sparks_02 실례) | 복사 후 링크 없음 — 팔레트 수정해도 기존 NS 불변 |
| 리스크 | 손상도 전파됨 | NS 독립 — 팔레트 손상 비전파. **API 불안정 환경에선 우리 방식이 옳음(유지)** |

**copy 방식 유지한 채 배울 점 4개:**
1. **전파는 에셋 링크가 아니라 스크립트로**: `build_emitter_palette.py` = 단일 진실원. 블록 개선 = 스크립트 수정 + 팔레트 재빌드, 기존 NS 갱신 = 보존된 spec 재실행. → **규칙 추가: 모든 build() spec을 LOG.md에 기록** (재빌드 가능성 보장).
2. **콤보를 레시피화**: NS 572개의 평균이 이미터 3~5개 합성 — 블록 1개짜리 NS 지양. Xociety 표준 콤보(Hit+Sparks+Exp=임팩트, Head+Trail=투사체, Parts+Parts_Smoke=파편)를 OBLIVIO_EFFECTS.md에 "블록 콤보 표"로 명문화.
3. **변형은 이미터가 아니라 값에서**: NS_Beast_Dust_01~10이 같은 NE 공유 — 우리도 variant는 `spec["template"]` 복제 + tint/MI 교체만 (FASTBUILD 기존 규칙 강화).
4. **커브 중앙화**: C_Fire 커브 1개가 20+ NS에서 공유 → `/Revvy/VFX/Curves/` 신설해 색 진행 커브 공유. 단 우리 환경은 ScaleColor 프레임 누적 이슈가 있으므로 **머티리얼 색 굽기 우선, 커브는 보조**.

### 5. Legacy 격리 규칙

Xociety 실측: 삭제 0건 — MI/Legacy 42개, Textures/Legacy 14개, ParticleSystem/Legacy 전체 브랜치로 **이동·보존**.

1. 교체된 NS/MI/텍스처는 **삭제 금지** → `/Game/VFX/_Legacy/[원폴더구조]/`로 이동. 반드시 에디터 Move(리다이렉터 유지), 파일 탐색기 이동 금지.
2. 이동 시 에셋 설명에 `DEPRECATED YYYY-MM-DD, 대체: NS_xxx` 기록.
3. 실삭제는 **zero-reference 감사 통과 + 2 버전 주기 경과** 후에만.
4. `_Library/NS_Template_*`도 동일 적용 — 상위 템플릿 등장 시 구버전을 `_Legacy/`로 이동하고 FASTBUILD 템플릿 표 참조 갱신.
5. 이름 변경(rename) 금지 — 폴더 이동만 (리다이렉터 부담 최소화).
