## RealisticBlood 팩 학습

> 소스: `Content/RealisticBlood/` — Niagara 시스템 37개(11개 효과군), 머티리얼 58개(마스터 5 + 인스턴스 ~46 + 데칼 MI 11), 텍스처 130+ (스플래터 60+, 데칼 40+, 노이즈/리플렉션/셰입). 모듈스크립트 `NMS_DecalSize`, 데칼 메시 `SM_DecalBox`.

### (a) 효과군별 Niagara 구조 — 4-레이어 이미터 레이어링

모든 효과군이 동일한 **4-레이어 합성** 골격을 공유하고, 효과군별로 레이어 비중만 다르다:

| 레이어 | 역할 | 핵심 모듈 | 지속 |
|---|---|---|---|
| **L1 Burst 분사** | 임팩트 순간 날카로운 스프레이 | Spawn(2200–3400 ref) + Velocity(2000–3300) + Sprite/Ribbon 렌더러 | 0.1–0.5s |
| **L2 포물선 낙하** | 탄도형 드롭릿 아크 | Velocity + Gravity(18–142 ref) + Sprite 300–1500개 | 중간 |
| **L3 데칼 누적** | 바닥/벽 혈흔 잔류 | Collision(Splatter군 3458 ref, CPU 비쌈) + Decal 렌더러(888 ref) + `M_LiquidDecalBase` 방향성 인스턴스 21종 | 영구적 |
| **L4 드립 잔여** | 후행 흘러내림 | 저속 Spawn + 긴 수명(0.5–3s) + ColorOverLife(226–720 ref) | 롱테일 |

**효과군별 비중 차이** (대표 스택 수치):
- **BulletHit/Slash** (관통·베기): L1+L2 중심. Slash_High Spawn=3426·Gravity=142 — 가장 넓은 스프레이 + 자연스러운 아크. GPU 힌트 25.
- **BloodBurst** (폭발성): Spawn=2211·Gravity=101 — 고에너지 방사형.
- **Artery** (분수형 제트): Spawn=1191·Gravity=60 — 중간 낙하, 제트 물리.
- **Splatter** (데칼 특화): Collision=3458·Decal=888·Gravity=18(수직 낙하) — L3 전담. Mesh 렌더러(69) + CPU 힌트 166으로 가장 무거움.
- **Dripping** (잔여 누수): Spawn=1538(저속 지속)·긴 수명 — L4 전담 별도 시스템 6종.
- **렌더러 역할분담**: Sprite=주 페이로드(GPU 우선, 300–1500개), Decal=영구 혈흔(CPU 충돌 스폰), Ribbon=트레일 보조(20–80개), Mesh=볼류메트릭 스플래터(6–70개).
- **LOD 전략**: 전 효과군 High/Med/Low 3티어(+Burst는 Extreme). 스폰 수·충돌·데칼 수만 깎고 레이어 구성은 유지.

### (b) 블러드 머티리얼 레시피

**마스터 2장 + 인스턴스 파라미터** 구조 — 그래프 변경 없이 MI 오버라이드만으로 46종 변주.

- **`M_LiquidBase`** (파티클용): BLEND_Translucent + MSM_DefaultLit + VolumetricPerVertexNonDirectional 트랜스루선시(얇은 혈막 광 투과). 노드 체인: Panner(D_SpeedX 0.05–0.2 / D_SpeedY 0.1–0.3 — 하향 드립 흐름) → 스플래터 알베도+노멀 샘플 → LinearInterpolate(MainColor↔SecondaryColor) → Multiply 체인(Opacity×Darkness) → DynamicParameter(파티클 색 틴트) + DitherTemporalAA.
- **`M_LiquidDecalBase`** (데칼용): Unlit 변형, DBuffer ColorNormalRoughness 응답 — 라이팅 연산 생략으로 영구 데칼 비용 절감. `M_LiquidDecal_Puddle`은 바닥 고임 전용 파생.
- **검증된 셰이딩 수치**:
  - 알베도: MainColor **R 0.25–0.35 / G 0.05–0.12 / B 0.02–0.08** (탈채도 크림슨), SecondaryColor **R 0.12 / G 0.02 / B 0.01** (그림자 톤) — Dissolve 파라미터로 lerp하여 신선→응고 노화.
  - 스펙큘러: `T_LiquidReflection_02` 샘플로 고주파 하이라이트 + **Roughness 0.6–0.8** (젖은 매트), **Metallic 0 고정**.
  - 노멀: 스플래터 노멀(SAMPLERTYPE_Normal) + Panner 노이즈 합성 = 젖은 반짝임/흐름 착시. Normal Strength **0.5–1.0**.
  - 디스토션: DistortIntensity **0.01–0.15** (초과 시 비현실적 왜곡).
  - Opacity = Base × Darkness(0.7–1.0) × Softness(가장자리 페이드) × Dissolve(알파 컷오프 페더링).
