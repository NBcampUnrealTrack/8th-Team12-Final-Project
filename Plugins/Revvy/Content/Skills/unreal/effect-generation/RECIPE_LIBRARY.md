# RECIPE_LIBRARY.md — 실측 빌더블 레시피 라이브러리 (UE5.5+, 누적)

> `NIAGARA_MAKING.md` 프레임워크의 **구체 레시피** 모음. 효과별: 이미터 표 + 머티리얼 + 핵심원칙/#1실수. 누적 추가됨.
> 검증 원칙: ①베이스 Sprite Size 안 박으면 안 보임 ②불/연기=움직이는 입자❌ 애니 머티리얼+정지 스프라이트✅ ③over-life는 ParticleRelativeTime 또는 ColorFromCurve/ScaleSpriteSize 커브.
> ⚠️ CGHow 등 튜토리얼 페이지는 프로모(상세값은 영상/프로젝트파일). 아래 값은 검증된 기법 + VFX 표준에서 구성한 빌더블 기준값 — 영상 참고해 미세조정. **Epic 공식문서 출처 표기 레시피는 공식 검증값.**

---

### 텔레포트 / 디졸브 (캐릭터 사라짐→재등장)
출처(참고): CGHow "Teleport in UE5 Niagara" / "Disintegration Skeletal Mesh FX".

| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **MeshDissolve** (대상 메시 자체) | (스켈레탈/스태틱 메시 머티리얼) | DissolveThreshold User param 0→1 over 0.6s, Feather 0.05~0.15 | dissolve: `saturate((NoiseTex.R - Threshold)/Feather)` → Opacity Mask; 디졸브 front 밴드(`abs(mask-0.5)<edge`)에 **핫 림 emissive**(시안 HDR 4~8) |
| **EdgeMotes** (Sprite) | Sprite | **Initial Mesh Reproduction Sprite**(메시 표면서 방출)+Mesh Update, SpawnBurst 400~800, Lifetime 0.5~1.0, **Sprite Size 3~8cm**, +Z 상승 60~120 + 약 Curl, alpha fade | MI_LevyGlow류 additive, tint 시안 HDR (1.0,3.0,4.0) |
| **Column** (Sprite/Mesh) | Sprite | 수직 글로우 기둥, Lifetime 0.4, scale_curve 위로 grow→fade | additive, 시안 HDR |
| **Flash** (Sprite) | Sprite | t=0 1~2개 pop, Lifetime 0.12, scale POP_OVERSHOOT | additive 흰-시안 HDR 6~10 |

**핵심원칙**: 디졸브는 **메시 머티리얼의 threshold(시간 구동)**가 만들고, 입자는 **디졸브 엣지에서 방출**(Initial Mesh Reproduction Sprite). 재등장은 Threshold 1→0 역재생.
**#1 실수**: 메시 디졸브 없이 입자만으로 흉내 → 반짝이만 보이고 "분해" 안 읽힘. 엣지 밴드 emissive 없으면 디졸브 라인이 안 보임.

---

### 에너지 실드 버블 + 피격 리플
출처(참고): CGHow energy shield 계열. realtimevfx.com 실드 스레드.

| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **ShieldBody** | **Mesh(구체)** | SpawnBurst 1, Lifetime=실드 지속(루프면 Infinite + Deactivate로 종료), Mesh Scale=캐릭터 반경 | **Fresnel 림**(Exp 2~4) × 헥스 패턴 텍스처(Panner 스크롤 0.02~0.1) × tint HDR; Translucent + TwoSided; Opacity = Fresnel + 패턴 |
| **HitRipple** | Mesh(구체) 또는 Decal | 피격 시 1개, `User.HitPos`서 SphereMask 반경 0→1 over 0.3s | 같은 머티리얼에 `SphereMask(HitPos, growingRadius)` 밴드를 emissive로 더함 |
| **HitSparks** (Sprite) | Sprite | 피격점 burst 8~15, Lifetime 0.3, Size 4~10cm, radial 200~500 +Drag | additive 임팩트색 HDR |

**핵심원칙**: 실드 = **단일 메시 파티클 + Fresnel 머티리얼**(림-라이트 버블). 피격은 같은 머티리얼에 **dynamic param(HitPos/Radius) 리플**로 — 새 지오메트리 스폰 아님.
**#1 실수**: 불투명 구체(Fresnel 없음) = 단단한 공처럼 보임(에너지장 X). 헥스/스크롤 없으면 죽은 표면. 피격 리플을 별 메시로 스폰=무겁고 위치 안 맞음 → dynamic param이 정답.

---

### 체인 라이트닝 / 전기 빔 (A→B 아크, 연쇄)
출처: **Epic 공식문서 "How to Create a Beam Effect in Niagara" (UE5.7)** — 아래 값 다수 공식 검증.

**템플릿: `Static Beam`** (빔 모듈 셋 포함). 단일 아크 스택(공식값):

