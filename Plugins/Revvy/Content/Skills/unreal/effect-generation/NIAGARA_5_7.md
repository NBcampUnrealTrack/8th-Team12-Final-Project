# Niagara 5.7 정확 스펙 (프로그램 작성용)

UE 5.6/5.7 + Revvy API 기준. 모든 수치/이름은 코드 생성 시 그대로 사용 가능한 캐노니컬 형태.

## (a) 핵심 모듈 입력 핀 사양

> **최우선 규칙: `Lifetime`은 Initialize Particle 모듈의 float rapid iteration 파라미터다.**
> Emitter State의 `Life Cycle Mode` enum(Self/System)과 전혀 다른 것. "수명 설정"을 Emitter State에서 하려는 시도는 항상 실패한다.
> - 파티클 수명: `Constants.<Emitter>.Initialize Particle.Lifetime` (float, ParticleSpawn 스테이지)
> - 이미터 수명/루프: Emitter State의 `Loop Behavior` + `Loop Duration` (EmitterUpdate 스테이지)

### Emitter State (EmitterUpdate 스테이지)

| 입력 핀 (표시명) | 타입 | 값 항목 / 기본값 | 설정 방법 |
|---|---|---|---|
| Life Cycle Mode | **enum (static switch)** | `Self` \| `System` | 읽기: `get_emitter_lifecycle()` → `life_cycle_mode`. 쓰기: `set_emitter_lifecycle(path, emitter, "Self", "Once")` 권장 |
| Loop Behavior | **enum (static switch)** | `Once` \| `Multiple` \| `Infinite` | 전용 `set_emitter_lifecycle()`이 표시명을 내부 enum명으로 변환하고 compile/readback까지 검증 |
| Loop Duration | float | 1.0 (초) | rapid iteration param |
| Loop Duration Mode | (UI 전용) | — | 현재 API 미노출 |
| Loop Delay | float | 0.0 | rapid iteration param — 다중 링 스태거에 사용 |
| Loop Delay Recalculate Each Loop | bool | false | |
| Duration Recalculate Each Loop | bool | false | |
| Inactive From Start | bool | false | |
| (내부) Execution State | enum | `Active` \| `Inactive` \| `InactiveClear` \| `Complete` | Revvy `ReadEmitterStateSettings`가 InactiveResponse로 읽음 |

주의: 웹 튜토리얼에서 Loop Behavior를 `Once | Loop | LoopingWithDelay`로 표기하는 경우가 있으나, **엔진/Revvy가 검증한 표시명은 `Once | Multiple | Infinite`** (Revvy `GetStaticSwitchValue()` → `GetDisplayNameTextByIndex` 경로로 확인됨). 원샷 이펙트는 `Once`, N회 반복은 `Multiple`(+LoopCount), 앰비언트 루프는 `Infinite`.

### Spawn 모듈

| 모듈 | 스테이지 | 입력 | 타입 | rapid param 이름 |
|---|---|---|---|---|
| Spawn Rate (`/Niagara/Modules/Emitter/SpawnRate`) | EmitterUpdate | Spawn Rate | float | `Constants.<E>.SpawnRate.SpawnRate` |
| Spawn Burst Instantaneous (`/Niagara/Modules/Emitter/SpawnBurst_Instantaneous`) | EmitterSpawn | Spawn Count | **int** | `Constants.<E>.SpawnBurst_Instantaneous.SpawnCount` |

Burst 카운트는 enum 핀이 아니라 int rapid param이다. enum 경로로 쓰려고 하지 말 것.

### Initialize Particle (ParticleSpawn 스테이지)

| 입력 핀 (표시명) | 타입 | 비고 |
|---|---|---|
| Lifetime | **float** | enum 아님. Emitter State 소속 아님. 예: Min 0.4 / Max 0.6 (음파 링) |
| Color | LinearColor (RGBA) | `'(R=1,G=0,B=0,A=1)'` 포맷 |
| Sprite Size | Vector2 | `'(X=50,Y=50)'` |
| Mass | float | |
| Mesh Uniform Scale (Min/Max 모드) | float | 예: Min 1.0 / Max 2.0 |

참고: 포럼의 "Initialize Particle deprecated" 글은 **모듈 버전 워닝**(구버전 모듈 인스턴스 → 버전 업데이트로 해결) 얘기지 모듈 자체 폐기가 아님. UE 5.x에서 Initialize Particle은 현행 표준이다.

