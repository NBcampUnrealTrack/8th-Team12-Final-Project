# FASTBUILD — 이펙트 초안 조립 절차

초안 제작은 **블록 조립 + 값 설정**으로 호출 수를 줄인다. 단계마다 스크린샷을 찍지는 않지만 `PRODUCTION_PIPELINE.md`의 최종 시각·성능 검증은 필수다.

> ⚡ **속도의 핵심은 도구 호출 횟수다.** 실측: 도구 실행 자체는 호출당 ~0.6초지만, 호출 사이 LLM 왕복이 수 초~수십 초. 작은 파이썬을 30번 나눠 부르면 그것만으로 수 분~십수 분. **이펙트 1개 = execute_python_code 1~3회**가 목표.

## 표준 절차 — 원샷 빌더 (기본)

1. **레시피 결정** — OBLIVIO_EFFECTS.md(프로젝트 전용) 또는 RECIPES.md에서 해당 효과 섹션만 Read. 문서 3개 이상 읽지 말 것.
2. **spec 작성 → `levy_vfx.build(spec)` 1회 호출** — 시스템 생성/블록 조립/값/틴트/머티리얼/컴파일/저장 일괄:
   ```python
   import unreal, sys; sys.path.append(unreal.Paths.project_plugins_dir() + "Revvy/Content/Python")
   import levy_vfx
   print(levy_vfx.build({
       "path": "/Game/VFX/NS_FireBurst",
       "blocks": [
           {"from": "EM_BurstSparks", "as": "Sparks",
            "set": [["SpawnBurst_Instantaneous", "SpawnCount", "80"],
                    ["InitializeParticle", "Lifetime", "1.2"]],
            "tint": "(3.0, 1.0, 0.2)"},
           {"from": "EM_GlowCore", "as": "Core",
            "set": [["InitializeParticle", "Lifetime", "0.5"]]},
       ],
   }))
   ```
   - 팔레트 자동 보장(없으면 자체 구축), `template` 키로 완성형 NS 복제 시작도 가능.
   - 반환 로그에서 **FAIL 라인만** 후속 호출로 수정 (성공 라인 재실행 금지).
3. **구조 gate** — `compile_with_results`와 Niagara Message Log, emitter/module/renderer/material binding, Effect Type과 bounds를 검증한다. builder의 `compile: OK`만으로 성공 판정하지 않는다.
4. **완료 gate** — preview에서 핵심 시점과 dark/light 배경을 캡처하고, Niagara Debugger/PIE에서 target quality와 worst-case instances를 측정한다. 증거 형식은 `PRODUCTION_PIPELINE.md`를 따른다.

수동 조립(팔레트에 없는 새 블록)도 **한 스크립트에 묶어서** — add_emitter+add_module들+set들+compile을 1회 execute_python_code로.

## 팔레트 블록 구성 (NS_EmitterPalette)

| 블록 | 구성 | 주 파라미터 (set_module_input 대상) |
|---|---|---|
| EM_BurstSparks | SpawnBurst + InitParticle + AddVelocity(point) + Gravity + Drag + Solve + Sprite | Spawn Count, Lifetime, Velocity Speed, Sprite Size |
| EM_ExpandingRing | SpawnBurst(1) + InitParticle + Mesh renderer | Lifetime, Mesh Scale (+머티리얼 교체) |
| EM_RisingWisps | SpawnRate + InitParticle + AddVelocity(up) + CurlNoise(V2) + Drag + Solve + Sprite | Spawn Rate, Lifetime, Velocity, Noise Strength |
| EM_GlowCore | SpawnRate(low) + InitParticle(긴 수명) + Sprite | Lifetime, Sprite Size, Color |
| EM_GroundDust | SpawnBurst + InitParticle + AddVelocity(radial) + Drag + Solve + Sprite | Spawn Count, Velocity, Sprite Size |
| EM_RibbonTrail | SpawnRate + InitParticle + Ribbon renderer | Spawn Rate, Lifetime, Ribbon Width |
| EM_ElectricArc | SpawnBurst + InitParticle + AddVelocity + Drag + Solve + Ribbon | Spawn Count, Velocity (MI_LevyLightning 권장) |
| EM_MeshDebris | SpawnBurst + InitParticle + AddVelocity + Gravity + Drag + Solve + Mesh | Spawn Count, Velocity, Mesh 교체 |

