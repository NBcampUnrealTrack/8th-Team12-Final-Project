# Production Pipeline - Unreal Engine 5.8 Niagara

이 문서는 신규 또는 재구성 Niagara 효과의 기본 제작 파이프라인이다. 신규 효과는 먼저 `AESTHETIC_GRAMMAR.md`의 미학 IR과 `ARCHETYPE_PROFILES.md`의 유형 계약을 작성한다. `FASTBUILD`, 튜토리얼 study와 프리셋은 초안을 빠르게 만드는 수단이며, 이 문서의 구조·런타임·픽셀·미학·유형·성능 게이트를 통과하기 전에는 완성으로 보고하지 않는다.

## 1. 시작 조건

1. 실행 중인 엔진 버전과 Revvy 서비스 시그니처를 조회한다. 이 문서는 UE 5.8 기준이며, live discovery 결과가 문서보다 우선한다.
2. 기존 시스템, 이미터, 렌더러, 머티리얼, Effect Type, user parameter, bounds를 조사한다. 이름이나 경로를 추측하지 않는다.
3. 원본을 직접 덮어쓰지 않는다. `/Game/VFX/_RevvyStaging/<EffectName>` 또는 사용자가 승인한 복제본에서 작업한다.
4. 용도(one-shot/loop/attached/world), 게임플레이 크기(cm), 카메라 거리, 동시 인스턴스 수, 대상 플랫폼과 품질 레벨을 확정한다.
5. 구조 변경 전에 작업 IR을 작성하고, 사용하려는 모듈·renderer property·asset path가 현재 엔진에 존재하는지 검증한다.
6. Effect Type, bounds, renderer binding 등 필수 항목을 live service가 설정할 수 없으면 자동 대체하거나 생략하지 않는다. capability gap과 필요한 수동 단계 또는 API 보완을 기록한다.
7. 표준 validation level, 고정 camera 또는 결정론적 viewport capture가 없으면 시각 승인을 자동 통과시키지 않는다. 가능한 구조/런타임 결과만 보고하고 visual gate는 미검증으로 남긴다.

## 2. Effect IR

작업 중인 효과를 아래 형태의 구조화 데이터로 유지한다. 자유 서술만으로 빌드하지 않는다. 모든 `id`는 재실행 시 같은 대상을 찾는 안정 키이며, 표시 이름의 substring을 식별자로 사용하지 않는다.

```yaml
effect:
  id: impact_fire_heavy
  destination: /Game/VFX/_RevvyStaging/NS_ImpactFireHeavy
  lifecycle: one_shot       # one_shot | loop | attached
  duration_s: 1.2
  scale_cm: 250
  gameplay_camera_m: 8
  platform: Win64
  quality_targets: [Low, High]
  worst_case_instances: 5
  deterministic_seed: 7319

style_brief:
  intent: sharp premium action RPG impact
  primary: white-hot directional impact core
  secondary: [broken shock arc, sparse sparks, low-value dust]
  palette: [white, amber, smoke-gray]
  avoid: [uniform concentric rings, full-frame flash, simultaneous layer peaks]

art_direction:
  gameplay_read: close-range impact from lower-left to upper-right
  archetype: impact
  style_profile: premium_action_rpg
  focal_anchor: weapon_contact
  primary_form: broken_diagonal_wedge
  palette_roles: {core: white, body: amber, residue: smoke_gray}
  depth_plan: [dark_core, translucent_body, emissive_accent]
  symmetry: low
  density: sparse
  shared_motion_field: impact_normal

system:
  effect_type: /Game/VFX/EffectTypes/NET_Impact
  bounds_mode: fixed        # fixed | dynamic
  bounds_local_min: [-180, -180, -40]
  bounds_local_max: [180, 180, 260]
  warmup_s: 0

layers:
  - id: flash
    role: anticipation      # anticipation | core | body | accent | decay
    hierarchy: primary      # primary | secondary
    timing: {start_s: 0.0, duration_s: 0.12, spawn: burst, count: 1}
    simulation: {mode: lightweight, space: local}
    motion: [{type: scale_curve, keys: [[0, 0.1], [0.35, 1.0], [1, 0.0]]}]
    renderer:
      type: sprite
      material: /Game/VFX/Materials/MI_ImpactFlash
      facing: camera
      sort: none
      bindings: {color: Particles.Color, age: Particles.NormalizedAge}
    material_contract: {particle_alpha: true, relative_time: true, dynamic_params: []}

validation:
  capture_times_s: [0.05, 0.18, 0.9]
  backgrounds: [dark, light]
  cameras: [gameplay_front, oblique_45]
  gates: [structural, runtime, pixel, aesthetic, archetype]
  aesthetic_gate: [clear_primary, staggered_timing, clean_decay]
  archetype_gate: [directional_peak, contact_response]
  max_gt_ms: 1.0
  max_gpu_ms: 5.0
```

