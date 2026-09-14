# PLAYBOOK — 이펙트 작업 절차 (이 문서 하나로 진행)

> **작업 소유권**: 사용자가 제작을 요청하면 레비는 `PRODUCTION_PIPELINE.md`의 staging 빌드와 시각·성능 검증까지 담당한다. 사용자가 조합 가이드만 요청한 경우에만 에디터 수동 절차로 전환한다.

## 표준 플로우

1. **질문과 IR** — 용도·타이밍(캐스팅/임팩트/루프), 톤, 색·규모, 플랫폼/동시 인스턴스를 확정하고 `PRODUCTION_PIPELINE.md`의 Effect IR을 작성한다.
2. **시안** — `levy_vfx.concept("<영문 연출 묘사>")` → 응답에 `![시안](경로)` 포함(채팅 인라인 표시) → "이 느낌 맞아요?"
3. **컨펌 후 3종 산출**:

### (a) 레이아웃 설계표
| 레이어 | 역할 | 렌더러 | 핵심 모듈 | 수치 |
형식으로 — 레시피(OBLIVIO_EFFECTS/RECIPES) 수치 기반. 레이어링 공식: 코어 플래시(0.1~0.2s) + 메인 + 여운(느림) + 라이트 1개. SpawnBurst=순간/SpawnRate=지속, 보조 레이어 0.1~0.3s stagger.

### (b) 머티리얼·텍스처 생성 (재료 공급 — 파이썬 1~3회)
- 내장 MI가 맞으면 생성 없이 경로만 안내: MI_LevyGlow/Spark/FireWisp/Runic/Lightning/Smoke (/Revvy/VFX/Materials)
- 변형 필요: `levy_vfx.derive_mi("MI_WindWisp", master="Additive", texture="/Revvy/VFX/Textures/T_Noise_Vector", tint=(0.3,0.9,0.5), emissive=1.5)` → /Game/VFX/Materials에 생성
- 마스터 선택: Additive(글로우·에너지) / Translucent(연기·먼지, soft) / Erosion(스크롤+소멸 — 불·바람·에너지)
- 새 질감 PNG만 codex: `rgt.generate_texture(...)`. 단순 도형·노이즈는 내장 텍스처(/Revvy/VFX/Textures 33종, MANIFEST) 우선.
- 생성한 에셋 경로 전부 보고.

### (c) 조합 가이드 (사용자가 따라할 단계)
번호 목록으로, 에디터 조작 단위로:
1. Content Browser 우클릭 → FX → Niagara System → Empty 선택, 이름 NS_X
2. +Track으로 이미터 추가 → 이름 변경
3. 각 이미터: 모듈 추가(정확한 모듈명 + 스택 위치 — 예: "Emitter Update에 Spawn Burst Instantaneous"), 입력값 수치 그대로
4. 렌더러 → Material에 생성한 MI 경로 지정 (Mesh 렌더러면 메시도)
5. 커브(Scale/Color over Life) 포인트 값
스테이지 규칙: SpawnRate/SpawnBurst/EmitterState=Emitter Update, Initialize Particle/AddVelocity/ShapeLocation=Particle Spawn, Forces/Drag/Solve=Particle Update. 모듈 경로는 NIAGARA_5_7.md 카탈로그(V2 우선).

## 금지
- 구조/컴파일 검증 전 editor 열기 또는 캡처, 매 edit마다 캡처, 기존 원본 덮어쓰기, 실패한 부분 상태 저장, `compile OK`만으로 완료 보고를 금지한다.

## 직접 빌드 모드

## 0. 최단 경로 — 프리셋 1줄 (요청이 6종에 해당하면 무조건 이걸로)

```python
import unreal, sys; sys.path.append(unreal.Paths.project_plugins_dir() + "Revvy/Content/Python")
import levy_vfx
print(levy_vfx.build_preset("impact_hit", "/Game/VFX/NS_HitA"))
# 프리셋: fire / impact_hit / smoke / sonic_wave / dark_aura / slash_trail
# 색 바꾸기: build_preset("fire", path, tint="(0.5,1.5,3.0)")  ← 파란 불
```
프리셋은 빠른 구조 초안이다. 빌드 후 FAIL 라인을 고치고, IR에 없는 lifecycle·curve·renderer binding·Effect Type·bounds·quality 설정을 보완한 뒤 시각·성능 gate를 수행한다.

## 1. 프리셋 밖 효과 — spec 직접 작성 (execute_python_code 1~3회)

```python
print(levy_vfx.build({
    "path": "/Game/VFX/NS_X",
    "blocks": [
        {"from": "EM_BurstSparks", "as": "Sparks",
         "set": [["SpawnBurst_Instantaneous", "SpawnCount", "60"],
                 ["InitializeParticle", "Lifetime", "0.7"]],
         "tint": "(3.0, 2.2, 0.8)", "material": "/Revvy/VFX/Materials/MI_LevySpark"},
    ],
}))
```

⚠️ **모듈명은 공백 없는 실제 함수명** — `"InitializeParticle"` ✓ / `"Initialize Particle"` ✗(매칭 실패). 팔레트 블록의 InitializeParticle 실제 이름은 `InitializeParticle001`이지만 `"InitializeParticle"` substring으로 잡힘.

