# Critique and Causal Repair

완성도는 emitter를 계속 추가해서 만들지 않는다. 같은 조건에서 실패를 관찰하고 원인을 하나씩 수정하는 폐루프로 만든다.

## Tutorial-free closed loop

```text
intent
  -> machine-readable aesthetic IR
  -> three constrained candidates
  -> deterministic validation capture
  -> structural/runtime/pixel/aesthetic/archetype metrics and critique
  -> one causal repair
  -> recapture and compare
  -> explicit approval
  -> approved exemplar memory
```

튜토리얼은 기법의 출처나 분석 예제가 될 수 있지만 후보의 emitter 목록을 결정하지 않는다. intent, archetype, local template contract가 우선한다.

## 후보 단계

1. 동일한 IR, seed, duration, camera를 사용한다.
2. A/B/C 후보는 shape, timing, material 중 최대 두 축만 다르게 한다.
3. 각 후보의 변경 가설을 한 문장으로 기록한다.
4. compile/runtime 실패 후보는 art comparison에 올리지 않는다.
5. objective pixel gate를 통과한 후보만 aesthetic/archetype 비교에 올린다.

## Critique 형식

자유로운 “덜 예쁘다” 대신 다음 JSON 형태로 남긴다.

```json
{
  "frame": "peak",
  "gate": "aesthetic",
  "observed": "three equal-bright concentric rings dominate the frame",
  "expected": "one diagonal impact wedge with a delayed broken shock arc",
  "metric": {"salient_component_count": 3, "circular_symmetry": "high"},
  "suspected_cause": "secondary ring area and emissive equal the primary",
  "repair_axis": "shape_and_value",
  "proposed_change": "break the ring mask, reduce area/value, delay its peak",
  "confidence": 0.88
}
```

`observed`와 `expected`가 없는 점수는 repair 입력으로 사용하지 않는다.

## 원인별 repair table

| 관찰 | 우선 원인 | 먼저 바꿀 것 | 다시 볼 증거 |
|---|---|---|---|
| Primary가 안 읽힘 | Secondary value/area 동률 | Secondary emissive, size, opacity, peak time 감소 | peak 흑백 montage, saliency |
| 완전 동심원이 지배 | full ring mesh/mask 반복 | partial mask, broken arc, phase/taper, 비균일 scale | circular symmetry, side view |
| 큰 glow ball | core sprite 과대/과다 노출 | core area와 lifetime 축소, body edge 강화 | clipped ratio, Primary silhouette |
| 효과가 작고 약함 | editor zoom 기준 scale | gameplay reference에서 Primary만 재스케일 | screen coverage, 3/8/15m camera |
| sprite가 커지기만 함 | motion source 부재 | velocity/rotation/curl 또는 material UV motion | frame difference, motion vector |
| 레이어가 따로 움직임 | 독립 난수와 서로 다른 origin | shared field/anchor, correlated variation | composite trajectory, centroid |
| 화면이 지저분함 | equal-value accent 과다 | burst count/size/value 감소, negative space 회복 | component count, edge density |
| 끝에서 pop | fade contract 부재/잘못된 material | alpha/erosion/size envelope 연결 | decay/end frame jump |
| 사각 card가 보임 | texture fringe/alpha/material contract | texture alpha, particle color, blend/depth fade 수정 | rectangle score, light background |
| loop에 빈 구간 | emitter duration/delay 불일치 | loop period와 lifetime/spawn 위상 정렬 | boundary_before/after |
| 지면에서 뜸 | anchor와 contact response 부재 | surface tangent, collision/decal/ripple, pivot 수정 | side view, contact centroid |
| generic tutorial look | generic MI와 primitive shape 유지 | art-direction 전용 material/texture/mesh contract | material depth rubric |

## 수정 규율

- 한 iteration에서 하나의 `repair_axis`만 수정한다.
- 실패를 가리기 위해 새 emitter를 추가하지 않는다.
- material 문제를 particle count로, timing 문제를 bloom으로 보정하지 않는다.
- Primary가 읽히기 전에는 accent/detail을 폴리싱하지 않는다.
- 수정 전후 같은 capture matrix를 사용한다.
- metric이 좋아졌지만 archetype read가 나빠지면 변경을 채택하지 않는다.
- 두 번 연속 개선이 없으면 원인 가설을 폐기하고 한 단계 위 IR을 다시 검토한다.

## Vision critic 사용

vision model은 다음 용도로만 사용한다.

- montage에서 artifact와 hierarchy 위반 후보 찾기;
- IR의 `avoid`와 실제 frame 비교;
- A/B/C 후보의 상대 순위와 근거 생성;
- failure atlas 항목 추천.

다음은 금지한다.

- vision 점수 하나로 production 승인;
- 다른 camera/background 없이 한 장만 평가;
- tutorial reference와 픽셀 유사도를 품질로 간주;
- 객관 gate 실패를 “스타일”로 정당화.

## 승인과 학습 메모리

학습 메모리는 모델 fine-tuning을 의미하지 않는다. 승인된 결과에서 재사용 가능한 관계를 구조화해 retrieval하는 방식이다.

승인 exemplar에는 다음을 저장한다.

- intent와 aesthetic IR;
- archetype/style profile;
- 최종 manifest와 source template contract;
- diagnostic/presentation captures와 metrics;
- 실패 critique와 실제로 개선된 repair delta;
- 성능/quality tier;
- 사용자 승인 여부.

실패 후보, tutorial 원본의 emitter 순서, 사용자 미승인 결과는 positive exemplar로 저장하지 않는다. 다음 생성에서는 비슷한 intent/archetype의 승인된 `관계와 repair delta`를 검색하며 asset path나 고정 수치를 무조건 복사하지 않는다.

## 종료 조건

- 다섯 visual gate가 모두 통과했다.
- Primary와 archetype read를 한 문장으로 설명할 수 있다.
- 모든 hard failure가 해소됐다.
- A/B/C 중 선택 근거와 폐기 근거가 남았다.
- target quality의 성능 gate를 통과했다.
- 승인된 candidate만 final destination과 exemplar memory에 승격됐다.