IR에는 최소한 다음을 기록한다.

- 각 레이어의 역할, 시작 시간, 수명, burst/rate, 반복 여부;
- simulation 방식, local/world space, determinism, 필요한 gameplay/DI 입력;
- spawn shape, 두 개 이상의 독립적인 motion source가 필요한지 여부;
- renderer 종류, asset, facing/sort/UV/tessellation과 attribute binding;
- size/color/alpha/dynamic material parameter curve;
- `style_brief`의 의도, 하나의 Primary, 이를 보조하는 Secondary, palette와 금지할 형태;
- Effect Type, bounds, 품질 레벨별 비활성화 또는 spawn scale;
- 검증 시간, 배경, 카메라 거리, 동시 인스턴스와 성능 예산.
- archetype, focal anchor, Primary form, palette role, shared motion field, symmetry/density, 피해야 할 형태;
- 후보 ID와 변경 가설, 다섯 gate의 결과, 최종 승인 여부.

빌드 후에는 실제로 해석된 모듈 경로, 엔진 버전, 생성 asset path를 IR manifest에 다시 기록한다.

현재 mutator가 stable node ID를 받지 않는 경우에는 stage와 전체 function name으로 조회해 일치 항목이 정확히 하나인지 확인한다. 동일 이름 모듈이 둘 이상이면 substring으로 첫 항목을 수정하지 말고 자동 편집을 중단한다.

### 실제 프로덕션 빌드와 감사 API

내장 production preset은 기본형 `combat_impact`, `arcane_burst`와 template-first 폴리싱형 `combat_impact_polished`, `arcane_burst_polished`가 있다. 신규 결과물과 시각 폴리싱에는 `_polished`를 우선하고, 기본형은 호환성 및 구조 비교용으로 유지한다. `build_production_preset`은 compile과 구조 품질 gate를 통과한 경우에만 저장하며 문자열 로그 마지막에 `RESULT: PASS` 또는 `RESULT: FAIL`을 반환한다.

```python
import json
import sys
import unreal

sys.path.append(unreal.Paths.project_plugins_dir() + "Revvy/Content/Python")
import levy_vfx

path = "/Game/VFX/_RevvyStaging/NS_CombatImpact_Polished"
build_log = levy_vfx.build_production_preset(
    "combat_impact_polished",
    path,
    tint="(3.5, 1.8, 0.5)",
    overrides={"quality_threshold": 82, "warnings_as_errors": True},
)
print(build_log)
if "simulation_failed" not in build_log:
    assert build_log.rstrip().endswith("RESULT: PASS"), build_log

audit = json.loads(levy_vfx.audit_system_json(
    path,
    phases=["anticipation", "emission", "body", "impact", "decay"],
    profile="combat",
    threshold=82,
))
assert audit["passed"], audit["issues"]
```

