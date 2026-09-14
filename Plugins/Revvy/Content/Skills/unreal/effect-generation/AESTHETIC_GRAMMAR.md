# Aesthetic Grammar

이 문서는 튜토리얼의 emitter 순서를 복제하지 않고도 완성도 있는 VFX를 설계하기 위한 공통 시각 문법이다. 새 효과는 기술 이름보다 먼저 게임에서 전달할 의미, 시선의 흐름, 시간 리듬을 정의한다.

## 핵심 원칙

1. **한 프레임의 의미**: gameplay camera에서 peak 한 프레임만 보아도 공격 방향, 위험 범위, 회복, 이동 경로 중 무엇을 뜻하는지 읽혀야 한다.
2. **Primary는 하나**: 가장 높은 value, 가장 선명한 silhouette, 가장 강한 운동 방향을 하나의 Primary가 담당한다. Secondary는 Primary를 설명하며 경쟁하지 않는다.
3. **색보다 value**: 흑백으로 보아도 초점과 깊이가 유지되어야 한다. hue와 saturation은 의미를 강화하는 수단이지 hierarchy의 대체물이 아니다.
4. **시간 대비**: anticipation, attack/peak, follow-through, decay는 서로 다른 상태로 읽혀야 한다. 모든 레이어가 동시에 켜지고 동시에 꺼지면 실패다.
5. **공유 운동장**: 서로 다른 emitter도 같은 충돌점, 축, vortex, 중력 또는 진행 벡터를 공유한다. 무관한 난수 운동을 여러 겹 쌓지 않는다.
6. **형태의 편집**: 완전한 원, 균일 복제, 같은 두께와 간격은 의도된 UI/기술 언어가 아니면 그대로 쓰지 않는다. 끊기, taper, phase offset, 비균일 간격, 방향 편향으로 형태를 편집한다.
7. **negative space**: 밝은 입자와 디테일로 화면을 채우지 않는다. Primary 주변의 조용한 영역이 silhouette와 속도를 읽게 한다.
8. **깊이 분리**: dark/occluding core, 중간 value body, 제한된 bright accent처럼 앞뒤 역할을 분리한다. 같은 blend, 밝기, 크기의 레이어만 반복하지 않는다.
9. **환경 접촉**: 지면 충돌, 벽 반사, 캐릭터 부착, 물 표면 등 효과가 놓인 공간과 접촉하는 단서를 최소 하나 둔다.
10. **게임 규모 우선**: Niagara editor의 확대 상태가 아니라 캐릭터, 지면, gameplay camera 거리와 비교해 크기를 판단한다.

## 미학 IR

`style_brief`의 자유 문장만으로 빌드하지 않는다. 다음 필드를 Effect IR에 구조화해 후보 생성과 검증이 같은 의도를 사용하게 한다.

```yaml
art_direction:
  intent: heavy arcane melee impact
  gameplay_read: close-range hit from lower-left to upper-right
  archetype: impact
  style_profile: premium_dark_fantasy
  focal_anchor: weapon_contact
  primary:
    form: broken diagonal wedge
    value_role: brightest_at_peak
    motion: fast_outward_slice
  secondary:
    - {form: partial_shock_arc, value_role: mid, motion: delayed_expand}
    - {form: sparse_debris, value_role: low, motion: ballistic}
  palette_roles:
    core: ivory
    body: amber
    residue: charcoal
  depth_plan: [dark_core, translucent_body, emissive_accent]
  symmetry: low
  density: sparse
  negative_space: preserve_upper_left
  shared_motion_field: impact_normal
  avoid: [full_concentric_rings, white_bloom_ball, uniform_radial_spray]
```

필수 필드는 `intent`, `gameplay_read`, `archetype`, `focal_anchor`, `primary`, `palette_roles`, `shared_motion_field`, `avoid`다. 값은 구현 모듈명이 아니라 보이는 결과를 기술한다.

## 시간 문법

