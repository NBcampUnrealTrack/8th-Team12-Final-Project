# VFX Failure Atlas

이 문서는 “안 예쁘다”를 재현 가능한 관찰과 원인으로 바꾸기 위한 진단 목록이다. 항목을 찾은 뒤 `CRITIQUE_AND_REPAIR.md`의 한 축 수정 규율을 적용한다.

## F01. Tutorial stack without tutorial visual

**보임**: emitter 이름과 레이어 순서는 reference와 비슷하지만 결과는 generic glow, ring, smoke의 조합처럼 보인다.

**원인**: texture, mesh UV, renderer alignment, material curve, dynamic parameter, lighting/scale 계약을 보존하지 않고 generic material로 교체했다.

**검사**: source template의 renderer/material contract와 final candidate를 비교하고 generic `MI_RevvyTutorial_*` 사용 여부를 확인한다.

**수정**: tutorial 순서를 더 복제하지 않는다. aesthetic IR과 archetype profile에 맞는 material/shape contract를 새로 설계한다. generic tutorial material은 prototype에서만 사용한다.

## F02. Concentric-ring stack

**보임**: Impact, Portal, Tornado가 모두 완전한 원 여러 개로 읽힌다.

**원인**: torus/ring primitive를 Primary와 Secondary에 반복하고 크기와 timing만 바꿨다.

**수정**: Primary를 방향성 형태로 교체하고 ring은 하나의 broken/partial accent로 낮춘다. taper, phase offset, 비균일 scale과 mask erosion을 사용한다.

**검증**: peak circular symmetry와 salient component count가 감소하고 archetype read가 개선되어야 한다.

## F03. White or colored bloom ball

**보임**: 밝은 구 또는 원반이 다른 silhouette를 덮는다.

**원인**: glow sprite 면적, emissive, lifetime이 모두 크거나 exposure를 고려하지 않았다.

**수정**: core 면적/수명을 먼저 줄이고 body/edge가 형태를 설명하게 한다. post bloom으로 약한 형태를 숨기지 않는다.

## F04. Tiny editor-preview effect

**보임**: editor에서 focus하면 보이지만 gameplay frame에서는 너무 작거나 약하다.

**원인**: preview zoom을 scale reference로 사용했다.

**수정**: 180 cm reference와 3/8/15 m camera에서 Primary의 화면 점유를 재설계한다. 전체 emitter를 일괄 확대하지 않는다.

## F05. Equal-value layering

**보임**: 모든 layer가 같은 밝기, 크기, 채도로 동시에 경쟁한다.

**원인**: tint만 다르고 같은 material/curve/peak time을 공유한다.

**수정**: Primary에 value와 선명도를 집중하고 Secondary의 면적, opacity, peak를 낮추고 늦춘다.

## F06. Scale-only motion

**보임**: sprite 또는 mesh가 제자리에서 커졌다 사라진다.

**원인**: scale curve만 있고 위치, 회전, UV, distortion 또는 flow 변화가 없다.

**수정**: archetype의 shared motion law에서 최소 두 scale의 움직임을 추가한다. 단, 무관한 noise로 형태를 흐리지 않는다.

## F07. Incoherent random motion

**보임**: smoke, sparks, arcs가 서로 다른 효과처럼 흩어진다.

**원인**: 위치, 속도, 수명, 회전을 모두 독립 난수화했다.

**수정**: 공통 anchor/axis/field를 두고 난수는 분포와 위상에만 제한한다.

## F08. Particle clutter

**보임**: 작은 밝은 점과 streak가 Primary 주변을 가득 채운다.

**원인**: detail 부족을 spawn count로 보정했다.

**수정**: equal-value accent를 제거하고 negative space를 회복한다. 가장 큰 silhouette와 timing부터 고친다.

## F09. Hard pop or dead tail

**보임**: 마지막 frame에서 갑자기 사라지거나 낮은 alpha card가 오래 남는다.

**원인**: material이 Particle Color/Relative Time을 소비하지 않거나 emitter loop/lifetime이 맞지 않는다.

**수정**: alpha, erosion, size envelope의 실제 material 연결과 loop period를 검사한다.

## F10. Billboard rectangle

**보임**: light background에서 sprite 사각 외곽, 검은 fringe, 잘린 glow가 보인다.

**원인**: alpha fringe, 잘못된 blend, texture background, missing Depth Fade, particle alpha 미사용.

**수정**: texture/channel/material contract를 고치고 dark/light diagnostic capture를 반복한다.

## F11. Loop seam or synchronized pulse

**보임**: Portal/Aura/Ambient가 주기적으로 모두 꺼지거나 동시에 밝아진다.

**원인**: emitter delay/duration/lifetime 위상이 같거나 once emitter를 loop body로 사용했다.

**수정**: loop boundary를 중심으로 phase를 재분배하고 steady body가 항상 남도록 한다. accent pulse는 비동기화한다.

## F12. Missing environment contact

**보임**: Impact가 지면 위에 뜨고, Splash가 표면을 통과하며, Aura가 캐릭터와 분리돼 보인다.

**원인**: anchor, pivot, local/world space, surface response가 정의되지 않았다.

**수정**: contact marker에서 side view로 확인하고 ground response, surface tangent, collision 또는 mask를 추가한다.

## F13. Background-dependent readability

**보임**: dark에서는 glow만 보이고 light에서는 전체가 사라지거나 검은 fringe가 나타난다.

**원인**: hue만으로 hierarchy를 만들고 value/opacity/depth를 분리하지 않았다.

**수정**: dark/light 양쪽에서 Primary silhouette를 유지하도록 value range와 edge treatment를 재설계한다.

## F14. Invalid visual evidence

**보임**: 캡처에 browser, editor graph, timeline, popup이 포함되고 효과가 작은 preview 영역에만 있다.

**원인**: `capture_editor_window`를 viewport regression으로 사용했다.

**수정**: validation level의 fixed camera와 Functional Screenshot/offscreen capture를 사용한다. 기존 캡처는 UI 상태 기록으로만 분류한다.

## F15. Structure-score substitution

**보임**: compile 0/0, quality 100/100을 근거로 시각 완성을 보고하지만 montage나 gameplay-scale evidence가 없다.

**원인**: structural/runtime gate를 pixel/aesthetic/archetype gate와 혼동했다.

**수정**: `VISUAL_VALIDATION.md`의 다섯 gate를 별도 항목으로 기록한다. 하나라도 미검증이면 production-ready가 아니다.

## 진단 순서

1. F14/F15로 증거 자체가 유효한지 본다.
2. F03/F04/F09/F10처럼 hard pixel failure를 제거한다.
3. F02/F05/F06/F07/F08로 hierarchy와 motion을 고친다.
4. F11/F12/F13으로 loop, 접촉, 배경 적응을 검증한다.
5. F01이 남으면 recipe가 아니라 material/shape contract와 aesthetic IR로 돌아간다.