그래프 추가/삭제 직후 같은 Python 실행 프레임에서는 GPU Sim Cache가 비어 있을 수 있다. 빌드 로그에 `simulation_failed`와 `NEXT:`가 나오면 다음 Unreal 도구 호출에서 `levy_vfx.finalize_production_preset("combat_impact_polished", path)`를 실행한다. 이 함수는 시작 직후의 일시적 컴파일 실패를 한 번 재시도하고, 런타임 gate가 통과한 경우에만 저장한다.

```python
# Run this in the next Unreal tool invocation, not in the graph-changing call above.
final_log = levy_vfx.finalize_production_preset("combat_impact_polished", path)
print(final_log)
assert final_log.rstrip().endswith("RESULT: PASS"), final_log
```

`audit_system_json(path, phases=None, profile="combat", threshold=78)`은 JSON 문자열을 반환한다. `json.loads` 후 `passed`, `score`, `issues`, `blockers`, `observation`을 검토한다. 이 감사는 구조 gate이며 viewport capture, art gate와 PIE 성능 측정을 대체하지 않는다. `RESULT: PASS`만으로 효과가 "예쁘다"고 판정하지 않는다.

### Tutorial-free 제작 폐루프

신규 또는 시각 폴리싱 작업은 다음 순서로 진행한다.

1. gameplay intent를 기계 판독 가능한 aesthetic IR로 만든다.
2. archetype profile과 local template/material contract를 선택한다.
3. 같은 IR에서 shape, timing, material 중 최대 두 축만 달리한 후보 A/B/C를 staging에 만든다.
4. 동일 seed, camera, background, capture time으로 validation level에서 캡처한다.
5. structural, runtime, pixel, aesthetic, archetype gate를 각각 기록한다.
6. `CRITIQUE_AND_REPAIR.md`로 한 원인 축만 수정하고 같은 조건으로 다시 캡처한다.
7. 명시적으로 승인된 후보만 final destination과 positive exemplar로 승격한다.

CGHOW를 포함한 tutorial study의 emitter 순서는 분석 자료일 뿐 production art direction이 아니다. `MI_RevvyTutorial_*` 같은 generic tutorial material은 graph, timing, runtime을 확인하는 prototype에서만 허용한다. 최종 후보는 고유 texture/mesh UV/renderer alignment/material curve/dynamic parameter 계약을 검증해야 한다.

## 3. 레이어와 모션

레이어는 시각 요소 개수가 아니라 서로 다른 시간·운동 책임으로 나눈다.

`style_brief`마다 시선이 먼저 머무는 Primary를 하나만 정한다. Primary는 peak의 가장 높은 value, 선명한 silhouette와 강한 방향성을 담당한다. Secondary는 더 낮은 value/opacity, 작은 면적과 다른 속도로 Primary의 방향과 여운을 보조하며, 동시에 같은 크기와 밝기로 peak하지 않는다.

| 역할 | 일반적인 책임 | 검사 항목 |
|---|---|---|
| Anticipation | 짧은 응축, 흡입, 선행 glow | peak 전에 읽히며 본체를 가리지 않는가 |
| Core | 가장 빠른 flash, ring, 충돌점 | 1~3 프레임 안에 명확한 초점이 생기는가 |
| Body | 주 실루엣과 방향성 | velocity/shape/noise 중 둘 이상의 motion source가 있는가 |
| Accent | spark, shard, ribbon, secondary mesh | 본체와 속도·크기·renderer가 구분되는가 |
| Decay | smoke, dust, ember, dissolve | 갑자기 pop하지 않고 alpha/size가 0으로 끝나는가 |

동일한 simulation과 수명을 공유하면 새 emitter보다 spawn grouping, mesh array, visibility tag 또는 다중 renderer를 먼저 검토한다. emitter마다 고정 CPU 오버헤드가 있으므로 단순한 색·mesh 차이만으로 emitter를 복제하지 않는다.

### Template-first 폴리싱

