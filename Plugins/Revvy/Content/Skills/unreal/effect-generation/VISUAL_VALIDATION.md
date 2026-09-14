# Visual Validation

구조 점수는 미감을 증명하지 않는다. VFX 승인은 다음 gate를 순서대로 통과해야 하며, 각 결과를 별도로 기록한다.

## Gate 모델

| Gate | 질문 | Hard failure 예시 | 증거 |
|---|---|---|---|
| Structural | asset과 graph가 유효한가 | compile error, missing material/mesh, 잘못된 binding | compile/diagnostic, asset inspection |
| Runtime | 실제 frame에 유효한 입자가 존재하는가 | zero particle/size/alpha, bounds 이탈, loop dead emitter | SimCache/runtime sample |
| Pixel | 화면에 artifact 없이 올바른 범위로 보이는가 | clipping, white-out, billboard rectangle, hard pop | 결정론적 image sequence와 metric |
| Aesthetic | 의도한 hierarchy, 형태, 리듬이 보이는가 | 초점 다중화, negative space 소실, 균일 동심원 지배 | montage, metric, rubric critique |
| Archetype | 해당 유형의 gameplay 의미가 읽히는가 | Impact 방향 부재, Portal 내부 부재, Tornado taper 부재 | profile-specific metric와 side view |

성능은 위 다섯 gate 이후의 release gate다. 성능 통과가 미학 실패를 상쇄하지 않으며, 미학 통과가 예산 초과를 허용하지 않는다.

## 표준 검증 레벨 계약

구현된 표준 경로는 `/Game/VFX/Validation/Maps/L_RevvyVFXValidation`이다. level 또는 필요한 actor/service가 없으면 자동 성공으로 처리하지 말고 `visual_validation_unavailable`로 보고한다.

레벨에는 다음이 있어야 한다.

- 고정 perspective camera와 gameplay 거리별 camera marker;
- 180 cm 캐릭터 또는 명시적인 100 cm scale reference;
- 지면과 수직 벽, contact marker;
- dark, 18% gray, light 배경을 전환하는 validation rig;
- 고정 exposure와 명시적인 post-process profile;
- effect 하나만 활성화하는 Niagara component;
- 동일 위치에 effect가 없는 blank capture 상태;
- stress 검증용 1/5/20 instance 배치 모드.

현재 표준 level의 pixel gate capture는 `VFXLab_SceneCapture` (`SceneCapture2D`)를 사용한다. 검증된 기본 계약은 1024x1024 `TextureRenderTarget2D`, `SCS_FINAL_COLOR_LDR`, `Capture Every Frame=false`, `Capture On Movement=false`, FOV 42다. effect별 `CAM_*`의 transform/FOV를 복사한 뒤 명시적으로 `capture_scene()`을 호출한다.

에디터 UI 확대 상태나 asset editor preview는 보조 진단일 뿐 gameplay-scale 승인 증거가 아니다.

## 결정론적 재생

1. Effect IR의 `deterministic_seed`, duration, capture time을 읽는다.
2. component를 reset하고 같은 random seed를 적용한다.
3. 고정 `1/60s` delta로 원하는 시점까지 advance한다.
4. GPU 결과가 기기별로 흔들리면 승인된 SimCache를 재생하거나 RHI별 baseline을 둔다.
5. 각 capture 전에 blank scene과 camera/post-process 상태가 같은지 확인한다.

C++ 검증 actor를 사용할 때 `UNiagaraComponent::SetRandomSeedOffset`, `ReinitializeSystem`, `AdvanceSimulation` 또는 검증된 동일 기능을 사용한다. Python/service signature는 실행 중인 엔진에서 먼저 조회한다.

### Prepare, tick, capture

Niagara visibility, desired age, backdrop 또는 environment를 바꾼 같은 Python invocation에서 바로 render target을 export하지 않는다. 준비 변경이 renderer에 반영되기 전에 이전 frame이 저장될 수 있다.

1. Unreal tool call A에서 `revvy_vfx_lab.prepare_blank(...)` 또는 `prepare_frame(...)`만 실행한다.
2. tool call을 종료해 editor에 제어를 돌려주고 **최소 1 editor tick**을 기다린다. GPU Niagara나 material update가 있으면 유효한 새 frame이 확인될 때까지 더 기다린다.
3. 별도 Unreal tool call B에서 `VFXLab_SceneCapture`를 대상 `CAM_*`와 정렬하고 `SceneCaptureComponent2D.capture_scene()`을 호출한다.
4. 같은 call에서 `unreal.RenderingLibrary.export_render_target(world, render_target, directory, filename)`으로 PNG를 export한다.
5. export된 파일이 존재하고 예상 resolution인지 확인한 뒤 다음 상태를 prepare한다.