시간은 절대 프레임보다 전체 수명에 대한 비율과 beat 관계로 먼저 설계한다.

| Beat | 역할 | 일반 규칙 |
|---|---|---|
| Anticipation | 방향과 발생점을 예고 | 작고 응축되며 peak보다 낮은 value |
| Attack | 가장 빠른 상태 변화 | Primary가 먼저 읽히고 가장 짧게 유지 |
| Follow-through | 힘의 방향과 규모 설명 | shock arc, trail, debris가 서로 다른 속도로 전개 |
| Decay | 에너지의 물질적 결과 | smoke, dust, ember, dissolve가 낮은 value로 종료 |

비율은 archetype과 gameplay 목적이 결정한다. `80 ms`, `0.8 s` 같은 숫자를 모든 효과에 복사하지 않는다. 실제 초 단위 값은 `ARCHETYPE_PROFILES.md`의 관계와 target gameplay 속도를 바탕으로 정한다.

## 형태와 운동 조합

- Primary 형태와 운동 방향은 일치시킨다. 대각 slash는 대각 속도와 taper를, vortex는 회전과 수직 이동을 함께 가져야 한다.
- 큰 형태 1개, 중간 보조 형태 1-2개, 작은 accent를 제한적으로 사용한다.
- 두 개 이상의 motion scale을 사용한다. 예: 빠른 streak와 느린 smoke, 큰 vortex와 작은 turbulence.
- randomness는 silhouette를 파괴하지 않는 범위에서 분포를 깨는 데 사용한다. 위치, 속도, 수명, 색을 모두 독립 난수화하지 않는다.
- primitive mesh를 그대로 드러내지 않는다. UV 흐름, erosion, 부분 마스크, non-uniform scale, 회전 위상 중 필요한 편집을 적용한다.

## 머티리얼 계약

- Primary와 Secondary가 같은 generic material과 같은 texture를 공유해 역할 차이가 사라지지 않게 한다.
- alpha, erosion, emissive, UV motion 중 시간에 따라 변하는 channel을 최소 하나 사용한다.
- 밝은 core는 작은 면적에 제한하고, body와 edge가 형태를 설명하게 한다.
- translucent 교차면은 Depth Fade 또는 의도된 contact treatment를 검토한다.
- 텍스처, mesh UV, renderer alignment, dynamic parameter 의미를 하나의 계약으로 기록한다.

> `MI_RevvyTutorial_*` 같은 generic tutorial material은 구조와 timing을 확인하는 prototype 전용이다. 최종 후보는 archetype과 art direction에 맞는 검증된 material contract를 가져야 하며, generic material을 유지한 채 production-ready로 승인하지 않는다.

## 후보 생성 규칙

첫 결과 하나를 폴리싱하는 대신 같은 IR에서 3개의 제한된 후보를 만든다.

- 후보 A: Primary shape를 가장 직접적으로 표현한다.
- 후보 B: timing contrast와 asymmetry를 강화한다.
- 후보 C: material/depth language를 달리한다.
- 후보마다 shape, timing, material 중 최대 두 축만 바꾼다.
- emitter 수를 임의로 늘리는 것은 후보 차이로 인정하지 않는다.
- 같은 seed, camera, background, capture time으로 비교한다.

후보 선택과 수정은 `VISUAL_VALIDATION.md` 및 `CRITIQUE_AND_REPAIR.md`를 따른다.

## 완료 질문

- Primary를 한 문장과 한 silhouette로 설명할 수 있는가?
- 흑백 peak에서도 초점이 하나인가?
- 각 Secondary가 Primary의 방향, 규모, 재질 또는 여운 중 하나를 설명하는가?
- 적어도 세 시점에서 서로 다른 상태가 보이는가?
- 완전 동심원, 균일 방사, 과도한 흰 core가 의도 없이 지배하지 않는가?
- 마지막 프레임에서 낮은 에너지로 자연스럽게 끝나는가?
- gameplay scale과 dark/light 배경 모두에서 같은 의미가 읽히는가?