1. 프로젝트의 기존 Niagara System/Emitter를 먼저 조회하고, 실제 asset path와 emitter name을 확인한다.
2. 의도에 맞는 emitter가 있으면 검증된 spawn, curve, renderer, material binding을 template로 복제한다. source system/emitter 경로와 보존한 renderer 계약을 manifest에 기록한다.
3. 필요한 timing, tint, scale만 좁게 override한다. renderer나 material을 바꿀 때는 기존 fade, alignment, SubUV, dynamic parameter 계약을 잃지 않았는지 다시 감사한다.
4. template가 없거나 현재 엔진에서 복제할 수 없을 때만 검증된 fallback emitter 또는 새 emitter를 사용한다. 완성된 폭발 그림 한 장을 sprite로 넣어 레이어 의미를 중복시키지 않는다.
5. generic tutorial material로 renderer를 통일하지 않는다. 역할별 material contract가 없으면 구조 prototype으로 표시하고 aesthetic gate에 올리지 않는다.

one-shot의 모든 burst를 `0.0s`에 겹치지 않는다. anticipation, Primary peak, shockwave/arc, spark/debris, decay를 짧게 stagger하고 비선형으로 배치한다. 이미터가 3개 이상이면 최소 3개의 구분 가능한 시작 시점을 확보하며, `SpawnRate`는 지속 방출이나 loop 의도가 있을 때만 사용한다.

## 4. Simulation 선택

### Lightweight emitter

UE 5.8의 Lightweight(Stateless) emitter는 tick과 VM 오버헤드를 줄이는 제한된 기능 집합이다. 다음 조건을 모두 만족할 때 우선 후보로 둔다.

- spawn, shape, velocity, drag/gravity, color/size 같은 지원 모듈만으로 표현 가능;
- 프레임 간 particle state, event, collision, scratch HLSL, 복잡한 data interface나 현재 프레임 gameplay sampling이 필요 없음;
- 많은 시스템/이미터 인스턴스에서 같은 단순 동작을 반복함.

현재 editor의 Lightweight module 목록에서 IR을 전부 표현할 수 있는지 먼저 확인한다. 하나라도 지원되지 않으면 regular emitter로 전환하고, 외형을 억지로 축소하지 않는다. regular 버전과 동일한 배치 수로 Niagara Debugger의 Game Thread Average를 비교해 선택 근거를 남긴다.

### Regular CPU/GPU emitter

- CPU: 소수 particle, CPU 전용 DI, gameplay readback, event 또는 정밀한 상호작용이 필요할 때 후보.
- GPU: 많은 particle의 독립 simulation에 후보. GPU 지원 DI인지 확인하고 target hardware에서 측정한다.
- CPU/GPU 선택은 particle 수만으로 확정하지 않는다. 프로젝트가 CPU-bound인지 GPU-bound인지, target hardware에서 어느 쪽이 실제로 빠른지 비교한다.
- warmup은 각 frame을 순차 평가해 hitch를 만들 수 있으므로 기본값은 0이다. 즉시 채워진 loop가 꼭 필요하면 spawn 설계나 Sim Cache를 검토한다.

## 5. Renderer와 머티리얼 계약

Renderer를 추가한 뒤 asset path만 확인하지 말고 다음 계약을 검사한다.

| Renderer | 필수 확인 |
|---|---|
| Sprite | Niagara Sprite usage, facing/alignment, pivot, sub-image, 필요한 경우에만 sort, color/alpha/age binding |
| Mesh | Particle Mesh와 material slot, Niagara Mesh Particles usage, orientation/scale binding, 낮은 triangle 수, 불필요한 collision/shadow/distance field/UV 제거 |
| Ribbon | Ribbon ID/link order, width, facing, UV tiling, tessellation, dynamic material binding, 끝 alpha 0 |
| Light | 실제 필요한 소수 particle만 사용, radius/intensity/volumetric binding, Low tier 비활성화 검토 |
| Decal/Component | 현재 Revvy service가 지원하는지 live signature로 확인; 미지원이면 자동 대체하지 말고 capability failure로 보고 |

머티리얼은 다음을 만족해야 한다.