| 그룹 | 모듈 | 핵심 값 |
|---|---|---|
| Emitter Update | **Beam Emitter Setup** | `Absolute Start`✅ + `Absolute End`✅ + `Use Beam Tangents`✅ |
| Emitter Update | **Spawn Burst Instantaneous** | `Spawn Count` **35** |
| Particle Spawn | **Initialize Particle** | `Lifetime` **0.2** |
| Particle Spawn | **Spawn Beam** | (빔 따라 입자 분포) |
| Particle Spawn | **Beam Width** | 커브 `Ramp Up Down`, `Scale Curve` **5** |
| Particle Update | **Update Beam** | (빔 형상 유지) |
| Particle Update | **Jitter Position** | `Jitter Amount` **15**, `Jitter Delay` **-0.01** ← 지그재그 핵심 |
| Particle Update | Color | over-life 깜빡임/페이드 |

- **렌더러**: Ribbon, `Curve Tension` **0.5**. 일렉트릭 머티리얼: 노이즈 **Panner**(1~3) UV distort + emissive **HDR 시안/흰** (0.4,2.0,5.0)~(2,4,6), Additive.
- **시작/끝**: Beam Start/End를 **User Parameter**(`Beam_End`)로 → BP/액터서 타격 위치 주입.
- **연쇄**: 빔 이미터 N개, 각자 `Beam_End` 다음 타겟, SpawnBurst stagger(0.05s).

**핵심원칙**: 번개 = **Ribbon+Beam으로 A→B 고정** 후 **Jitter Position(15)** 지그재그. 각진 번개는 **Jitter가 정답**(Curl 아님).
**#1 실수**: 스프라이트/벨로시티로 흉내 → 점들 흩어짐. Beam Width 커브 없으면 균일 두께. Jitter 0이면 직선.

---

### 지면 균열 / 어스 셰터 (내려찍기 충격)
출처(참고): CGHow "Ground Breaker"/"Ground Breaking Impact FX". (구조+표준값 빌더블)

| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **Debris** (돌조각) | **Mesh**(저폴리 락 2~4종) | **Torus/Cone Location**서 SpawnBurst **15~40**, velocity radial+상향 **300~700**, **Gravity Force -980**, Lifetime 1.5~3, **Mesh Rotation 랜덤**+Angular Velocity, **Scale 랜덤 0.3~1.2**, size-fade | 불투명 락, 필요시 emissive 용암 균열 |
| **Dust** (먼지구름) | Sprite | SpawnBurst **30~60**, Lifetime 1.5~2.5, radial 느림 **100~250**+**Drag**, Sprite **80~200cm grow** | 소프트 스모크(SubUV), 회갈색 |
| **CrackDecal** (균열) | **Decal**/평면 Mesh | crack 텍스처 Opacity Mask, scale 0→1 over **0.2s**, (용암형)엣지 hot emissive | dissolve로 균열 번짐 |
| **Shockwave** (충격 링) | Mesh(평면 링) | scale 0→대 over **0.3s**, Fresnel/alpha fade | 반투명 먼지 링 |
| **Flash** | Sprite | t=0 1~2 pop, Lifetime 0.1, POP overshoot | additive 흰-노랑 HDR |

**핵심원칙**: 무게감 = **Mesh 디브리(중력 -980 + 포물선)** + **바닥 붙는 먼지(Drag)** + **균열 데칼(시간구동 dissolve)** 3박자. 디브리는 반드시 회전 랜덤.
**#1 실수**: 디브리 중력 없음=우주부유. 먼지 Drag 없음=폭발처럼 멀리. 균열 데칼 생략=지면에 "박힌" 느낌 없음.

---

### 빙결 / 프로스트 (얼음 결정 생성 + 서리)
출처(참고): CGHow "Ice FX/Crystal FX". (KB기법+표준값 빌더블)

| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **IceCrystal** (결정) | **Mesh**(크리스탈 2~3종) | 표면/바닥서 SpawnBurst **8~20**, **scale_curve 0→1 over 0.25s**(생성 pop), Mesh Rotation 랜덤, Lifetime 길게/Infinite | Fresnel 림(Exp 2~4)+Subsurface 창백한 파랑 (0.6,0.85,1.0)+**Bump Offset**(서리 깊이), Translucent |
| **FrostSpread** | Decal/평면 | dissolve threshold 0→1 over 0.4s | 노이즈 dissolve, 흰-시안 faint emissive |
| **Shards/Flakes** | Sprite | SpawnBurst 30~60, Lifetime 0.6~1.2, Size 2~6cm, 약 +Z drift+Curl, alpha fade | additive 창백한 시안 HDR (1,3,4) |
| **Mist** (한기) | Sprite | SpawnRate 느림, Lifetime 2~3, 큰 소프트 위로 천천히, 낮은 alpha | 소프트 흰-파랑 |

**핵심원칙**: 얼음 = **결정 메시 "생성"(scale_curve pop)** + **서리 번짐(머티리얼 dissolve)** — 떨어지는 입자 아님. Fresnel+SSS가 "차가운 유리".
**#1 실수**: 결정 풀사이즈 스폰(생성감 0). 불투명 결정=플라스틱. 서리 번짐 생략=결정만 둥둥.

---