### 기타 자주 쓰는 모듈

| 모듈 | 핵심 입력 | 타입 / enum 항목 | 기준값 |
|---|---|---|---|
| Shape Location | Shape Primitive | enum: `Box` \| `Sphere` \| `Cylinder` \| `Torus` \| `Ring/Disc` \| `Cone` | Cylinder: Height 1.0, Radius 10.0 |
| Add Velocity | Velocity Mode | enum: `Linear` \| `From Point` \| `In Cone` | From Point, Speed 20 |
| Drag | Drag | float | 8.0 (트레일/2차 레이어 감속) |
| Update Mesh Orientation | (회전 모드) | — | 구 Orient 계열 3종 대체 |

### Renderer 바인딩 기본값

| Renderer | 바인딩 | 타입 | 기본값 |
|---|---|---|---|
| Mesh Renderer | Scale Binding | Vector3 | (X=1, Y=1, Z=1) |
| Mesh Renderer | Mesh Orientation Binding | Quat | (X=0, Y=0, Z=0, W=1) |
| Sprite Renderer | Sprite Size Binding | Vector2 | (X=50, Y=50) |
| Sprite Renderer | Sprite Facing / Alignment Binding | Vector3 | (X=1, Y=0, Z=0) |
| Light Renderer | Radius Binding | float | 100 (Radius Scale은 곱연산) |
| Light Renderer | Light Exponent Binding | float | 10 |
| Light Renderer | Volumetric Scattering Binding | float | 1.0 |

머티리얼 런타임 제어는 Mesh Renderer의 **머티리얼 다이내믹 파라미터 슬롯 0–3** 사용 (머티리얼 인스턴스 없는 스태틱 메시 머티리얼 직결 금지).

## (b) enum 핀 설정 규칙 (내부명 vs 표시명)

1. **표시명을 쓴다.** `'Once'`, `'Multiple'`, `'Infinite'`, `'Self'`, `'System'` 등 에디터 UI에 보이는 그대로. Revvy 서비스가 표시명→내부명(`NewEnumerator0`…`NewEnumeratorN`)을 자동 변환하므로 표시명 사용이 정답이다. 내부명을 직접 쓰는 것은 불필요하고 깨지기 쉽다(.uasset 직렬화 디테일).
2. **숫자 금지.** enum 핀에 float/int를 쓰면 엔진이 타입 불일치로 거부한다 (암시적 변환 없음). `1.0`을 Life Cycle Mode에 쓰는 식의 코드는 절대 생성하지 말 것.
3. **lifecycle static switch는 전용 API를 쓴다.** Loop Behavior / Life Cycle Mode는 rapid iteration param이 아니라 FunctionCall 노드의 컴파일 타임 스위치다. `set_emitter_lifecycle()`은 표시명→내부명 변환, compile, graph readback을 한 계약으로 처리한다. 일반 `set_module_input()`도 enum을 지원하지만 원샷 정규화에는 전용 API를 우선한다.
4. **값 직렬화 포맷** (Python API는 전부 문자열):
   - float: `'50.0'` (따옴표 필수, raw 숫자 금지)
   - int: `'100'` / bool: `'true'` \| `'false'`
   - Color: `'(R=1,G=0,B=0,A=1)'` / Vector2: `'(X=64,Y=64)'` / Vector3: `'(X=0,Y=0,Z=100)'`
5. **rapid param 이름 포맷**: `Constants.<EmitterName>.<ModuleName>.<InputName>` — 반드시 `list_rapid_iteration_params()` 결과의 풀네임 사용. 같은 파라미터가 여러 스테이지(EmitterSpawn/EmitterUpdate/ParticleSpawn/ParticleUpdate)에 존재하면 `set_rapid_iteration_param()`(전 스테이지 일괄)을 쓴다. 한 스테이지만 세팅하면 적용이 안 되거나 부분 적용된다.
6. `set_module_input()`의 InputName은 에디터 표시명(`'Spawn Rate'`, `'Color'`) — 내부 변수명 아님. `Module.`/`Output.`/`Particles.` 접두사는 Revvy가 자동 부착.

## (c) LEGACY 모듈 → 현대 대체 매핑