모듈명은 공백 없는 실제 함수명("InitializeParticle"/"SpawnBurst_Instantaneous")을 사용한다. 팔레트의 InitParticle 실제명은 InitializeParticle001이며 현재 서비스는 substring으로 찾으므로, 복제 뒤 실제 stack과 입력 대상이 맞는지 반드시 다시 조회한다. 팔레트는 baseline이지 품질 검증 완료본이 아니다.

팔레트 없으면(첫 사용) 1회 구축: `Plugins/Revvy/Content/Python/build_emitter_palette.py` 를 execute_python_code로 실행.

## 표준 텍스처·머티리얼 (플러그인 내장 — `/Revvy/VFX/`)

1회 구축: `build_vfx_assets.py` 실행 (텍스처 임포트 + 마스터 + MI. 재실행 안전).

| 에셋 | 용도 |
|---|---|
| `/Revvy/VFX/Materials/M_LevyVFX_Additive` | **마스터** (additive·Unlit): 파라미터 Texture / Tint / EmissiveIntensity(2.0) × ParticleColor |
| `/Revvy/VFX/Materials/M_LevyVFX_Translucent` | **마스터** (연기·먼지): + OpacityScale(0.8) / DepthFadeDistance(60) soft particle |
| `/Revvy/VFX/Materials/M_LevyVFX_Erosion` | **마스터** (스크롤+소멸): Panner(PanSpeedX/Y) + ErosionMask/Amount/Softness — 불·에너지·dissolve 계열 |
| `MI_LevyGlow` · `MI_LevySpark` · `MI_LevyFireWisp` · `MI_LevyRunic` · `MI_LevyLightning` | additive 마스터 + 내장 텍스처 |
| `MI_LevySmoke` | translucent 마스터 + T_SmokePuff(grayscale) |
| `/Revvy/VFX/Textures/T_*` 6종 | SoftGlow/SparkStreak/SmokePuff/FireWisp/MagicRunic/LightningArc |

규칙: 렌더러 머티리얼은 **MI를 먼저** (`spec` blocks의 `"material": "/Revvy/VFX/Materials/MI_LevyGlow"`). 색·밝기 변형은 새 머티리얼 만들지 말고 MI 파라미터(Tint/EmissiveIntensity) 또는 set_color_tint. 마스터를 직접 수정 금지.

## 금지 목록
- ❌ 작은 파이썬 다회 호출 (이펙트 1개 = execute_python_code 1~3회. 30회 금지)
- ❌ 구조 gate 전 캡처 또는 매 edit마다 스크린샷. 단, 최종 핵심 시점/배경 캡처와 PIE 성능 검증은 생략 금지
- ❌ list_modules/get_module_info 전수 탐색 (팔레트 표 + 레시피에 답 있음)
- ❌ discover_python_class 반복 (levy_vfx/팔레트 사용법은 이 문서에 전부 있음)
- ❌ 문서 4개 이상 Read (레시피 1 + 필요시 NIAGARA_5_7 1)
- ❌ 모듈을 0부터 쌓기 (팔레트에 같은 블록 있으면 copy_emitter)
- ❌ open_asset으로 에디터 열기 (컴파일 검증 실패 시 특히 — INDEX 크래시 규칙)

## 반복 효과는 템플릿 복제
같은 계열 이펙트 2번째부터는 조립도 생략 — 잘 나온 NS를 템플릿 삼아 `spec["template"]`으로 복제 후 값만 변경. 잘 나온 결과물은 `/Game/VFX/_Library/`에 NS_Template_* 로 저장해 재사용.