### 독구름 / 독성 가스 (포이즌 클라우드)
출처: **Epic 공식문서 "How to Create a Smoke Effect Using Sprite Particles" (UE5.7)** — 베이스 스모크 공식 검증값.

**베이스 = 공식 스모크 스택:**
| 그룹 | 모듈 | 핵심 값 |
|---|---|---|
| (템플릿) | **Simple Sprite Burst** | 시작점 |
| 렌더러 | Sprite | Material `M_smoke_subUV`, **SubImageSize (8,8)**, **SubUV Blending ✅** |
| Emitter Update | **Spawn Rate** | **50** |
| Emitter Update | **Emitter State** | Life Cycle Self, Loop Infinite |
| Particle Spawn | **Initialize Particle** | Lifetime **Random 2.0~3.0**, **Sprite Size Random Uniform 75~200**, Rotation Direct Normalized **0.25~0.5** |
| Particle Spawn | **Add Velocity** | Random Range Vector **min(0,0,50)~max(1,1,200)** |
| Particle Spawn | **Shape Location** | Sphere **Radius 64**, Random |
| Particle Spawn | **SubUV Animation** | Linear, **Start 0 End 63** |
| Particle Update | **Acceleration Force** | **(0,0,500)** ← 위로 피어오름 |

**독성 레이어**: 틴트=독성 그린 (0.3,0.9,0.2)/보라 (0.5,0.2,0.7), emissive 약(HDR 1~2). Bubbles(거품)=SpawnRate 8~15, +Z 30~80+Drag, additive 그린 HDR (0.4,2.0,0.3). GroundPool=바닥 독 웅덩이 dissolve+faint 펄스.

**핵심원칙**: 독구름 = **공식 스모크 베이스(SubUV+Accel 위로+Sphere)** + 독성 틴트 + **거품만 발광**(유독 가독성). 연기는 emissive≈0.
**#1 실수**: 전체 밝은 그린=마법처럼. SubUV 없으면 평평 카드. Accel 없으면 안 퍼짐.

---

### 홀리 라이트 / 갓레이 (천상의 빛기둥)
출처(참고): CGHow/realtimevfx "Celestial Beam"/"Rays FX". (KB기법 빌더블)

| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **LightShaft** (빛기둥) | **Mesh(원뿔/실린더)** | scale Y로 길이, 살짝 회전 | 세로 스트라이프 **Panner 아래로 0.1~0.3**+**Fresnel**(Exp 2~3)+**Depth Fade**+emissive 따뜻한 흰-금 HDR (6,5,3)~(10,8,4), Translucent Additive, TwoSided |
| **Ring** (디졸빙 링) | Mesh(평면 링) | scale 0→1 over 0.5s, dissolve | additive 금빛, Erosion |
| **Motes** (떠오르는 입자) | Sprite | SpawnRate 20~40, Lifetime 1.5~3, **+Z 상승 40~100+약 Curl(50/0.2)**, Size 2~6cm, alpha fade | additive 금-흰 HDR (4,3,2) |
| **GroundGlow** | Decal/평면 | 바닥 원형 글로우, 펄스 | additive radial gradient 금빛 |

**핵심원칙**: 갓레이 = **원뿔/실린더 메시 + 세로 스트라이프 Panner 레이 머티리얼**(볼류메트릭 모사) + Fresnel 가장자리 페이드. 입자는 **위로 떠오르는 motes**(빛 속 먼지).
**#1 실수**: 불투명 원뿔=고체 콘. Fresnel/DepthFade 없으면 메시 경계/바닥선 드러남. Panner 없으면 죽은 빛. motes는 떠올라야(하강 X).

---

### 토네이도 / 회오리 (스월 볼텍스)
출처(참고): CGHow "Stylized/Fluid Tornado". (구조+KB 빌더블)

| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **Funnel** (깔때기) | **Mesh(토러스 적층/콘)** | 깔때기형, Mesh Rotation 세로축 빠른 스핀, dynamic param tiling/erosion/size 입자별 랜덤 | 노이즈 **Panner 가로 빠름 0.5~1.5**+**Erosion**+tiling 랜덤, Translucent |
| **DustSpiral** | Sprite(GPU) | SpawnRate 100~300, Lifetime 1.5~3, **Vortex Force**+약 +Z+Curl, 바닥 스폰 | 소프트 스모크, 어두운 회색 |
| **GroundKick** | Sprite | 접지부 burst, radial+Drag, 큰 소프트 | 먼지 |
| **Debris** | Mesh | 소량 burst, Vortex+상승, 회전 랜덤, Lifetime 2~4 | 락/나뭇잎 |

**핵심원칙**: 토네이도 = **깔때기 메시(가로 Panner=회전) + Vortex Force로 입자 나선 상승**. 핵심 모션은 **Vortex Force**+약한 +Z.
**#1 실수**: Vortex 없이 Curl만=무작위 난류. 메시 Panner 없으면 정지 깔때기. GroundKick 생략=공중에 뜬 회오리.

---

