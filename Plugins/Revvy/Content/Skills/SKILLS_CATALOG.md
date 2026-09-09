# Revvy 게임 스킬 카탈로그

Revvy가 세션에서 자동 로드하는 게임 개발 스킬 전체 목록입니다. 각 스킬은 `Content/Skills/<엔진>/<스킬명>/SKILL.md`에 정의되며, 대화 내용이 스킬의 트리거 조건과 맞으면 자동으로 선택됩니다.

**총 90개** — Unreal 78 · Shared(엔진 공통) 8 · Unity 2 · Godot 2

---

## Shared — 엔진 공통 (8)

| 스킬 | 설명 |
|---|---|
| `revvy` | Revvy 코어 — MCP 도구 탐색, 안전한 에디터 자동화, 스킬 선택, 외부 프록시 진단. 작업이 에디터 자동화인지 런타임 C++인지 판단 기준 포함 |
| `revvy-external-control` | 데스크톱 앱·터미널·IDE에서 상시 MCP 프록시를 통한 Revvy 제어 — CLI 설정, 연결 진단, 에디터 오프라인 시 캐시 기반 도구 탐색, 에디터 재시작 복구 |
| `lore-vcs` | Epic의 오픈소스 VCS Lore(코드+바이너리 에셋) — 파괴적 편집 전 체크포인트, 커밋/푸시/동기화, 복구 지점. git 저장소에는 사용하지 않음 |
| `gpt-sol-image-generation` | GPT Sol 계열 이미지 생성·정제·품질 검사·임포트 — 컨셉 아트, 텍스처, UI 아트, 아이콘, 스프라이트, 플립북 |
| `blender-3d-modeling` | Blender MCP로 3D 모델 검사·수정·검증·익스포트 — 메시/토폴로지, 트랜스폼, 모디파이어, 머티리얼, UV |
| `blender-animation-roundtrip` | Unreal AnimSequence → Blender 왕복 편집 — 본 모션 정제 후 검증 에셋으로 재임포트, 스켈레톤·타이밍·루트모션 검증 |
| `zen-uv` | Blender Zen UV 애드온 지식 — Zen Unwrap·마크 시스템, 아일랜드 선택 필터, 트랜스폼/쿼드리파이/릴랙스, 스택, 텍셀 밀도, 패킹 엔진, 트림시트·핫스팟 매핑. 유료 클로즈드소스라 공개 파이썬 API 가 없다 — 오퍼레이터 ID 를 지어내지 말 것 |
| `tripo-blender-character-pipeline` | AI 멀티뷰 레퍼런스 → Tripo 이미지→3D → Blender 리토포·텍스처·리깅·씬까지, 레디가 자동화 가능한 단계를 대신 실행하는 단계별 캐릭터 파이프라인 가이드 |

## Unreal — 에디터 자동화 도구 스킬 (38)

Revvy MCP 도구(서비스)로 에디터를 직접 조작하는 스킬들.

### 에셋 · 프로젝트 · 환경

| 스킬 | 설명 |
|---|---|
| `asset-management` | 에셋 검색/열기/이동/복제/저장/삭제/임포트/익스포트 |
| `checkpoints-recovery` | Revvy 도구가 덮어쓴 에셋의 이전 버전 복구 — "되돌려줘" 계열 요청 |
| `project-settings` | 프로젝트 설정 + 에디터 환경설정(UI 외형, 툴바, 스케일 등 UDeveloperSettings 전반) |
| `engine-settings` | 엔진 코어 설정 — 렌더링, 물리, 오디오, GC, cvar, 스케일러빌리티 |
| `dev-environment-setup` | 아티스트용 원클릭 C++ 개발환경 구축·수리 — VS 빌드툴 설치, 프로젝트 파일 재생성, 에디터 타깃 리빌드 |
| `screenshots` | 에디터 창·뷰포트·블루프린트 스크린샷 캡처(AI 비전 분석용) |
| `viewport` | 레벨 뷰포트 제어 — 카메라, 뷰 모드, FOV, 노출, 레이아웃 |
| `pie-testing` | PIE(Play-In-Editor) 세션 시작/중지/조회 — 런타임 테스트 |
| `automation-testing` | Unreal Automation Test 열거·실행, 결과 JSON 회수 — "테스트 돌려줘/검증해줘" |