`prepare_* -> capture_scene -> export`를 한 blocking Python call에 합치거나 `sleep`으로 editor tick을 대신하지 않는다. 서로 다른 Unreal tool invocation 사이의 실제 editor tick이 계약이다.

## Blank/effect pair contract

host-side pixel 분석은 matching blank/effect 두 장이 모두 있어야 한다. 한 pair에서는 다음 값이 완전히 같아야 한다.

- effect key가 선택하는 camera, camera transform/FOV/projection;
- render target object, resolution, capture source와 color format;
- backdrop, `presentation|neutral` environment, lighting, post-process와 exposure;
- engine/RHI/quality level, frame resolution과 ROI;
- VFX 외 actor의 visibility와 world state.

두 장 사이에서 달라지는 것은 대상 Niagara actor/component의 visibility와 effect frame뿐이다. blank에서는 모든 Niagara actor를 숨기고, effect에서는 하나의 target만 같은 위치에서 활성화한다. blank는 effect의 다른 phase나 검은 이미지로 대체할 수 없다.

권장 filename pair:

```text
impact_blank_dark_neutral_rt_v01.png
impact_peak_dark_neutral_rt_v01.png
```

pair마다 `effect_key`, phase/age, seed, camera, backdrop, environment, RHI, quality와 capture timestamp를 manifest에 함께 기록한다.

## Capture matrix

one-shot 최소 세트:

- `anticipation`, `peak`, `follow_through`, `decay`, `end`;
- dark와 light 배경;
- gameplay front와 45도 oblique camera;
- diagnostic mode와 presentation mode.

loop 최소 세트:

- `start_fill`, `steady_a`, `steady_b`, `boundary_before`, `boundary_after`;
- front와 side camera;
- dark/light 배경 중 effect 성격에 필요한 둘 이상.

capture time은 IR에 초 단위와 normalized age를 함께 기록한다. 자동 선택한 peak가 의도한 Primary peak와 다른 경우 IR을 수정한다.

## 두 가지 capture mode

### Diagnostic

- noisy rendering feature, motion blur, eye adaptation을 끈다.
- 고정 exposure와 resolution을 사용한다.
- background blank 차분으로 effect ROI를 계산한다.
- 형태, alpha, clipping, 시간 에너지, 회귀 비교에 사용한다.

### Presentation

- 실제 프로젝트의 승인된 post-process와 bloom을 사용한다.
- gameplay camera에서 최종 value, palette, environment integration을 판단한다.
- diagnostic 결과를 대체하지 않는다.

`SceneCapture2D -> TextureRenderTarget2D -> export_render_target`가 pixel metric용 우선 capture 경로다. active editor viewport나 `ScreenshotService.capture_editor_window`는 camera framing, UI, OS window와 focus 상태에 의존하므로 최종 pixel gate 또는 regression oracle로 사용하지 않는다. 수동 상태 기록에만 사용할 수 있다. Automation Screenshot Comparison은 승인 baseline의 회귀 비교에 별도로 사용할 수 있다.

## Pixel metric

blank capture와 effect capture의 차이로 ROI mask를 만들고 다음을 측정한다. threshold는 archetype profile과 승인된 exemplar로 조정하며 전역 절대값으로 고정하지 않는다.

- `screen_coverage`: gameplay frame에서 effect가 차지하는 비율;
- `edge_contact`: 의도 없이 image border에 닿는 ROI 비율;
- `clipped_highlight_ratio`: presentation capture의 과다 노출 면적;
- `salient_component_count`: 비슷한 밝기의 경쟁 초점 수;
- `primary_energy_share`: 가장 중요한 영역의 weighted luminance 비중;
- `negative_space_ratio`: Primary 주변의 조용한 영역;
- `circular_symmetry`: 완전 동심원 또는 균일 radial 반복 정도;
- `rectangular_artifact_score`: sprite card 외곽 또는 검은/흰 사각형;
- `palette_deviation`: IR palette role에서 벗어난 dominant hue;
- `temporal_energy_curve`: frame별 ROI luminance/area;
- `end_to_peak_energy`: one-shot 종료 프레임과 peak 에너지 비율;
- `frame_jump`: 인접 frame의 비정상적 소실 또는 loop 경계 차이.

초기 hard gate는 clipping, invalid rectangle, end hard-pop, effect 완전 비가시, test frame 이탈에만 둔다. hierarchy, symmetry, palette는 profile-aware score와 critique로 판단한다.

## 구현된 host-side 분석기

`Plugins/Revvy/Content/Python/revvy_vfx_visual_quality.py`는 Unreal 의존성 없이 Pillow로 matching blank/effect PNG를 비교한다. 현재 `changed_coverage`, bounding box/fill, edge contact, white clipping, luminance percentile/center, connected components와 bilateral symmetry를 JSON으로 반환한다.