1. blend mode와 renderer usage flag가 renderer와 일치한다.
2. Translucent는 `ParticleColor.A` 또는 명시적 alpha binding을 Opacity에 사용하고, 교차면에는 Depth Fade를 검토한다.
3. Additive도 수명 끝에서 RGB 또는 opacity가 0이 되도록 particle alpha/age/dynamic parameter를 실제 출력에 소비한다.
4. `Particles.DynamicMaterialParameter[0..3]`을 쓰면 Niagara module, renderer binding, material channel의 세 곳이 같은 의미를 가진다.
5. transient layer는 `ScaleColor`/`FloatFromCurve` 또는 머티리얼의 `ParticleRelativeTime` 기반 envelope 중 하나 이상의 temporal fade 계약을 가진다. 상수 alpha 상태로 수명 끝에서 사라지는 hard pop은 실패다.
6. 방향성 spark/streak sprite는 velocity-aligned인지 확인하고, mesh/ribbon은 해당 orientation 또는 link order를 검증한다.
7. 머티리얼에 baked bloom, 검은/흰 사각 배경, 사용하지 않는 texture sample과 고비용 분기를 남기지 않는다.

## 6. Effect Type, 품질과 Bounds

모든 shipping 시스템에 ambient, impact, hero 등 프로젝트 공용 `UNiagaraEffectType`을 지정한다. Effect Type은 그룹 단위 culling과 per-platform scalability의 기준이며 validation rule도 가질 수 있다.

품질 레벨별로 다음을 명시한다.

- cull distance와 max instance 정책;
- spawn count/rate scale;
- 비싼 accent emitter, Light renderer, sort 또는 collision의 비활성화;
- Low와 High에서 유지되어야 하는 core silhouette.

고정 bounds는 local/attached 또는 제한된 공간 안의 효과에 우선 사용한다. peak와 decay 전체 particle 위치를 측정한 box에 여유를 추가하고, light/dark 배경과 두 각도에서 잘림이 없는지 확인한다. 긴 bullet trace처럼 작은 시각 요소가 넓은 공간을 이동하여 고정 box가 과도하게 커지는 경우에는 dynamic bounds가 더 적절할 수 있다. GPU emitter는 bounds가 잘못되면 효과 전체가 조기 cull될 수 있으므로 bounds 증거 없이 완료하지 않는다.

## 7. GPT Image 텍스처 품질 게이트

기존 `/Revvy/VFX/Textures`, material node, procedural noise/gradient로 만들 수 없는 고유 shape에만 GPT Image를 사용한다. 법선, flow/vector field, 정확한 LUT처럼 수학적 의미가 필요한 데이터는 생성 이미지로 대체하지 않는다.

1. 용도와 채널을 먼저 고정한다: `color`, `opacity mask`, `erosion`, `flipbook`, `decal` 중 하나.
2. 외부 Revvy MCP의 `generate_vfx_texture`를 우선 사용한다. 이 도구는 로컬 Codex/ChatGPT 로그인을 사용하고 API key를 요구하지 않으며, PNG 품질 gate 통과 뒤 Unreal이 실행 중이면 import와 texture 설정까지 수행한다.