### 블루프린트 · 스크립팅 · 데이터

| 스킬 | 설명 |
|---|---|
| `blueprints` | Blueprint 에셋·변수·함수·컴포넌트 생성/수정 |
| `blueprint-graphs` | 이벤트/함수 그래프에 노드 추가·연결·설정 |
| `enum-struct` | UserDefinedEnum/Struct 생성·수정·조회 |
| `gameplay-tags` | Gameplay Tag 생성/삭제/이름변경(런타임 등록 포함) |
| `data-assets` | Primary Data Asset 생성·프로퍼티 관리 |
| `data-tables` | Data Table 생성·행 관리 |
| `enhanced-input` | Input Action, Mapping Context, 트리거, 모디파이어 구성 |
| `state-trees` | StateTree 에셋 생성·검사·편집 — AI 행동, 게임 로직, 상태 머신 |

### 애니메이션

| 스킬 | 설명 |
|---|---|
| `animation-blueprint` | Animation Blueprint·상태머신·트랜지션 룰 탐색/검사/저작 |
| `animation-editing` | 제약 인지 본 회전 편집 — 프리뷰, 검증, 베이크, 리타깃 안전성 |
| `animation-montage` | 몽타주 생성·조작 — 섹션, 슬롯, 세그먼트, 브랜칭 포인트, 블렌드 |
| `animsequence` | 애니메이션 시퀀스 프리뷰/검증/베이크/조작 |
| `skeleton` | 스켈레톤·본·소켓·리타기팅·커브 메타데이터·블렌드 프로파일·본 제약 |

### 머티리얼 · VFX · 오디오

| 스킬 | 설명 |
|---|---|
| `materials` | 머티리얼·머티리얼 인스턴스 생성/편집 |
| `uv-mapping` | StaticMesh UV 채널 검사/생성/변환/시각화 |
| `niagara-systems` | Niagara 시스템 수명주기, 이미터 추가/복사, 유저 파라미터, 스크래치패드 |
| `niagara-emitters` | 이미터 내부 — 모듈, 렌더러, 신속 반복 파라미터, Custom HLSL |
| `effect-generation` | 프로덕션 VFX 설계·구축·검증·최적화 — 게임필 레이어까지 포함한 이펙트 종합 |
| `metasounds` | MetaSound Source 저작 — 노드 추가, 핀 연결, 프로시저럴 재생 |
| `sound-cues` | SoundCue 저작 — 노드 추가·연결, 오디오 프로퍼티 |

### 월드 · 터레인 · 레벨

| 스킬 | 설명 |
|---|---|
| `landscape` | 랜드스케이프 생성·편집 — 하이트맵, 스컬프팅, 페인트 레이어, 지형 분석 |
| `landscape-materials` | 랜드스케이프 머티리얼 — 레이어 블렌딩, 텍스처, 레이어 인포 |
| `landscape-auto-material` | 마스터 머티리얼+함수+인스턴스 패러다임의 프로덕션 자동 머티리얼 — RVT, 경사/고도/거리 자동 레이어링, 바이옴 |
| `terrain-data` | 실세계 지리 데이터로 하이트맵·참조 지도·수계 스플라인 생성(Mapbox) |
| `foliage` | 랜드스케이프 폴리지 인스턴스 배치·관리 |
| `level-actors` | 현재 레벨의 액터 조작(에디터 서브시스템 기반) |
| `map-blockout` | AAA 오픈월드 FPS 맵 블록아웃 절차 생성 — 도로/POI/숲/철도 계획 후 실제 지오메트리로 구체화 |
| `pcg` | PCG Graph 에셋 생성·검사·편집 — 노드/핀 배선 |

### UI · 레퍼런스