| LEGACY (사용 금지) | 현대 대체 | 비고 |
|---|---|---|
| Box / Sphere / Cylinder / Torus Location | **Shape Location** (Shape Primitive enum 선택) | UE 5.0 통합 |
| Add Velocity in Point / Add Linear Velocity / Add Velocity in Cone | **Add Velocity** (Velocity Mode enum) | UE 5.0 통합 |
| Orient Mesh to Vector / Flight Orientation / Pure Roll Orientation | **Update Mesh Orientation** | UE 5.0 통합 |
| Emitter Life Cycle | **Emitter State** | UE 4.26 폐기 |
| Vortex Velocity | **Vortex Force** 또는 Scratch Pad 커스텀 | UE 5.4 제거, 공식 대체 없음 |
| Grid2DCollection `Fill Raw Texture 2D` | 이미터 object user variable로 렌더타겟 지정 | |
| Curl Noise Data Interface (구형) | **Vector Noise** | |
| Cascade 파티클 시스템 | **Niagara** | 컨버터 플러그인 있으나 미지원 모듈 다수 |
| 레거시 이미터 간 통신 | **Niagara Data Channels** | 5.7에서 액세스 컨텍스트 확장 |
| 단순 버스트용 Standard Emitter | **Lightweight Emitter** (단, world space 미지원 주의) | UE 5.5+ |
| 확장 링에 Sprite Renderer | **Mesh Renderer** + 캡 없는 실린더/플랫 토러스 | 스케일 품질/카메라 독립 |

## (d) 자주 나는 워닝 → 원인 → 수정

| 증상/워닝 | 원인 | 수정 |
|---|---|---|
| enum 핀 세팅이 무시되거나 값이 되돌아감 | 숫자(`1.0`) 또는 내부명(`NewEnumerator0`)을 씀 | 표시명 문자열(`'Once'`) 사용 |
| lifecycle 설정 후 값이 다름 | template의 static switch가 복사됐거나 잘못된 표시명 사용 | `set_emitter_lifecycle()`로 설정하고 `get_emitter_lifecycle().resolved` 및 readback 확인 |
| Lifetime 설정해도 파티클 수명 불변 | Emitter State에서 설정 시도 / 잘못된 모듈명 | `Constants.<E>.Initialize Particle.Lifetime` (ParticleSpawn, float) |
| Color/Vector가 부분 적용 | 다중 스테이지 파라미터를 한 스테이지만 세팅 | `set_rapid_iteration_param()`으로 전 스테이지 일괄 |
| "could not find input" | 내부 변수명 사용 / 풀네임 미사용 | `list_rapid_iteration_params()`의 풀네임 + 표시명 InputName |
| float 값 파싱 실패 | 따옴표 없는 raw 숫자 전달 | `'50.0'` 문자열 포맷 |
| "Initialize Particle deprecated" 워닝 | 구버전 모듈 인스턴스 (모듈 버저닝) | 모듈 버전 업데이트 — 모듈 교체 불필요 |
| 원샷 이펙트가 끝나지 않음 / 일정 시간 뒤 재등장 | `System/Infinite` emitter가 loop boundary마다 burst 재생 | 모든 emitter를 `Self/Once`로 정규화하고 완료형 Inactive Response 확인. `bAutoDestroy`만으로 Infinite loop를 종료할 수 없음 |
| ColorFromCurve 색이 rapid param으로 안 바뀜 | 커브 키프레임 데이터는 rapid param 아님 | Scale Color / Initialize Particle Color 경로 사용 |
| 평면 링 메시 z-fighting / 교차면 깨짐 | DepthFade 미적용 | 머티리얼에 DepthFade 노드 추가 |
| Lightweight emitter 위치 이상 | world space 미지원 (UE 5.5+) | Standard emitter로 전환 또는 local space 설계 |
| Vortex Velocity 모듈 없음 | UE 5.4에서 제거 | Vortex Force 또는 Scratch Pad |

## 기존 에셋 워닝 정화
과거에 잘못 설정된 enum 핀(표시명/숫자)이 박힌 NiagaraSystem은 로드/컴파일 때마다 `PinToNiagaraVariable: Failed to convert ...` 워닝을 반복 출력한다(값은 무시되는 중). 정화: `unreal.NiagaraEmitterService.repair_invalid_enum_pins("/Game/.../NS_X")` — 깨진 enum 핀을 자동 탐지해 표시명이면 내부명으로 구조, 아니면 모듈 기본값으로 리셋(동작 불변, 워닝 제거). 반환값 = 수리된 핀 수. 수리 후 저장 필요.