### 회복 / 버프 오라 (라이트 발광 + 상승 흐름)
출처: **Epic 공식문서 "How to Create Particle Effects That Emit Light" (UE5.7)** — Light Renderer 공식 검증값.

**발광 베이스 = 공식 스택 (색만 회복 그린):**
| 그룹 | 모듈 | 핵심 값 |
|---|---|---|
| (템플릿) | **Fountain** | 시작점 |
| Emitter Update | Emitter State | Life Cycle **System** |
| Emitter Update | **Spawn Rate** | **500** |
| Particle Spawn | **Initialize Particle** | Lifetime Random **1.75~2.5**, **Color RGB(0.1,0.3,50)** ← **>1=emissive**(회복=그린 (0.2,50,0.3)), Sprite Size Random Uniform **2.5~8.0** |
| Particle Spawn | **Sphere Location** | Radius **15** |
| Particle Spawn | **Add Velocity in Cone** | Min **300** Max **600**(회복=완만 +Z) |
| Particle Update | Gravity/Drag/**Collision** | 기본(회복은 Gravity 약/음수=떠오름) |
| 렌더러 | **Light Renderer** | **Radius Scale 5.0**, **Color Add RGB(0,0,15)**(회복=(0,15,0)) ← 입자색과 별도 광원색 |

**회복 연출**: 입자 Color G채널 >1 그린 발광 + Light Renderer Color Add G 15. RisingTube(원통 메시+Panner 위로 0.1~0.3). 바닥 Ring pulse. 상승=Gravity 음수(−100~−300)/+Z.

**핵심원칙**: 발광=**입자 Color>1(에미시브) + Light Renderer 씬 라이팅** 2중. 회복=상승+그린, 색만 교체(공격=레드, 마나=블루).
**#1 실수**: Light Renderer 없이 에미시브만=주변 안 밝음(어색). Color ≤1=안 빛남. 회복인데 하강=데미지/저주처럼.

---

### 폭포 / 물줄기 (워터폴)
출처(참고): CGHow "Waterfall"/"Water Wake Foam". (구조+KB 빌더블)

| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **WaterSheet** | **Mesh(평면/곡면)/Ribbon** | 위→아래 흐름, Gravity 가속 | **Refraction**(1.33)+**Normal Panner 아래로**(리플)+opacity, 청록 tint, Translucent |
| **FallStreaks** | Sprite/Ribbon | SpawnRate 80~200, **Gravity -980**, Lifetime 1~2, 세로 늘인(velocity-aligned), Drag 약 | 흰-청록 soft, alpha fade |
| **MistSpray** | Sprite | SpawnRate 40~100, Lifetime 1~2, 느린 부유+Drag, 큰 소프트 | 흰 soft, 낮은 alpha |
| **FoamBase** | Sprite | 착수점 burst, radial+Drag, **노이즈 radial gradient** 유기 거품 | 흰 거품 dissolve |
| **Splash** | Sprite | 착수 burst 10~30, 포물선 상향+Gravity | 흰-청록 droplet |

**핵심원칙**: 물 = **굴절+노멀 Panner 머티리얼** + **중력 낙하 줄기** + **바닥 거품(노이즈 radial dissolve)** + 물안개. 거품은 노이즈 마스크 필수.
**#1 실수**: Refraction 없으면 불투명판. Foam/Splash 생략=바닥에 안 닿는 느낌. Mist 없으면 물보라 없음.

---

### 화염방사 / 불줄기 (플레임 스트림)
출처(참고): Epic 커뮤니티 "Flamethrower with Niagara Fluids", yarsalabs UE5 fire.

**A. 스프라이트(가벼움, 범용):**
| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **FireStream** | Sprite(GPU) | **Add Velocity in Cone**(전방 Min 400 Max 900), SpawnRate 150~400, Lifetime 0.5~1.0, **Curl Noise(200~400, Freq 0.3)**, Drag 1~3, Size 10~40cm grow→shrink | **SubUV 불 플립북**(8×8)+emissive 그라데이션(흰-노랑→주황→빨강, ParticleRelativeTime) HDR 8~12, Additive |
| **HeatHaze** | Sprite | 전방 SpawnRate 20~50, Lifetime 0.5~1 | Refraction distortion만 |
| **Smoke** (끝) | Sprite | 줄기 끝 SpawnRate 30~60, Lifetime 1.5~2.5, 위+전방 약, Drag | 어두운 SubUV 연기 |
| **Embers** | Sprite | burst, 전방+Curl, Lifetime 0.5~1.2, 2~5cm, Drag | additive 주황 HDR, alpha fade |

**B. Niagara Fluids(사실적·무거움):** `Grid 3D Gas Fire` 템플릿, SpawnRate 20000, Gravity 비활성, `Velocity Mult -0.1`, Mesh Reproduction Sprite(캐릭터 표면). GPU 비싸 시네마틱/근접용.

**핵심원칙**: 화염방사 = **Add Velocity in Cone + Curl Noise + SubUV 불 + 끝 연기 전환**. 색 흰-노랑(뜨거움)→주황→빨강(식음) over-life.
**#1 실수**: Curl 없는 직선=죽은 콘. 단색 주황=평평. 끝 연기 없으면 허공서 끊김. Fluids는 perf 폭락 주의.

---

### 마법진 / 룬 소환 (매직 서클)
출처(참고): CGHow "Advanced Summoning Ring FX"/매직서클. (KB 빌더블)

| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **RuneDisk** | **Mesh(평면 디스크)** | scale 0→1 over 0.4s, 천천히 회전, 끝나면 dissolve | **폴라 UV 룬**: 룬텍스처 **PolarCoordinates**→**Panner(각도축 0.05~0.2)**+동심 링 마스크+**Erosion dissolve**+emissive HDR(아케인=보라(3,1,5)/홀리=금(8,6,3)), Translucent Additive Unlit |
| **OuterRing** | Mesh(링)/Ribbon | 역방향 회전, 가는 글로우 링 | additive 동색 HDR |
| **RisingGlyphs** | Sprite | SpawnRate 10~25, Lifetime 1~2, **+Z 50~120+약 Curl**, Size 5~15cm, alpha fade, 룬 SubUV | additive 동색 |
| **ChargeCore** | Sprite/Mesh | 중심 글로우 scale pulse, 절정 flash | additive 흰-동색 HDR 6~10 |
| **Flash** | Sprite | 완료 t에 1~2 pop, Lifetime 0.15, POP | additive 흰 HDR 10+ |

**핵심원칙**: 마법진 = **폴라 UV + Panner(각도축)로 회전하는 룬 머티리얼** + Erosion dissolve 등장/퇴장. 회전 방향 레이어별 반대.
**#1 실수**: 룬 회전하려 메시 통째 돌림(텍스처 같이 돌아 어색) → 폴라UV Panner가 정답. dissolve 없으면 등장감 X. 단색=평평.

---

### 바람 / 돌풍 (윈드 거스트)
출처(참고): CGHow 리본 트레일, 80.lv 리얼리스틱 윈드, Epic WindDirectionalSource.

| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **WindStreaks** | **Ribbon**/늘인 Sprite | SpawnRate 20~50, Lifetime 0.5~1.5, **directional velocity 300~800**, 약 Curl, **Ribbon Tessellation OFF=직선** | 흰-투명 soft, 가로 alpha 그라데이션, Translucent |
| **Leaves/Debris** | Sprite/Mesh | SpawnRate 10~30, Lifetime 1.5~3, directional+**Curl(난류)**+Mesh Rotation(팔랑), Drag | 나뭇잎/먼지 |
| **DustGust** | Sprite | SpawnRate 15~40, Lifetime 1~2, directional+Drag, SubUV | 옅은 회갈색 soft |