| 스킬 | 설명 |
|---|---|
| `umg-widgets` | Widget Blueprint 생성·수정(MVVM ViewModel 지원 포함) |
| `ue58-version-notes` | UE 5.8 버전 고정 API 레퍼런스 — 파괴적 변경·폐기·신규 표면. 엔진 대상 C++ 작성 전 필수 참조 |
| `epic-mcp-toolsets` | UE 5.8 내장 Epic 공식 실험적 MCP 서버 — 활성화, 툴셋 범위, Revvy MCP와의 공존 |
| `learning-agents` | Epic 공식 ML 플러그인 — 강화/모방학습 기반 NPC 훈련 지식 팩 |

## Unreal — C++ · 시스템 지식 팩 (37)

`ue-` 접두 스킬. 도구 호출이 아니라 엔진 시스템별 검증된 지식·패턴·레퍼런스를 제공합니다.

### 기초 · 아키텍처

| 스킬 | 설명 |
|---|---|
| `ue-cpp-foundations` | UPROPERTY/UFUNCTION/UCLASS, 컨테이너, 델리게이트, GC, 스마트 포인터 |
| `ue-module-build-system` | Build.cs/Target.cs, 모듈·플러그인 구성, 링크 오류·IWYU 진단 |
| `ue-actor-component-architecture` | 액터·컴포넌트 설계 — 수명주기, 스폰, 컴포지션, 인터페이스 |
| `ue-subsystems-taxonomy` | 5종 서브시스템 선택 가이드 — 수명 매트릭스, PIE 누수 방지 |
| `ue-project-context` | 프로젝트 컨텍스트 문서(`.agents/ue-project-context.md`) 생성·갱신 — 다른 UE 스킬들의 공통 참조점 |

### 게임플레이

| 스킬 | 설명 |
|---|---|
| `ue-gameplay-framework` | GameMode/GameState/PlayerController/PlayerState/Pawn/GameInstance — 권한·매치 흐름·스폰 |
| `ue-gameplay-abilities` | GAS — 어빌리티, 이펙트, 어트리뷰트, 태그, 쿨다운 |
| `ue-game-features` | Game Feature 플러그인, 모듈러 게임플레이, init state, 경험 시스템 |
| `ue-lyra-experience` | Lyra식 Experience 조합 — 정의·매니저·액션 셋·Pawn 데이터 |
| `ue-gameplay-message-subsystem` | GameplayTag 채널 pub/sub — 리스너 수명, 매칭, 선택 기준 |
| `ue-character-movement` | CMC — 이동 모드, 커스텀 이동, 네트워크 예측, 루트 모션 |
| `ue-mover-plugin` | UE 5.8 실험적 Mover 플러그인 — 레이어드 무브, 롤백 네트워킹, CMC 마이그레이션 |
| `ue-input-system` | Enhanced Input 심화 — 트리거/모디파이어 카탈로그, 커스텀 저작 |
| `ue-physics-collision` | 콜리전·트레이스·물리 시뮬레이션·Chaos |

### AI · 대규모 시뮬레이션

| 스킬 | 설명 |
|---|---|
| `ue-ai-navigation` | AIController, 비헤이비어 트리, 블랙보드, 퍼셉션, NavMesh, EQS |
| `ue-state-trees` | StateTree C++ — 태스크/컨디션/이밸류에이터 템플릿, Mass 연동 |
| `ue-mass-entity` | Mass Entity ECS — 프로세서, 프래그먼트, 옵저버, 군중 시뮬레이션 |

### 네트워킹 · 온라인

| 스킬 | 설명 |
|---|---|
| `ue-networking-replication` | 리플리케이션, RPC, 넷 롤, 권한, 예측 |
| `ue-iris-replication` | Iris 리플리케이션(5.5+) — 프래그먼트/필터/우선순위, 점진 마이그레이션 |
| `ue-online-subsystem-eos` | OSS+EOS — 세션, 로그인, 보이스, 업적, 패키징·진단 |

### 월드 · 절차 생성