## 현행 모듈 에셋 경로 카탈로그 (5.7 엔진 실측 — add_module 시 이 경로 사용)

**V2 있는 모듈 — 반드시 V2 경로 사용 (비-V2는 legacy 표시됨):**
| 모듈 | 현행 경로 |
|---|---|
| Initialize Particle | `/Niagara/Modules/Spawn/Initialization/V2/InitializeParticle` |
| Shape Location | `/Niagara/Modules/Spawn/Location/V2/ShapeLocation` |
| Sphere Location | `/Niagara/Modules/Spawn/Location/V2/SphereLocation` |
| Grid Location | `/Niagara/Modules/Spawn/Location/V2/GridLocation` |
| Skeletal Mesh Location | `/Niagara/Modules/Spawn/Location/V2/SkeletalMeshLocation` |
| Rotate Around Point | `/Niagara/Modules/Spawn/Location/V2/RotateAroundPoint` |
| Curl Noise Force | `/Niagara/Modules/Update/Forces/V2/CurlNoiseForce` |
| Point Attraction Force | `/Niagara/Modules/Update/Forces/V2/PointAttractionForce` |
| Initial Mesh Orientation | `/Niagara/Modules/Spawn/Orientation/V2/InitialMeshOrientation` |
| Update Mesh Orientation | `/Niagara/Modules/Update/Orientation/V2/UpdateMeshOrientation` |
| Orient Mesh To Camera/Vector | `/Niagara/Modules/Update/Orientation/V2/OrientMeshToCamera` · `OrientMeshToVector` |
| Mesh Rotation Force | `/Niagara/Modules/Update/Orientation/V2/MeshRotationForce` |
| SubUV Animation | `/Niagara/Modules/Update/SubUV/V2/SubUVAnimation` |
| Cone/Region Mask | `/Niagara/Modules/Masks/V2/ConeMask` · `RegionMask` |

**단일 버전(현행 그대로):**
`/Niagara/Modules/Emitter/SpawnRate` · `/Niagara/Modules/Emitter/SpawnBurst_Instantaneous` · `/Niagara/Modules/Emitter/EmitterState` ·
`/Niagara/Modules/Spawn/Velocity/AddVelocity`(·FromPoint·InCone) · `/Niagara/Modules/Update/Forces/Drag`·`GravityForce` ·
`/Niagara/Modules/Update/Color/ScaleColor`(·BySpeed) · `/Niagara/Modules/Update/Size/ScaleSpriteSize`(·BySpeed) ·
`/Niagara/Modules/Solvers/SolveForcesAndVelocity`

규칙: add_module 호출 전 위 표에서 경로 확인 — V2 존재 모듈에 비-V2 경로 쓰면 스택에 legacy/deprecated 표시 + 차기 버전 호환 위험.

## 추가 관측 워닝 (실사용 로그)
| 워닝 | 원인 | 수정 |
|---|---|---|
| `Cannot Set external constant ... Emitter.Module.SpawnRemainder - Node: Map Set` | SpawnRate 모듈 중복 추가 또는 모듈 리네임 후 남은 stale rapid-iteration 파라미터 | 같은 에미터에 SpawnRate가 2개면 하나 remove_module; 아니면 해당 SpawnRate를 remove 후 add_module로 재추가(RI 파라미터 재생성) |

## Python deprecated API 대체 (execute_python_code 작성 시)
| Deprecated (경고 발생) | 현행 |
|---|---|
| `unreal.EditorLevelLibrary.get_editor_world()` | `unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()` |
| `unreal.EditorLevelLibrary.spawn_actor_from_*` | `unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_*` |
| `unreal.EditorLevelLibrary.get_selected_level_actors()` | `unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_selected_level_actors()` |
| `unreal.XxxSubsystem()` 직접 생성 (예: `unreal.EditorActorSubsystem()`) | ❌ 5.2+ deprecated — 반드시 `unreal.get_editor_subsystem(unreal.XxxSubsystem)` |
| `unreal.EditorAssetLibrary` 일부 | 가능하면 `unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)` |

규칙: ①EditorLevelLibrary 등 Editor Scripting Utilities는 플러그인 자체가 deprecated ②Subsystem은 생성자 호출 금지, **항상 `unreal.get_editor_subsystem(클래스)`** 로 인스턴스 획득.