**엔진 통합**: 씬 전역=**WindDirectionalSource 액터**(**Min/Max Gust Amount**로 강도 변동). Niagara는 Vector Field/directional Curl로 동조.
**핵심원칙**: 바람은 안 보임 → **운반체(줄기/잎/먼지)로 시각화**. 줄기는 **Ribbon Tessellation OFF로 직선**+가로 alpha. 잎은 Curl로 팔랑.
**#1 실수**: 줄기 Tessellation 켜두면 흐물. 운반체 없으면 안 보임. 잎 회전 없으면 미끄러지듯 부자연.

---

### 환경 앰비언트 (반딧불 / 먼지 모트 / 부유 입자)
출처(참고): CGHow "Ambient Dust"(2×2 UV), Epic 커뮤니티 "Fog Mist Dust". (가장 쉬운 효과군)

| 종류 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **DustMotes** | Sprite | **Box/Volume Location**+SpawnRate 10~30, **Lifetime 5~10**, **느린 Curl(20~60, 0.05~0.1)** 부유, Size 1~4cm, alpha 0.1~0.3 | 소프트 글로우, **2×2 UV** 약변화, faint 흰 |
| **Fireflies** | Sprite+**Light Renderer** | Box+SpawnRate 3~10, Lifetime 4~8, 느린 Curl+약 +Z, Size 2~5cm, **blink**(alpha sin) | additive 따뜻한 황록 HDR (3,4,0.5), Light Renderer 약(Radius Scale 1~2) |
| **Fog/Mist** | Sprite | Box 넓게+SpawnRate 5~15, Lifetime 6~12, 거의 정지, 200~500cm, **DepthFade soft particle** | 옅은 회백 translucent, 낮은 alpha |

**핵심원칙**: 앰비언트 = **Box/Volume Location 공간 채움** + **느린 Curl 부유** + **긴 Lifetime+루프**. 반딧불=**Light Renderer+blink**. 안개=**DepthFade 필수**.
**#1 실수**: SpawnRate/속도 과다=눈보라처럼 산만. blink 없으면 그냥 점. 안개 DepthFade 없으면 카드 경계선. alpha 과다=방해물.

---

### 소드 슬래시 / 멜리 트레일 (검 궤적)
출처: **Epic 공식 "How to Create a Ribbon Effect" (UE5.7)** 리본 스택 검증 + CGHow Sword Trails/Slash.