PowerShell 실행 예:

```powershell
python Plugins/Revvy/Content/Python/revvy_vfx_visual_quality.py `
  "Saved/Revvy/Validation/VFXLab/impact_peak_dark_neutral_rt_v01.png" `
  --blank "Saved/Revvy/Validation/VFXLab/impact_blank_dark_neutral_rt_v01.png" `
  --output "Saved/Revvy/Validation/VFXLab/impact_peak_dark_neutral_rt_v01.quality.json" `
  --strict
```

`--strict`는 report에 error가 있으면 exit code 1, 입력이나 Pillow 문제가 있으면 exit code 2를 반환한다. 필요할 때만 `--roi left,top,right,bottom`, `--difference-threshold`와 `--edge-margin`을 manifest에 명시해 사용한다. 서로 다른 pair에 threshold를 임의로 바꾸어 통과시키지 않는다.

분석기의 hard error는 effect 비가시, ROI 0.2% 미만, edge clipping이다. white clipping 3% 초과, 과도한 fragmentation과 양축 symmetry는 warning이며 aesthetic/archetype review 없이 단독 실패 또는 승인 근거로 사용하지 않는다.

## Aesthetic rubric

각 항목을 0-4로 평가하고 근거가 되는 frame과 ROI를 기록한다.

- gameplay readability;
- Primary/Secondary hierarchy;
- shape coherence와 negative space;
- temporal contrast와 decay;
- motion coherence;
- material depth;
- palette/value discipline;
- environment integration.

점수만 저장하지 말고 `observed`, `expected`, `delta`, `suspected_cause`를 남긴다. vision model 평가는 두 번 이상 일치한 항목만 repair 후보로 사용하며 hard gate를 단독으로 결정하지 않는다.

## Archetype gate

`ARCHETYPE_PROFILES.md`에서 해당 유형의 check를 가져온다. 예:

- Impact: peak 방향성, contact centroid, delayed ground response;
- Portal: interior/background 대비, boundary continuity, side silhouette, loop seam;
- Tornado: height별 radius 변화, angular flow, vertical lift, loop dead zone;
- Splash: ground contact, upward/downward 탄도, delayed ripple;
- Trail: velocity alignment, width taper, path continuity.

## Visual regression

1. 새 효과는 objective gate와 art review를 먼저 통과한다. baseline이 없다는 이유로 실패시키지 않는다.
2. 사용자가 승인한 candidate만 ground truth가 된다.
3. 이후 동일 seed/camera/time/background/RHI 조합과 비교한다.
4. baseline 갱신은 명시적 승인 없이는 자동 수행하지 않는다.
5. GPU/driver 차이는 platform-RHI-shader model별 대안 baseline 또는 diagnostic SimCache로 관리한다.
6. regression pass는 기존 외형이 유지된다는 뜻이지 외형이 아름답다는 최초 증거가 아니다.

## Approval baseline

objective analyzer pass는 승인 baseline을 자동 생성하지 않는다. 다음 조건을 모두 만족한 candidate만 baseline으로 승격한다.

1. structural/runtime/pixel/aesthetic/archetype gate가 각각 기록되어 있다.
2. matching blank/effect pair와 analyzer JSON이 같은 manifest에 연결돼 있다.
3. presentation capture에서 gameplay read와 art direction을 사람이 검토했다.
4. 사용자 또는 지정 art owner가 candidate를 명시적으로 승인했다.

승인 시 effect PNG뿐 아니라 matching blank PNG, capture contract, JSON report를 함께 보존한다. 이후 regression은 새 capture가 같은 contract로 생성됐는지 먼저 확인하고 approved effect baseline과 비교한다. blank도 달라졌다면 effect regression으로 판정하지 말고 environment/capture contract 변경으로 분리한다. baseline 교체와 alternative baseline 추가는 명시적 승인 없이는 자동 수행하지 않는다.

Epic의 Screenshot Comparison Browser는 Functional Screenshot Test의 ground truth, noisy-feature 비활성화, tolerance와 RHI별 대안 이미지를 지원한다.

- https://dev.epicgames.com/documentation/unreal-engine/screenshot-comparison-tool-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-test-framework-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/niagara-debugger-for-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/NiagaraSimCache/CaptureNiagaraSimCacheImmediate

## 산출물

각 승인 시 다음 경로 또는 manifest field를 기록한다.

- system, material, texture, mesh, Effect Type 경로;
- engine/RHI/quality/post-process profile;
- deterministic seed, camera, background, capture time;
- 각 gate의 pass/fail과 raw observation;
- diagnostic/presentation montage;
- baseline ID와 승인자/승인 시각;
- 성능 측정과 worst-case instance 수.
