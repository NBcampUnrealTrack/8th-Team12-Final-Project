# Archetype Profiles

archetype profile은 emitter recipe가 아니라 효과가 반드시 전달해야 할 공간, 시간, 운동의 계약이다. 같은 profile에서도 shape, palette, material은 여러 방식으로 구현할 수 있다.

## 공통 필드

각 profile은 다음 항목을 Effect IR에 기록한다.

- `anchor`: 효과가 시작되고 의미가 고정되는 위치 또는 면;
- `primary_read`: peak에서 가장 먼저 보여야 하는 형태와 방향;
- `motion_law`: 모든 레이어가 공유할 힘, 축 또는 흐름;
- `beats`: anticipation, attack, follow-through, decay 또는 loop 상태;
- `environment_response`: 지면, 표면, 캐릭터, 경로와의 접촉;
- `archetype_checks`: 해당 유형에만 적용할 시각 검사;
- `forbidden_shortcuts`: 구조는 맞지만 유형의 의미를 훼손하는 표현.

## Profile 표

| Archetype | Primary read | Shared motion law | 필수 beat/접촉 | 주요 실패 |
|---|---|---|---|---|
| Impact | 충돌점과 힘의 방향 | impact normal 또는 weapon tangent | 짧은 attack, 지연된 파편/연기, contact response | 동심원만 보임, 구형 flash가 방향을 가림 |
| Projectile | 이동체의 선두와 경로 | forward velocity | launch, travel, tail, hit handoff | trail이 본체보다 밝음, tail 단절 |
| Trail/Slash | 시작-끝 방향과 taper | source trajectory | path accumulation, head/tail fade | 고정 plane 확대, 폭이 일정한 ribbon |
| Portal | 경계와 통과 가능한 내부 | boundary tangent/vortex | open, stable loop, close; interior depth | 얇은 원 하나, 내부와 배경 구분 없음 |
| Tornado/Vortex | 수직 taper와 회전 흐름 | angular velocity + vertical lift | form-up, steady flow, loop boundary | 쌓인 완전 링, 회전 없이 위로 뜨는 smoke |
| Splash/Fluid | 접촉점에서 발생한 탄도 | impulse + gravity + surface tangent | crown/sheet, droplets, delayed ripple | 안개만 보임, ripple이 splash보다 먼저 발생 |
| Lightning | 불규칙 경로와 짧은 에너지 전달 | source-to-target field | charge, strike, branch/ion residue | 매끈한 직선, 긴 균일 glow |
| Beam/AOE | 원점, 축, 영향 범위 | beam axis + radial field | charge, beam/area peak, floor response | beam과 ring이 같은 밝기, 범위가 불명확 |
| Aura/Attached | 대상 silhouette를 보조하는 흐름 | target local field | enter, steady variation, exit | 캐릭터를 가림, camera-facing halo만 반복 |
| Ambient/Loop | 공간의 상태와 반복 없는 흐름 | wind, buoyancy, turbulence | start fill, steady state, seamless boundary | loop gap, 모든 입자 동기화, 과도한 초점 |

## Impact

- `anchor`: 충돌 표면 또는 weapon contact.
- Primary는 radial sphere보다 방향성 wedge, slice, compressed core 또는 비대칭 burst가 우선이다.
- shock arc는 Primary보다 늦고 낮은 value로 규모를 설명한다.
- debris와 smoke는 impact normal, gravity, surface tangent를 공유한다.
- gate: peak anisotropy, contact centroid, Primary/Secondary peak 분리, decay energy.
- 금지: full ring 2개 이상이 주 silhouette, 모든 burst가 `0.0s`, 큰 white sphere.

## Projectile 및 Trail

- head가 가장 명확하며 tail은 이동 방향을 설명하되 head를 추월하지 않는다.
- velocity alignment, ribbon link order, width taper, source motion sampling을 검사한다.
- 느린 camera에서도 path가 끊기지 않고, 정지 시 불필요한 긴 ribbon이 남지 않아야 한다.
- hit effect는 projectile lifecycle과 별도 시스템이어도 palette와 motion handoff를 유지한다.

## Portal

- boundary, interior, outward/inward flow의 세 공간을 구분한다.
- dark interior나 scene treatment로 통과 깊이를 만들고, ring은 경계 설명에만 사용한다.
- smoke, motes, arcs는 boundary tangent 또는 vortex를 공유한다.
- gate: interior-to-background contrast, boundary continuity, loop start/end 차이, side-angle silhouette.
- 금지: generic torus 한 개와 glow만으로 완료, 모든 detail이 완전한 원.

## Tornado/Vortex

- 아래는 좁고 위는 넓은 taper 또는 의도된 역방향 taper가 한 silhouette로 읽혀야 한다.
- particle은 angular velocity뿐 아니라 vertical lift와 radius evolution을 가져야 한다.
- 큰 회전 body와 작은 turbulence의 속도 scale을 분리한다.
- gate: vertical extent, radius-by-height trend, angular flow evidence, loop dead zone.
- 금지: Z축에 완전 링을 등간격으로 쌓아 회전을 대신함.

## Splash/Fluid

- crown/sheet가 접촉점을 만들고, droplets가 탄도를, ripple이 지연된 표면 반응을 설명한다.
- gravity, drag, collision 또는 surface constraint 중 필요한 물리 단서를 사용한다.
- mist는 가장 낮은 value이며 splash silhouette를 지우지 않는다.
- gate: ground contact, upward-to-downward motion, delayed ripple, below-surface clipping.

## Lightning 및 Beam/AOE

- Lightning은 source/target 관계와 path irregularity가 핵심이다. glow는 경로보다 넓거나 오래 남지 않는다.
- Beam은 축, AOE는 영향 면을 Primary로 하며 둘을 같은 value로 동시에 peak시키지 않는다.
- branching, flicker, ion residue는 큰 경로를 보조하는 작은 scale 변화로 사용한다.

## Aura 및 Ambient

- attached aura는 캐릭터의 얼굴, 무기, 발 위치 등 gameplay 정보를 가리지 않는다.
- ambient loop는 명확한 단일 peak보다 통계적 변화와 loop 연속성이 중요하다.
- loop 검증은 start, steady, boundary 직전/직후를 포함한다.
- one-shot용 `continuous_spawn` 금지 규칙을 ambient에 기계적으로 적용하지 않는다.

## Profile 선택

1. gameplay 목적이 둘 이상이면 Primary 목적 하나를 선택한다.
2. 나머지는 secondary archetype으로 기록하되 각 profile의 Primary를 동시에 유지하지 않는다.
3. 기존 recipe는 profile을 충족시키는 구현 후보일 뿐 profile 자체가 아니다.
4. tutorial stack이 profile의 gate를 통과하지 못하면 tutorial과 같은 emitter 이름을 가졌더라도 실패다.