**리본 트레일 베이스 = 공식 리본 스택:**
| 그룹 | 모듈 | 핵심 값 |
|---|---|---|
| (템플릿) | **Simple Sprite Burst** → 리본화 | 시작점 |
| Emitter Update | Emitter State | Life Cycle **System** |
| Emitter Update | **Spawn Rate** | **100** |
| Particle Spawn | **Point Attributes** | Lifetime **5**(트레일은 0.3~0.6), Mass 10 |
| Particle Spawn | **Ribbon Attributes** | **Ribbon Width 10**(검=20~60) |
| Particle Spawn | Shape Location | **검 소켓 위치**(Skeletal Mesh Location/소켓 바인딩) |
| 렌더러 | **Ribbon Renderer** | **Curve Tension 0.5**, **Tessellation Automatic**, Ribbon Width Binding, UV0 Tiling Distance |

**슬래시 레이어**: Trail(궤적, Lifetime 0.3~0.6=칼맛, Width 커브 중간 굵게, 가로 그라데이션 emissive+UV Panner). SlashDisk(부채꼴 메시 erosion dissolve, emissive HDR). Sparks(궤적 따라 burst 8~20, velocity-aligned). Flash(스윙 피크 1 pop).

**핵심원칙**: 트레일 = **Ribbon(검 소켓 스폰, Tessellation Auto 곡선) + 짧은 Lifetime(0.3~0.6=날카로움) + Width 커브**. AnimNotifyState로 스윙 구간만 스폰.
**#1 실수**: Lifetime 길면 둔함. Tessellation Disabled=각진 리본. Width 균일=밋밋. 소켓 바인딩 없으면 검 안 따라감.

---

### 비 / 눈 (날씨 파티클)
출처(참고): Epic 커뮤니티 Rain/Snow Niagara, CGHow Snowfall. (구조+KB 빌더블)

| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **RainDrops** | Sprite(**GPU**) | **Box Location 카메라 위 부착**, SpawnRate 500~2000, **Gravity -980~-1500**, Lifetime 1~2, **velocity-aligned 세로 streak**, Size 작게 | 흰-청록 반투명 streak |
| **Splash** | Sprite | **GPU Collision** 이벤트 또는 바닥 burst, radial 작게+위로, Lifetime 0.3 | 흰 작은 ring/droplet dissolve |
| **눈 SnowFlakes** | Sprite(GPU) | Box 카메라 위, SpawnRate 200~800, **Gravity 약 -50~-150**, **Curl(흩날림 30~80)**, Rotation 랜덤, Lifetime 4~8, Size 2~6cm | 눈송이(2×2 UV), 흰 soft |

**핵심원칙**: 날씨 = **Box Location 카메라/플레이어 위 부착**(맵 전체 금지=perf). 비=빠른 중력+세로 streak+착지 splash, 눈=느린 중력+Curl 흩날림. **GPU sim 필수**.
**#1 실수**: 맵 전체 스폰=perf 폭락. velocity-align 없으면 비가 점. splash 없으면 안 닿음. 눈 Curl 없으면 수직 낙하.

---

### 다크 오라 / 보이드 (빛 흡수형 — 역발상)
출처(참고): OBLIVIO_EFFECTS 다크오라 + realtimevfx void 스레드. (Horror 톤)

| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **VoidCore** | **Mesh(구체)** | scale pulse 느림, Lifetime 지속/Infinite | **Translucent 어둠**(가산 금지): 베이스 검정, **Fresnel sickly rim**만 약 emissive 검보라 HDR (1.5,0.3,2.0), TwoSided |
| **Tendrils** | Sprite/Ribbon | SpawnRate 20~40, Lifetime 1~2, +Z 약+**Point Attraction(코어로)**, Drag | 검보라 soft, near-black |
| **DarkMotes** | Sprite | 바깥서 스폰→**Point Attraction Force로 코어 수렴**, SpawnRate 30~60, Lifetime 1.5~2.5, Size 2~5cm | 어두운 보라, 약 emissive |
| **Distortion** | Sprite | 코어 주변 굴절, SpawnRate 10~20, Lifetime 1~2 | **Refraction만**(색 거의 0) |
| **RimSparks** | Sprite | 소량 burst, 느린 부유, Lifetime 1~2 | additive 검보라/독록 약 HDR(2,0.5,2) |

**핵심원칙**: 다크/보이드 = **가산 금지**(빛 흡수). Translucent로 어둠 쌓고 Fresnel sickly rim만 약 발광. 입자는 **Point Attraction으로 코어에 빨려듦**(수렴=불길). Horror 톤(채도↓, emissive<0.5, 2~5s).
**#1 실수**: 가산 블렌드로 밝게=마법광선. 입자 바깥 방출=내뿜는 느낌(보이드는 삼킴). 너무 밝은 rim=불길함 상실.

---

### UI / 픽업 글로우 (아이템 획득·하이라이트)
출처(참고): 일반 게임 UI VFX + Revvy 팩 글로우. (앰비언트 루프 + 획득 버스트 2상)