MCP tool call 예제:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "generate_vfx_texture",
    "arguments": {
      "subject": "single directional amber impact spark with a white-hot core",
      "usage": "additive",
      "style": "sharp premium action RPG combat VFX",
      "destination_path": "/Game/VFX/_RevvyStaging/Textures",
      "asset_name": "T_CombatImpactSpark",
      "size": 1024,
      "grid_columns": 1,
      "grid_rows": 1,
      "quality_threshold": 82,
      "attempts": 3,
      "replace_existing": false
    }
  }
}
```

허용 `usage`는 `sprite`, `additive`, `mask`, `tileable_noise`, `flowmap`, `normal`, `flipbook`이다. 반환값에서 `success`, `quality.passed`, `quality.score`, `source_png`, `asset_path`, `imported`, `configured`를 확인한다. Unreal이 꺼져 있으면 QA를 통과한 PNG만 staging되고 `imported=false`이므로 asset 사용 준비가 끝난 것이 아니다.
3. additive texture는 순수 검정 배경, alpha texture는 투명 배경을 요구한다. 글자, 프레임, 그림자, baked bloom, ambient scene, 잘린 외곽을 금지한다.
4. import 전에 원본을 검사한다: 중심/여백, alpha fringe, 밝기 범위, 단일 silhouette, 2x2 tile seam, channel contamination, flipbook cell 경계.
5. Unreal import 뒤 color texture만 sRGB를 켠다. mask/noise/flow/normal 데이터는 sRGB를 끄고 용도에 맞는 compression을 지정한다.
6. material preview와 실제 Niagara renderer에서 dark/light 배경을 모두 확인한다. 하나라도 실패하면 import한 결과를 사용하지 않고 prompt 또는 source를 수정한다.
7. 승인된 PNG, prompt, model/output 설정, Unreal texture path를 manifest에 기록한다.

## 8. GeometryScript 절차 메시

단순 ring, disc, shard, cone 같은 VFX mesh는 기존 asset을 먼저 찾고, 없을 때만 GeometryScript로 staging asset을 만든다. `levy_vfx.build_procedural_mesh(recipe)`는 검증된 recipe를 현재 Revvy GeometryScriptService로 실행하고 transient handle을 항상 정리한다.

```python
import unreal
import sys

sys.path.append(unreal.Paths.project_plugins_dir() + "Revvy/Content/Python")
import levy_vfx