| 스킬 | 설명 |
|---|---|
| `ue-world-level-streaming` | World Partition, 레벨 스트리밍/트래블, 데이터 레이어, HLOD |
| `ue-world-partition-ofpa` | World Partition 심화 — OFPA, 런타임 해시 그리드, 커맨들릿, wp.* 진단 |
| `ue-procedural-generation` | PCG 프레임워크, ProceduralMesh, HISM, 스플라인, 노이즈, 던전 생성 |

### 렌더링 · VFX · 오디오 · 애니메이션 · 시네마틱

| 스킬 | 설명 |
|---|---|
| `ue-materials-rendering` | 머티리얼/셰이더/MID/포스트프로세스/렌더 타깃/Nanite/Lumen |
| `ue-niagara-effects` | Niagara C++ — 스폰, 파라미터, 데이터 인터페이스, 성능 튜닝 |
| `ue-audio-system` | 오디오 시스템 — 어테뉴에이션, 서브믹스, 동시성, 공간 오디오 |
| `ue-animation-system` | AnimInstance, 몽타주, 블렌드 스페이스, 노티파이, IK, 링크드 애님 그래프 |
| `ue-sequencer-cinematics` | Sequencer, 컷신, 카메라, Movie Render Queue |

### UI

| 스킬 | 설명 |
|---|---|
| `ue-ui-umg-slate` | UMG/Slate/HUD — 위젯 타입, 바인딩, 입력 모드 |
| `ue-common-ui` | CommonUI — 액티버터블 위젯, 스택, 게임패드 우선 포커스, 모달 |

### 데이터 · 비동기

| 스킬 | 설명 |
|---|---|
| `ue-data-assets-tables` | DataAsset/DataTable, 소프트 참조, 비동기 로딩, Asset Manager |
| `ue-serialization-savegames` | 세이브/로드 — USaveGame, FArchive, 슬롯 관리 |
| `ue-async-threading` | 비동기·스레딩 — TaskGraph, UE::Tasks, ParallelFor, 스레드 안전 |

### 도구 · 테스트 · 빌드

| 스킬 | 설명 |
|---|---|
| `ue-editor-tools` | 에디터 확장 — 유틸리티 위젯, 디테일 커스터마이제이션, UToolMenus |
| `ue-testing-debugging` | 테스트·디버깅 — UE_LOG, check/ensure, DrawDebug, Insights, stat |
| `ue-automation-functional-tests` | 자동화 테스트 저작 — Functional Test 액터, Spec, Gauntlet |
| `ue-build-cook-package` | 빌드/쿡/패키징 — RunUAT BuildCookRun, BuildGraph, 쿡 오류 triage |

## Unity (2)

| 스킬 | 설명 |
|---|---|
| `unity-overview` | Revvy Unity 지원 현황 — `Bridges/Unity` 에디터 브리지, 검증된 도구 티어, 로드맵 |
| `unity-editor-automation` | Unity 에디터 자동화 원리 — 메인스레드 규칙, 검증된 API 앵커, 헤드리스 CI 진입점 |

## Godot (2)

| 스킬 | 설명 |
|---|---|
| `godot-overview` | Revvy Godot 지원 현황 — 의존성 없는 11-도구 에디터 브리지, 검증 티어, 로드맵 |
| `godot-editor-automation` | Godot 에디터 자동화 원리 — EditorPlugin/EditorInterface 표면, GDScript 제약, 헤드리스 CI |

---

### 구조 메모

- **도구 스킬 vs 지식 팩**: Unreal의 무접두 스킬은 대부분 Revvy MCP 도구(서비스)를 실제로 호출하는 자동화 스킬이고, `ue-` 접두 스킬은 C++/시스템 지식·패턴 레퍼런스입니다. 둘은 상호 참조로 연결되어 있습니다(예: `enhanced-input` 도구 ↔ `ue-input-system` 지식).
- **엔진별 격리**: Unity/Godot 세션에는 해당 엔진 스킬만 노출됩니다(per-engine skill isolation).
- **버전 기준**: Unreal 스킬은 UE 5.8 기준으로 검증·핀 고정되어 있습니다(`ue58-version-notes` 참조).