| 레이어 | 렌더러 | 핵심 값 | 머티리얼 |
|---|---|---|---|
| **AmbientGlow** | Sprite | SpawnRate 5~15, Lifetime 1~2, 거의 정지 약 +Z, Size pulse(sin), 루프 | additive 소프트 글로우 HDR(전설=금(8,6,2)/레어=청(2,4,8)) |
| **RisingWisps** | Sprite | SpawnRate 8~20, Lifetime 1~2, +Z 30~80+약 Curl, Size 2~6cm, alpha fade | additive 동색 HDR 스파클 |
| **GroundRing** | Mesh(평면 링)/Decal | scale pulse(sin), 희미 | additive radial, dissolve 약 |
| **PickupBurst** | Sprite | 획득 t에 burst 15~30, radial 200~400+Drag, Lifetime 0.4 | additive 흰-동색 HDR |
| **PickupFlash** | Sprite | 획득 t에 1~2 pop, Lifetime 0.15, POP | additive 흰 HDR 10+ |

**핵심원칙**: 픽업 = **상시 앰비언트 루프(절제, 희소가치 색)** + **획득 순간 버스트+플래시(use*/이벤트 트리거)**. 색=등급 신호(금=전설, 청=레어). FEEL 페어링.
**#1 실수**: 상시 글로우 너무 화려=획득 버스트 안 도드라짐. 색 등급 구분 없으면 정보 전달 실패. 획득 피드백 없으면 "주웠다" 감각 약함.

---

## 부록: 모듈 레퍼런스 (Epic UE5.7 공식 Group References)
출처: Emitter Update / Particle Spawn / Particle Update Group Reference (dev.epicgames.com, UE5.7). 80+ 모듈 — 자주 쓰는 것만. **스테이지가 모듈 위치를 정함**. 모듈은 스택 위→아래 실행.

**Emitter Update 그룹**:
- **Emitter State** — `Life Cycle Mode`(System/Self) · `Loop Behavior`(Once/Multiple/Infinite) · `Loop Duration` · **`Inactive Response`(Complete/Kill/Continue)**.
- **Spawn Rate**(지속 초당) · **Spawn Burst Instantaneous**(`Spawn Count`·`Spawn Time`) · **Spawn Per Frame** · **Spawn Per Unit**(트레일).
- **Scalability** — Distance/Visibility Culling·Reset Age on Awaken. **항상 포함 권장**.

**Particle Spawn 그룹**:
- **Initialize Particle** — `Lifetime`·`Position`·`Mass`·`Color`·`Sprite Size`·`Sprite Rotation`. **Initialize Ribbon**(`Ribbon Width`·`Ribbon Twist`).
- **Location**: `Box`·`Sphere`(반구)·`Cylinder`·`Cone`·`Torus`·`Grid`·`System`·**`Static Mesh`(표면)**·**`Skeletal Mesh`(본/소켓/삼각형/버텍스)**.
- **Velocity**: `Add Velocity` · `Add Velocity in Cone`(콘 각도) · `Add Velocity from Point`(falloff).

**Particle Update 그룹**:
- **Forces**(전부 Physics.Force 누적→Solve 필요): `Gravity Force`(cm/s) · **`Drag`(질량 무관)** · `Curl Noise Force` · **`Vortex Force`(축 회전+원점 당김 — 토네이도)** · `Point Attraction Force` · `Acceleration Force`.
- **Size/Color**: `Scale Color`(alpha fade) · `Scale Sprite Size`(grow/shrink 커브).
- **Collision** — **반드시 Solve 직전**. CPU=레이캐스트, GPU=씬뎁스/거리필드.

**활용 핵심**: Force류는 **Solve Forces and Velocity 없으면 무반응**(빈번한 "force 안 먹힘"). Collision은 Solve 직전. 토네이도=Vortex(Curl 아님). 트레일=Spawn Per Unit+Initialize Ribbon.

---

## 부록: 렌더러 속성 레퍼런스 (Epic UE5.7 공식 Render Module Reference)

**Sprite**: Material(**Use with Niagara Sprites 필수**)·Alignment·Facing Mode·Pivot(0.5,0.5)·**Sub Image Size**(SubUV 프레임수)·**Sub UV Blending**·Sort Mode·Sprite Size Binding(기본 50,50).
**Mesh**: Particle Mesh(**Niagara Mesh Particles 필수**)·**Override Materials**(배열)·Facing Mode(Default/Velocity/Camera Position/Camera Plane)·Scale Binding(1,1,1).
**Ribbon**: Facing Mode·**Curve Tension(0~1)**·**Tessellation Mode(Automatic/Custom/Disabled)**·Max Tessellation(1~16)·**UV0/UV1 Tiling Distance**·Draw Direction·Width Binding(1).
**Light**: **Use Inverse Squared**·**Radius Scale**·**Color Add**(X=R/Y=G/Z=B)·Affects Translucence·**Radius Binding(100)**·Color Binding(1,1,1,1).
**Decal**: Material·**Source Mode(Particles/Emitter)**·Decal Size Binding(50,50,50)·Decal Color Binding(Particles.Color).

