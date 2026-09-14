## OutlineMaterial 팩 학습

> 소스: `Content/OutlineMaterial/` (AssetFreaks 팩, UE5.2) — 마스터 머티리얼 12종(`M_Outline_Aura/Colorful/Edge/Electric/Fire/Glitch/Glow/Poison/Shadow/Simple/Stripe/Water`) + MI 12종 + `BP_Outline` + 컬러 커브 아틀라스(`CA_Outline`, `CC_Outline_01-03`) + 클라우드/파이어/코스틱/버텍스노이즈 텍스처.

### 아웃라인 기법 — 방식

- **버텍스 기반 인버티드-헐 (메시 머티리얼)**, 포스트프로세스/CustomDepth 아님. WorldPositionOffset으로 버텍스를 `VertexNormalWS` 방향으로 밀어내고 `TwoSidedSign`으로 백페이스만 실루엣으로 노출 — STYLES.md 툰/셀 축이 정의한 "inverted-normal back-face hull via WPO, TwoSidedSign × -1"과 **동일 계열 기법임을 팩이 검증**.
- 전 머티리얼 공통: **MSM_Unlit**(에미시브 전용, 라이팅 무시) + **BLEND_Masked**(불투명 베이스 + 알파 마스크 가장자리 — 반투명 아님) + MD_Surface.

### 노드 체인 (코어 패턴)

```
Fresnel(림 검출) ──┐
                   ├→ Multiply(컬러/텍스처) → Emissive (백페이스에 가산)
TwoSidedSign ──────┘
WPO = LocalPosition함수 + 패닝 텍스처 → 애니메이션 두께 변조
```
- **정적 변형(Simple/Edge)**: Fresnel × Base_Color만 — 3–4 노드. 우리 기본 아웃라인에 해당.
- **애니메이션 변형(Fire/Glitch 등)**: Panner 노드가 U/V 독립 스크롤(U_Speed/V_Speed/U_pos/V_pos) + Time 입력 sine 플래싱 + 노이즈 글리치 — 30+ 노드.
- **컬러 커브**: `CurveAtlasRowParameter`로 [0–1] 입력을 RGB 그라데이션에 리맵 — 펄싱/전이 색 변화를 커브 에셋으로 외부화.
- **안티앨리어싱**: DitherTemporalAA — STYLES.md의 "dither to fight posterize banding" 노트와 일치.

### 파라미터 표면

| 파라미터 | 타입 | 역할 |
|---|---|---|
| `outline_size` | Scalar (0.0–1.0) | WPO 밀어내기 양 = 아웃라인 두께 |
| `Base_Color` | Vector | 아웃라인 색 |
| `Speed`, `U_Speed_00/01` | Scalar | 패너 스크롤 속도 (애니메이션 변형) |
| `Strength_Base_VertexNoise` | Scalar | 버텍스 노이즈 진폭 (유기적 흔들림) |
| `Tex00/Tex_01/Tex_VN` | Texture | 레이어 텍스처 + 버텍스 노이즈 |

### 적용 방법

- **메시 단위 적용** — 글로벌 포스트프로세스 볼륨이 아님. 스켈레탈/스태틱 메시의 머티리얼 슬롯 오버라이드 또는 `BP_Outline` 방식(복제 메시 섹션에 아웃라인 머티리얼 배정)으로 액터별 부착.
- 인스턴스는 스칼라/컬러 트윅만 저장(그래프 불변) — 마스터 1 + MI N 전략.
- **한계**: 버텍스 기반이라 두께가 메시 해상도 의존(로우폴리 = 자글거림). 스크린스페이스 균일 두께가 필요하면 포스트프로세스로 가야 하나, STYLES.md 툰 규칙("thickness constant or scale-tied, not depth-soft")에는 오히려 부합.

### 우리 툰/셀 스타일 축 통합 절차

1. **`M_LevyOutline` 마스터 신설** (Masked / Unlit / TwoSided): WPO = `VertexNormalWS × OutlineThickness`, `TwoSidedSign`으로 백페이스만 출력(프론트페이스 마스크아웃). 기본값 = STYLES.md 정의대로 **불투명 검정 + 두께 상수(스케일 연동)**.
2. **파라미터 계약**: `OutlineThickness`(스칼라, 캐릭터 스케일 연동), `OutlineColor`(팔레트 블록 바인딩 — 임의 색 금지), `DitherAmount`(포스터라이즈 밴딩 대응), 옵션 `PulseSpeed`/`NoiseStrength`(상태 피드백용, 기본 0).
3. **상태 변형 트랙 매핑**: 팩 12종 스타일 중 호러 톤에 맞는 3개만 변형 MI로 — Simple(기본/상시), Shadow(ActorPosition+Sine 이동 그림자 — 추적자/위협 표시), Glitch(Time sine 플래싱 — 정신오염/기억상실 상태 피드백). Fire/Electric류 고채도 변형은 OBLIVIO 탈채도 팔레트와 충돌하므로 제외. **Emissive는 호러 캡(<0.5) 내로 클램프.**
4. **컬러 커브 채택**: 펄싱 색 전이는 하드코딩 대신 `CurveAtlasRowParameter` + 커브 아틀라스 1장으로 — 팩의 CA_Outline 패턴. 팔레트 8블록에서 파생한 행만 등록.
5. **적용 파이프라인**: 캐릭터/적 BP에 아웃라인 전용 머티리얼 슬롯(또는 복제 메시 컴포넌트) 추가 → MI 스왑으로 상태 전환. 포스트프로세스 의존성 없음 = 모바일/저사양 안전.
6. **검증 기준**: 로우폴리 적 메시에서 아웃라인 자글거림 확인 → 필요 시 해당 메시만 노멀 스무딩 또는 두께 하향. 카메라 거리별 두께 일관성(스케일 연동) 체크.
