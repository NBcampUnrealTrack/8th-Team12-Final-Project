# CGHOW Tutorial Studies

공개 CGHOW 튜토리얼에서 확인한 레이어 순서와 머티리얼/모션 원리를 Revvy의 검증 가능한 빌드 스펙으로 변환한다. 생성 결과는 CGHOW 원본 프로젝트 복제본이 아니라 프로젝트 내 검증된 팔레트와 로컬 템플릿으로 다시 설계한 학습용 에셋이다.

## 접근 상태 (2026-07-12)

- CGHOW 공개 글과 공개 YouTube 영상은 현재 도구 세션에서 접근 가능하다.
- 현재 웹 세션은 사용자의 YouTube/Patreon 계정에 로그인되어 있지 않다. 회원 전용 Hit FX와 Water Splash 영상, Patreon 프로젝트 파일은 재생/다운로드 권한을 확인할 수 없다.
- 로컬 브라우저 쿠키나 계정 자격 증명은 읽거나 복사하지 않는다. 회원 전용 자료를 자동 학습에 포함하려면 사용자가 로그인한 브라우저 커넥터 세션이 별도로 필요하다.
- CGHOW 홈페이지의 공개 안내와 Patreon 프로젝트 파일 티어는 별도다. 구독 보유 사실만으로 현재 자동화 세션의 파일 접근 권한이 전달되지는 않는다.

## 공개 근거

- Hit FX: https://cghow.com/hit-fx-in-ue5-niagara-tutorial/
- Magical Portal: https://cghow.com/magical-portal-in-ue5-niagara-tutorial-2/
- Dark Tornado: https://cghow.com/dark-tornado-in-ue5-niagara-tutorial/
- Water Splash: https://cghow.com/ue4-niagara-water-splash-2/
- Burning Smoke: https://cghow.com/basic-smoke-to-burning-smoke-in-unreal-engine-5-niagara-tutorial/
- Electric Shockwave: https://cghow.com/unleash-the-power-crafting-electric-shockwaves-in-ue5-niagara/
- Tutorial archive: https://cghow.com/tag/tutorial/
- CGHOW Patreon: https://www.patreon.com/ashif%E2%80%8B/about

## 공통 해석 규칙

1. Primary, Secondary, Accent, Decay를 별도 이미터로 분리한다.
2. 밝은 레이어 앞에 Dark Core를 배치해 값 대비와 실루엣을 만든다.
3. 모든 one-shot은 최소 세 시점으로 stagger하고 continuous SpawnRate를 금지한다.
4. Spark/Streak 계열 스프라이트는 VelocityAligned를 강제한다.
5. 일시 레이어는 ParticleRelativeTime 기반 마스터나 Niagara curve로 알파 종료를 보장한다.
6. GPU 템플릿은 고정 bounds와 SimCache 런타임 입자/크기/알파 증거가 있어야 저장한다.
7. 중립색 MI에 Particle Color를 곱해 프리셋 tint와 MI tint의 이중 착색을 피한다.

## 빌드 가능한 5종

| Preset | Stable output | CGHOW 레이어 해석 |
|---|---|---|
| `cghow_study_impact` | `/Game/VFX/Generated/CGHOW/Impact/NS_Revvy_CGHOWStudy_Impact_V01` | Dark Core, Flash, Body, Streaks, Ring, Debris/Smoke decay |
| `cghow_study_portal` | `/Game/VFX/Generated/CGHOW/Portal/NS_Revvy_CGHOWStudy_Portal_V01` | Black Portal, vertical Ring, Rune, Mist, Motes, intermittent Arc |
| `cghow_study_lightning` | `/Game/VFX/Generated/CGHOW/Lightning/NS_Revvy_CGHOWStudy_Lightning_V01` | Charge, Hero Bolt, Electric Body, Shockwave, Ions, Residual haze |
| `cghow_study_water_splash` | `/Game/VFX/Generated/CGHOW/Water/NS_Revvy_CGHOWStudy_WaterSplash_V01` | Specular Flash, Splash Body, Mist, delayed Ripple, Crown |
| `cghow_study_dark_tornado` | `/Game/VFX/Generated/CGHOW/Tornado/NS_Revvy_CGHOWStudy_DarkTornado_V01` | Tornado Shell, Smoke Body, two-speed Wisps, early/late Arcs, Storm Motes |

Portal과 Tornado는 GeometryScript로 만든 원본 절차 메시를 사용한다. Portal은 수직 torus, Tornado는 높이에 따라 반경이 커지는 5단 불완전 torus shell이다. 기존 메시나 시스템은 교체하지 않는다.

## 로컬 템플릿 계약

- Palette: `/Revvy/VFX/_Library/NS_EmitterPalette`
- Lightning: `/Game/SoStylized/Effects/Emitters/NE_LightningSingle`, `NE_ElectricBurst`, `NE_ElectricBurstParticles`, `NE_ElectricShockwave`
- Water: `/Game/SoStylized/Effects/Emitters/NE_WaterSplashParticles`, `NE_WaterSplashMist`, `NE_RainSplash`
- Ripple reference: `/Game/SoStylized/Effects/Systems/NS_WaterRipple_v2:WaterRipple`; the study rebuilds this layer from `EM_ExpandingRing` because the copied embedded emitter produced no SimCache particles in isolation.
- Tornado smoke body: `/Game/VFX/NS_SmokeAura:Fountain`
- Tutorial MI: `/Revvy/VFX/Materials/MI_RevvyTutorial_*`

전용 renderer binding이 중요한 Lightning/Water splash 이미터는 원본 renderer와 material/curve 계약을 보존한다. 독립 실행이 불가능한 event/ripple 레이어는 명시적 burst와 검증된 mesh/material로 재구성하고, 나머지 팔레트 기반 레이어만 중립색 Tutorial MI로 교체한다.

## 실행

```python
import unreal, sys
sys.path.append(unreal.Paths.project_plugins_dir() + "Revvy/Content/Python")
import levy_vfx

print(levy_vfx.cghow_tutorial_catalog())
print(levy_vfx.build_cghow_tutorial("cghow_study_impact"))
# Graph 변경 직후 simulation_failed면 다음 editor tick에서 실행한다.
print(levy_vfx.finalize_cghow_tutorial("cghow_study_impact"))
```

완료 조건은 compile `0 errors`, 구조 gate 통과, 각 이미터의 SimCache nonzero, 저장 후 unload/reload 재컴파일 통과다. 구조 점수만으로 미감이 확정되지 않으므로 anticipation/peak/decay 뷰포트 리뷰를 별도로 수행한다.