**팔레트 8블록** (`/Revvy/VFX/_Library/NS_EmitterPalette`): EM_BurstSparks(스파크 burst) · EM_ExpandingRing(메시 링) · EM_RisingWisps(상승 위스프+컬노이즈) · EM_GlowCore(글로우) · EM_GroundDust(먼지) · EM_RibbonTrail(리본) · EM_ElectricArc(전기 리본) · EM_MeshDebris(파편 메시)

**주요 입력명**: `SpawnRate.SpawnRate` / `SpawnBurst_Instantaneous.SpawnCount` / `InitializeParticle.Lifetime` (enum 핀은 표시명 OK — 도구가 변환, 숫자 금지)

## 2. 머티리얼·텍스처 — 만들지 말고 골라 쓰기

| 필요 | 사용 |
|---|---|
| additive 글로우/스파크/불/룬/번개 | `MI_LevyGlow / MI_LevySpark / MI_LevyFireWisp / MI_LevyRunic / MI_LevyLightning` |
| 연기/먼지(translucent+soft) | `MI_LevySmoke` |
| 스크롤+소멸(불·에너지) | `M_LevyVFX_Erosion`에서 MI 파생 (Texture/ErosionMask/PanSpeed) |
| 텍스처 | `/Revvy/VFX/Textures/` 21종 — T_Noise_Vector(만능)/T_Aura_GlowBase/T_Shockwave_Radial/T_Flow_Distortion/T_Gradient_Ramp_V/플립북 2종... (MANIFEST 참조) |

색·밝기 변형 = MI 파라미터(Tint/EmissiveIntensity)나 spec tint. **새 마스터 제작 금지.** 새 텍스처 필요할 때만: 절차적(노드) 우선 → 안 되면 codex(CODEX_PROMPTS).
(마스터/MI 미구축 상태면 1회: `build_vfx_assets.py` 실행)

## 3. 레이어링 공식 (Revvy/CGHow 학습 — 풍성하게 만들 때)

임팩트류 = **코어 플래시(0.1~0.2s, 밝게) + 메인(스파크/링) + 여운(먼지/연기, 느림) + 선택: 라이트 1개**
- 순간(SpawnBurst)과 여운(SpawnRate) 분리. 보조 레이어는 0.1~0.3s 지연(staggered)
- 색은 화이트 피크 → 색상 페이드 (ColorFromCurve 또는 tint 밝게→어둡게 레이어 분담)
- 라이트 렌더러는 마지막 글로우 이미터 1개만

## 4. 톤 (오블리비오 = 호러)

| | Horror(기본) | Action(타격감) |
|---|---|---|
| emissive | <0.5 상시, 임팩트 프레임만 2~5 | 5~12 |
| 채도 | 0.3~0.5 desaturated | 0.85+ |
| 타이밍 | 느림 2~5s, 절제 | snap 0.1~0.4s |

impact엔 FEEL 스택(사운드+셰이크+hit-stop 같은 프레임). ambient엔 셰이크 금지.

## 5. 완료 기준

1. material compile과 `compile_with_results`를 통과하고 Niagara Message Log/editor log의 blocking diagnostic이 없어야 한다. builder의 `compile: OK` 문자열만으로 완료하지 않는다.
2. emitter/module/renderer/parameter 수, material/mesh binding, Effect Type과 bounds를 다시 조회한다.
3. preview map/actor에서 gameplay scale로 실행하고 one-shot은 anticipation/peak/decay, loop는 start/steady/boundary를 관찰한다.
4. 구조 gate 뒤 dark/light 배경과 gameplay camera의 대표 화면을 `ScreenshotService.capture_editor_window`로 남긴다. 매 edit마다 캡처하지는 않지만 최종 시각 증거는 필수다.
5. Niagara Debugger/PIE에서 target quality와 worst-case simultaneous instances를 측정한다. `PERF.md` 예산을 넘으면 수정 후 다시 검증한다.
6. `PRODUCTION_PIPELINE.md`의 완료 증거를 보고한다. 검증하지 못한 항목은 완료가 아니라 미검증으로 표기한다.

컴파일 실패 상태에서는 asset editor를 열지 않는다. Cascade 시스템을 새 효과의 최종 구현으로 직접 사용하지 않는다.

## 6. 트러블슈팅
- **회색/흰 박스만 보임** = 렌더러 머티리얼 미바인딩 또는 머티리얼에 Niagara usage 플래그 없음. levy_vfx가 블록별 기본 MI를 자동 바인딩하니 build() 로그의 `material ...: FAIL` 라인 확인 → set_renderer_property(path, emitter, 0, "Material", MI경로) 재시도. 커스텀 머티리얼 쓸 땐 used_with_niagara_sprites/ribbons/mesh_particles 플래그 필수.
- **Mesh 렌더러가 안 보임** = ParticleMesh 미지정. spec 블록에 "mesh": "/Engine/BasicShapes/Cylinder" 등 지정 (ExpandingRing/MeshDebris는 자동 기본 바인딩됨).