**활용 핵심**: Sprite/Mesh 머티리얼은 **Use with Niagara 플래그** 없으면 안 보임("안 보임" 1순위). Mesh 머티리얼 교체=**Override Materials 배열**. SubUV=**Sub Image Size**. 리본 곡선=Tessellation Automatic+Curve Tension. Light=Radius Scale+Color Add.

---

## 부록: 핵심 셋업 패턴 (Epic UE5.7 공식 how-to 검증)

**메시 파티클** (Balloon Effect 공식): Fountain→**Sprite Renderer 삭제+Mesh Renderer 추가**·Particle Mesh 지정+**Override Materials**+머티리얼(Use with Niagara Mesh Particles 필수)·Emitter State System·Spawn Rate 100·Init **Mesh Scale Random Uniform 0.7~1.0**+Sprite 속성 Unset·Add Velocity Random Range(min(15,25,50)~max(30,30,100))·Sphere R200·Gravity(10,10,40)·Drag 1.0. **핵심: Mesh Scale(Sprite Size 아님)+Override Materials. Facing Mode Velocity=디브리/발사체.**

**이벤트** (Events 공식): **`Requires Persistent IDs` 활성+CPU 시뮬만**(GPU 이벤트 X). Generate(Particle Update): `Generate Location/Death Event`, `Generate Collision Event`(**Collision 모듈 선행**). Handler: **`Event Handler Properties`**(Source·Max Events Per Frame·Spawn Number)+`Receive Location/Death/Collision Event`. 활용: 불 죽으며 연기(Death), 충돌점 스플래시(Collision), 트레일 서브이미터(Location).

**GPU 시뮬** (GPU Sprite 공식): Sim Target→**`GPUComputeSim`**·**`Fixed Bounds` 필수**(GPU는 크기 못 읽음). 대량(수천+)=GPU. 단 **이벤트·CPU Collision 불가**. Fixed Bounds 안 잡으면 컬링 사라짐.

---

## 부록: 머티리얼 연동 · 퍼포먼스 · 게임플레이 통합 (Epic UE5.7 공식)

**Niagara→머티리얼**: **Particle Color 노드** 필수(없으면 색 안 먹음)→Emissive/Opacity. **Color>1=emissive 발광**. **Dynamic Material Parameters**: **4개×각 4 float=16채널**(모듈→머티리얼 Dynamic Parameter 노드). over-life는 ParticleRelativeTime 구동이 간단.

**퍼포먼스** (Scalability 공식): **Effect Type 에셋**(레벨/플랫폼별 스케일). **GPU 입자 1개=CPU 자원 ~64개분 → 극소수 입자(코어/플래시)는 CPU 유리**. **Fixed Bounds>Dynamic**. 한프레임 대량버스트 금지(분산). Warmup 금지. 이미터 수↓(GPU도 System/Emitter 스크립트는 CPU). **Spawn Index**로 변형(복제 대신). 한 이미터 렌더러 여러개. 풀링(Spawn at Location/Attached+prime, 동적로드 prime 금지). Attribute Reader>Events. 정렬 기본 끄기. CVar(`fx.Niagara.EmitterBounds.*`, `Niagara.GPUSorting/Culling.CPUToGPUThreshold`).

**게임플레이 통합**: `Spawn System at Location`/`Spawn System Attached`(풀링 지원). GAS **GameplayCueNotify_Static(순간 임팩트)/_Actor(지속 오라 OnActive·OnRemove)**, Cue Tag 트리거, 히트위치=GameplayEffectContext hit result. AnimNotifyState(스윙 구간 트레일 on/off). FEEL 동시성(Cue=VFX+SFX+셰이크+hitstop 진입점).

---

## Sources
- **Epic 공식문서**: How to Create a Beam Effect in Niagara (UE5.7) — Static Beam/Jitter Position/Beam Width 검증.
- **Epic 공식문서**: How to Create a Smoke Effect Using Sprite Particles (UE5.7) — Simple Sprite Burst 전체 검증값.
- **Epic 공식문서**: How to Create Particle Effects That Emit Light (UE5.7) — Fountain+Light Renderer 검증값.
- **Epic 공식문서**: How to Create a Ribbon Effect (UE5.7) — 리본 스택 검증.
- **Epic 공식문서**: Render Module Reference / Emitter Update·Particle Spawn·Particle Update Group Reference (UE5.7) — 부록.
- **Epic 공식문서**: Balloon(Mesh Particle)·Events and Event Handlers·GPU Sprite Effect·Scalability and Best Practices (UE5.7) — 부록.
- **Epic 커뮤니티**: Flamethrower with Niagara Fluids, yarsalabs Grid3D Gas Fire; Fog/Mist/Dust·Rain/Snow; WindDirectionalSource.
- CGHow(Ashif Ali): Teleport/Disintegration/Ground Breaker/Ice/Lightning/Waterfall/Tornado/매직서클/Sword Trails; realtimevfx.com 포럼. — 구조 참고(상세값 영상).
- 본 레시피 값 = Epic 공식문서 + NIAGARA_MAKING 검증 기법 + 실시간 VFX 표준 기준값(빌더블).