- **방향성 데칼 전략**: 스플래터 텍스처 6군 + Directional 7군 + Thick 2군(각각 컬러+노멀 페어)을 MI 12종(`MI_BloodDirection_01-12`)에 배정 → **임팩트 각도와 텍스처 흐름 방향을 매칭**해 스폰. 충돌 시 `NMS_DecalSize`로 크기 계산 → `SM_DecalBox` 지오메트리로 누적.

### (c) OBLIVIO 블러드 클로 슬래시 (`NS_BloodClawSlash`) 업그레이드 포인트

현 레시피(OBLIVIO_EFFECTS.md §3: ClawSlash_Main 리본 + BloodSpray + Trail_Droplets + StainDecal)는 팩의 4-레이어 골격과 이미 일치. 보강할 점:

1. **StainDecal 텍스처 교체**: 현재 `T_StainNoise_Organic`(절차적 Voronoi) 단일 → 팩의 **방향성 스플래터 마스크**(추출 `T_BloodSpray_Directional_01`/`T_BloodSpray_Heavy_01`)로 교체하고 슬래시 진행 방향과 텍스처 흐름 방향을 정렬. 절차적 노이즈보다 "할퀸 방향" 가독성이 즉시 상승. 데칼 2–3장 변주(`T_BloodSplotch_Organic_01` 혼합)로 반복감 제거.
2. **중력비 차등 검증 완료**: 레시피의 Spray 400 / Droplets 600 cm/s² 구분은 팩의 Hit(128–142) vs Splatter(18) vs Artery(60) gravity:velocity 차등 패턴과 같은 원리 — 유지. 단 Droplets에 팩식 **하향 바이어스 + 긴 수명 분산(0.8–2.0s)** 이 이미 반영돼 있으므로 변경 불필요.
3. **L4 드립 레이어 추가 (옵션)**: 현 레시피는 L4(잔여 드립)가 없음. 벽면 히트 시 StainDecal 위치에서 저속 스폰(3–5개, 수명 2–3s, gravity 600, `T_BloodPuddle_01` 패닝)으로 흘러내림 추가 — 팩 Dripping군의 롱테일 패턴. 호러 톤(잔류 공포)에 부합.
4. **머티리얼 수치 보정**: `M_BloodSlash_Ribbon`·`M_BloodStain_Flat`의 컬러 커브를 팩 검증 레인지로 — Fresh #5C2E2E는 유지하되 Old/Stain 단계를 팩 SecondaryColor(0.12/0.02/0.01) 쪽으로 더 어둡게. Stain에 **Roughness 0.7 + `T_LiquidReflection_Gloss` 희미한 시트 샘플**을 더하면 "wet sheen만" 요구사항(Emissive 0.05–0.1)을 에미시브 대신 스펙큘러로 달성 — 호러 emissive 캡(<0.5)에 더 안전.
5. **데칼 Softness/Dissolve 파라미터 채택**: StainAlpha 단일 스칼라 → 팩식 **Softness(가장자리 페이드) + Dissolve(노화 컷오프)** 2-파라미터로 분리하면 3.5–4.5s 잔류 동안 가장자리부터 마르는 연출 가능.
6. **CPU Collision 데칼 스폰은 비채택**: 팩 Splatter군의 충돌 기반 데칼(CPU 힌트 166)은 비용 과다 — 현 레시피의 "delay 0.4s Z-pinned 버스트 1" 방식이 CPU <1.2ms 예산에 맞음. 유지.

### (d) 팔레트/마스터 채택 (3개 이내)

1. **`M_LevyVFX_Translucent` → Blood 프리셋 파라미터 세트 추가**: MainColor/SecondaryColor lerp + Darkness·Softness·Dissolve 스칼라 + Roughness 0.6–0.8/Metallic 0 — 팩의 MI-only 변주 전략을 그대로 가져와 그래프 수정 없이 혈액 계열 전부 커버.
2. **`M_LevyVFX_Erosion` → Dissolve 노화 패턴 채택**: 팩의 Dissolve(신선→응고 lerp + 알파 컷오프 겸용) 듀얼 역할을 Erosion 마스터의 DissolveThreshold와 통합 — 혈흔 데칼 수명 연출(젖음→건조→소멸)을 단일 파라미터 타임라인으로.
3. **팔레트 블록 1개 추가 — `Blood` 트라이어드**: Fresh(0.30, 0.08, 0.04) / Old(0.12, 0.02, 0.01) / Sheen(리플렉션 하이라이트용 저채도 화이트) — 기존 8블록 체계에 9번째로 등록, 채도 상한 0.70(임팩트 스파이크) 규칙은 OBLIVIO 톤 가이드 유지.