result = levy_vfx.build_procedural_mesh({
    "path": "/Game/VFX/_RevvyStaging/Meshes/SM_ImpactRing",
    "replace_existing": False,
    "parts": [
        {
            "type": "torus",
            "operation": "append",
            "major_radius": 100.0,
            "minor_radius": 8.0,
            "major_steps": 48,
            "minor_steps": 8,
            "location": [0, 0, 0],
            "rotation": [0, 0, 0],
            "scale": [1, 1, 1]
        }
    ]
})
assert result["success"], result
print(result["asset_path"], result["reused"], result["operations"])
```

허용 primitive는 `box`, `sphere`, `cylinder`, `torus`, `static_mesh`이고 operation은 첫 part의 `append`, 이후 part의 `append`, `subtract`, `intersect`다. 기존 asset이 있고 `replace_existing=false`이면 새 geometry를 쓰지 않고 `reused=true`를 반환한다.

Export는 기존 asset 내용을 바꿀 수 있으므로 승인되지 않은 destination을 재사용하지 않는다. export 후 다음을 검증한다.

- pivot/origin과 Niagara local axis가 의도한 방향인지;
- normals/winding, bounds, material slot, UV0가 유효한지;
- screen size에 필요한 최소 segment 수인지, 불필요하게 고밀도인지;
- particle mesh용 collision, shadow, distance field와 여분 UV가 꺼졌는지;
- mesh renderer material에 Niagara Mesh Particles usage가 켜졌는지.

UV/normal/collision 후처리가 현재 GeometryScriptService 범위를 넘으면 해당 전용 service/skill을 사용하고 결과를 다시 조회한다. transient handle은 성공/실패와 관계없이 정리한다.

## 9. 구조 게이트와 시각 검증

1. material을 먼저 compile하고 오류를 제거한다.
2. Niagara를 `compile_with_results`로 검사하고 Niagara Message Log와 editor log의 compiler diagnostic도 확인한다. 현재 service 결과가 실제 warning 본문을 모두 수집하지 못할 수 있으므로 builder의 `compile: OK`나 readiness만 성공 증거로 사용하지 않는다.
3. emitter/module/renderer/parameter 수와 실제 asset binding을 다시 조회한다. 미완성 상태에서는 editor asset을 열지 않는다.
4. `/Game/VFX/Validation/Maps/L_RevvyVFXValidation` 또는 검증된 동등 level/actor에서 gameplay scale과 카메라 거리로 spawn한다. level이 없으면 visual gate를 미검증으로 보고한다.
5. one-shot은 anticipation, peak, decay의 최소 3시점에서 관찰한다. loop는 start, steady state, loop boundary를 본다.
6. dark/light 배경, 정면과 사선, gameplay camera에서 framing, intersection, pop, silhouette, alpha decay를 확인한다.
7. 구조 gate 통과 뒤 Functional Screenshot/Automation Comparison 또는 검증된 offscreen viewport capture로 UI 없는 대표 증거를 캡처한다. `ScreenshotService.capture_editor_window(...)`는 수동 UI 상태 기록일 뿐 pixel regression과 최종 art evidence로 사용하지 않는다.
8. 실패 원인을 spawn, velocity, size, alpha, material, bounds 중 하나에 연결해 수정한다. 무근거로 emitter를 추가하지 않는다.

gate는 다음을 섞지 않고 순서대로 통과한다. 상세 기준은 `VISUAL_VALIDATION.md`를 따른다.

1. **Structural**: compile, diagnostics, asset binding, material/mesh contract.
2. **Runtime**: sampled particle/size/alpha, bounds, lifecycle/loop evidence.
3. **Pixel**: effect ROI, clipping, overexposure, rectangle, hard pop, loop frame jump.
4. **Aesthetic**: Primary/Secondary hierarchy, negative space, shape, timing, material depth.
5. **Archetype**: Impact 방향, Portal 내부/경계, Tornado taper/회전, Splash 접촉/탄도처럼 유형 고유의 gameplay read.

구조 및 런타임 gate 뒤 별도의 aesthetic gate를 통과해야 한다.

- peak에서 Primary가 즉시 읽히고 Secondary가 초점과 실루엣을 가리지 않는다.
- anticipation, peak, follow-through, decay가 같은 프레임에 겹치지 않으며 최소 3개의 구분 가능한 시점이 보인다.
- scale만 커지는 레이어에 의존하지 않고 velocity, curl/noise, rotation, ribbon 또는 비대칭 mesh motion이 읽힌다.
- 완전한 동심원, 균일한 방사형 복제, 과도한 흰색 core와 불필요한 particle clutter가 주 형태가 아니다.
- 모든 transient layer가 시간에 따라 감쇠하고 마지막 capture에서 hard pop이나 잔상 사각형이 없다.
- dark/light 배경과 gameplay camera의 anticipation/peak/decay 캡처를 나란히 비교해 `style_brief`의 Primary/Secondary 의도가 유지된다.
- generic tutorial material이 최종 renderer에 남아 있지 않으며, 남아 있다면 명시적으로 승인된 고유 contract라는 증거가 있다.

## 10. Sim Cache와 성능 검증

Sim Cache는 실행 중 여러 frame의 simulation data를 기록해 재생하거나 debug할 때 사용한다. 반복 가능한 cinematic review, scrub, 승인 비교 또는 비싼 안정 simulation에 적합하다. gameplay 입력에 계속 반응해야 하는 효과의 기본 실행 경로로 쓰지 않는다.

- capture 방법: Niagara System Editor Baker, Sequencer Niagara Component Cache track, 또는 `CaptureNiagaraSimCache` Blueprint 함수;
- capture 뒤 cache가 유효하고 frame/duration/emitter가 존재하는지 확인한다;
- 필요한 attribute/DI가 capture 설정에 포함됐는지 확인한다;
- 원본 simulation과 cache playback의 시작, peak, 끝 frame을 비교한다.

성능 승인은 PIE에서 target quality와 worst-case simultaneous instance count로 수행한다.

1. `Tools > Debug > Niagara Debugger`에서 Debug HUD, FX Outliner, Performance를 사용한다.
2. system/emitter instance 수, active particle 수, Game Thread Average, render/GPU 비용을 기록한다.
3. `stat unit`, `stat niagara`, `stat gpu`와 필요 시 Unreal Insights/ProfileGPU로 병목을 확인한다.
4. Shader Complexity와 Quad Overdraw view mode로 translucent overlap을 검사한다.
5. Low와 High를 비교해 culling, spawn scaling, emitter/renderer disable이 실제로 적용되는지 확인한다.
6. 예산을 넘으면 emitter 통합, Lightweight 전환, spawn/size/overlap 감소, sort 제거, Effect Type/cull/bounds 조정 후 다시 측정한다.

## 11. 완료 증거

다음이 모두 있어야 완료로 보고한다.

- engine version, Effect IR/manifest, 최종 Niagara/material/texture/mesh/Effect Type 경로;
- material 및 Niagara compile 결과와 blocking diagnostic 0;
- emitter/module/renderer/user parameter 수와 material/mesh binding 확인;
- fixed/dynamic bounds 선택 근거와 peak/decay에서 잘림 없음;
- anticipation/peak/decay 또는 loop 3시점의 시각 캡처, dark/light 및 gameplay camera 확인;
- structural/runtime/pixel/aesthetic/archetype gate를 분리한 판정과 raw evidence;
- `style_brief` 및 aesthetic IR 대비 Primary/Secondary hierarchy, staggered timing, motion variety와 clean decay의 aesthetic gate 판정;
- archetype-specific gameplay read 판정, 후보 A/B/C 비교와 최종 승인 근거;
- 재사용한 template의 source system/emitter 경로와 보존 또는 변경한 renderer/material 계약;
- target quality, worst-case instance 수, Debugger/PIE의 GT/GPU 측정치;
- Lightweight 또는 CPU/GPU 선택 근거, 사용했다면 Sim Cache 경로와 유효성;
- 생성 텍스처의 source PNG/prompt/import 설정과 절차 mesh 검증 결과.

gate를 통과한 staging asset만 최종 destination으로 승격한다. 검증하지 못한 항목은 완료가 아니라 미검증으로 명시한다.

## 공식 참고 자료

- [Niagara Scalability and Best Practices (UE 5.8)](https://dev.epicgames.com/documentation/unreal-engine/scalability-and-best-practices-for-niagara)
- [UNiagaraEffectType API (UE 5.8)](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/Niagara/UNiagaraEffectType)
- [Niagara Lightweight Emitters Overview](https://dev.epicgames.com/documentation/unreal-engine/niagara-lightweight-emitters-overview)
- [Lightweight Emitters Quick Start](https://dev.epicgames.com/documentation/unreal-engine/lightweight-emitters-quick-start-for-niagara-in-unreal-engine)
- [Niagara Renderer Reference (UE 5.8)](https://dev.epicgames.com/documentation/unreal-engine/render-module-reference-for-niagara-effects-in-unreal-engine)
- [Niagara Emitter Settings Reference (UE 5.8)](https://dev.epicgames.com/documentation/unreal-engine/emitter-settings-reference-for-niagara-effects-in-unreal-engine)
- [Niagara Debugger (UE 5.8)](https://dev.epicgames.com/documentation/unreal-engine/niagara-debugger-for-unreal-engine)
- [Measuring Performance in Niagara (UE 5.8)](https://dev.epicgames.com/documentation/unreal-engine/measuring-performance-in-niagara)
- [UNiagaraSimCache API (UE 5.8)](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/Niagara/UNiagaraSimCache)
- [Screenshot Comparison Tool (UE 5.8)](https://dev.epicgames.com/documentation/unreal-engine/screenshot-comparison-tool-in-unreal-engine)
- [Automation Test Framework (UE 5.8)](https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-test-framework-in-unreal-engine)
- [Geometry Scripting Reference (UE 5.8)](https://dev.epicgames.com/documentation/en-us/unreal-engine/geometry-scripting-reference-in-unreal-engine)
- [OpenAI Images API Reference](https://developers.openai.com/api/reference/resources/images)
